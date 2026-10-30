#include "PlaybackEngine.h"
#include "Decoder.h"

#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QIODevice>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <optional>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstring>

namespace {
constexpr int FrameBytes = Decoder::Channels * int(sizeof(float));

struct Segment {
    qint64 startFrame;   // absolute frame (in this stream generation) where it begins
    qint64 token;
    double offset;       // seconds into the track at startFrame
    double duration;
};
}

// QIODevice the sink pulls from. Owns the decode thread.
class RingSource : public QIODevice {
public:
    explicit RingSource(int rate, bool int16Output)
        : m_rate(rate), m_int16(int16Output), m_decoder(rate) {
        open(QIODevice::ReadOnly);
        m_thread = std::thread([this] { run(); });
    }
    ~RingSource() override {
        { std::lock_guard lock(m_mutex); m_quit = true; }
        m_cv.notify_all();
        m_thread.join();
    }

    void start(const QString &path, qint64 token, double at) {
        std::lock_guard lock(m_mutex);
        resetLocked();
        m_command = Command{path, token, at};
        m_cv.notify_all();
    }
    // Answer to a successor request (empty path = the stream ends after the tail).
    void provideNext(qint64 forTail, const QString &path, qint64 token) {
        std::lock_guard lock(m_mutex);
        if (!m_awaitingNext || forTail != m_tailToken) return;
        m_awaitingNext = false;
        if (path.isEmpty()) { m_endOfStream = true; m_decoder.close(); }
        else { m_nextPath = path; m_nextToken = token; }
        m_cv.notify_all();
    }
    void stopAll() {
        std::lock_guard lock(m_mutex);
        resetLocked();
        m_nextPath.clear();
        m_cv.notify_all();
    }
    void seekCurrent(double seconds) {
        std::lock_guard lock(m_mutex);
        const Segment *s = currentSegmentLocked();
        if (!s) return;
        const QString path = m_paths.value(s->token);
        const qint64 token = s->token;
        resetLocked();
        m_command = Command{path, token, seconds};
        m_cv.notify_all();
    }

    // Snapshot for the UI thread.
    struct Status {
        qint64 token = -1; double position = 0; double duration = 0; bool active = false; bool ended = false; QString error;
        bool awaitingNext = false; qint64 tailToken = -1;
        bool streamEnded = false;           // nothing more will be decoded
        QVector<qint64> upcoming;           // decoded-ahead tracks after the audible one
    };
    Status status(qint64 latencyFrames) {
        std::lock_guard lock(m_mutex);
        Status st;
        const qint64 heard = std::max<qint64>(0, m_readFrame - latencyFrames);
        const Segment *seg = nullptr;
        for (const Segment &s : m_segments) if (s.startFrame <= heard) seg = &s;
        if (seg) {
            st.token = seg->token;
            st.duration = seg->duration;
            st.position = seg->offset + double(heard - seg->startFrame) / m_rate;
            if (st.duration > 0) st.position = std::min(st.position, st.duration);
        }
        // Drop segments that are fully behind us.
        while (m_segments.size() > 1 && m_segments[1].startFrame <= heard) m_segments.pop_front();
        st.active = !m_segments.empty() || m_command.has_value();
        st.ended = m_endOfStream && heard >= m_writeFrame;
        st.awaitingNext = m_awaitingNext;
        st.tailToken = m_tailToken;
        st.streamEnded = m_endOfStream;
        for (const Segment &s : m_segments) if (s.startFrame > heard) st.upcoming.append(s.token);
        st.error = m_error; m_error.clear();
        return st;
    }
    QVector<float> recent(int count) const {
        std::lock_guard lock(m_mutex);
        QVector<float> out(count, 0.f);
        const int have = int(m_history.size());
        for (int i = 0; i < count && i < have; ++i) out[count - 1 - i] = m_history[have - 1 - i];
        return out;
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 20; } // never signal EOF to the sink

protected:
    qint64 writeData(const char *, qint64) override { return -1; }
    qint64 readData(char *data, qint64 maxlen) override {
        std::lock_guard lock(m_mutex);
        const int outFrameBytes = m_int16 ? Decoder::Channels * 2 : FrameBytes;
        qint64 frames = maxlen / outFrameBytes;
        const qint64 available = qint64(m_buffer.size()) / FrameBytes;
        const qint64 take = std::min(frames, available);
        const float *src = reinterpret_cast<const float *>(m_buffer.data());
        if (m_int16) {
            auto *dst = reinterpret_cast<qint16 *>(data);
            for (qint64 i = 0; i < take * Decoder::Channels; ++i)
                dst[i] = qint16(std::lround(std::clamp(src[i], -1.f, 1.f) * 32767.f));
        } else {
            std::memcpy(data, src, size_t(take * FrameBytes));
        }
        for (qint64 i = 0; i < take; i += 4) {             // decimated mono history for visualizers
            m_history.push_back(0.5f * (src[2 * i] + src[2 * i + 1]));
        }
        while (m_history.size() > 8192) m_history.pop_front();
        m_buffer.remove(0, take * FrameBytes);
        m_readFrame += take;
        // Underrun or end: hand out silence so the device keeps a steady clock.
        const qint64 silent = frames - take;
        std::memset(data + take * outFrameBytes, 0, size_t(silent * outFrameBytes));
        // After the last track, keep the clock running through the trailing silence so
        // the end is reported once the final samples have actually been heard. (During
        // a mid-track underrun the clock must not move, or positions would drift.)
        if (m_endOfStream && m_buffer.isEmpty()) m_readFrame += silent;
        m_cv.notify_all();
        return frames * outFrameBytes;
    }

private:
    struct Command { QString path; qint64 token; double at; };

    void resetLocked() {
        m_buffer.clear();
        m_segments.clear();
        m_readFrame = m_writeFrame = 0;
        m_endOfStream = false;
        m_awaitingNext = false;
        m_tailToken = -1;
        m_nextPath.clear();
        m_command.reset();
        ++m_generation;
    }
    const Segment *currentSegmentLocked() const {
        const Segment *seg = nullptr;
        for (const Segment &s : m_segments) if (s.startFrame <= m_readFrame) seg = &s;
        return seg ? seg : (m_segments.empty() ? nullptr : &m_segments.front());
    }

    void run() {
        const qint64 capacityBytes = qint64(m_rate) * FrameBytes * 2;   // ~2 s ahead
        const qint64 chunkFrames = 4096;
        QByteArray chunk;
        std::unique_lock lock(m_mutex);
        while (!m_quit) {
            if (m_command) {
                Command cmd = *m_command; m_command.reset();
                const quint64 gen = m_generation;
                lock.unlock();
                QString error;
                bool ok = m_decoder.open(cmd.path, &error);
                if (ok && cmd.at > 0) m_decoder.seek(cmd.at);
                lock.lock();
                if (gen != m_generation) continue;            // superseded while opening
                if (!ok) { m_error = error; m_endOfStream = true; continue; }
                m_paths[cmd.token] = cmd.path;
                m_segments.push_back({m_writeFrame, cmd.token, cmd.at, m_decoder.duration()});
                m_tailToken = cmd.token;
                m_endOfStream = false;
                continue;
            }
            if (!m_decoder.isOpen() || m_endOfStream || m_awaitingNext || qint64(m_buffer.size()) >= capacityBytes) {
                m_cv.wait(lock);
                continue;
            }
            const quint64 gen = m_generation;
            lock.unlock();
            chunk.clear();
            const qint64 got = m_decoder.read(chunk, chunkFrames);
            lock.lock();
            if (gen != m_generation) continue;
            if (got > 0) {
                m_buffer.append(chunk);
                m_writeFrame += got;
                continue;
            }
            // End of this track: continue straight into the next one. The engine asks
            // the player for it (from the UI thread) and we wait for the answer.
            if (m_nextPath.isEmpty()) { m_awaitingNext = true; continue; }
            {
                const QString path = m_nextPath; const qint64 token = m_nextToken;
                m_nextPath.clear();
                lock.unlock();
                QString error;
                const bool ok = m_decoder.open(path, &error);
                lock.lock();
                if (gen != m_generation) continue;
                if (ok) {
                    m_paths[token] = path;
                    m_segments.push_back({m_writeFrame, token, 0, m_decoder.duration()});
                    m_tailToken = token;
                } else {
                    m_error = error;
                    m_endOfStream = true;
                }
                continue;
            }
        }
    }

    const int m_rate;
    const bool m_int16;
    Decoder m_decoder;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    bool m_quit = false;
    quint64 m_generation = 0;
    std::optional<Command> m_command;
    QByteArray m_buffer;
    std::deque<Segment> m_segments;
    std::deque<float> m_history;
    QHash<qint64, QString> m_paths;
    qint64 m_readFrame = 0, m_writeFrame = 0;
    bool m_endOfStream = false;
    bool m_awaitingNext = false;
    qint64 m_tailToken = -1;
    QString m_nextPath; qint64 m_nextToken = -1;
    QString m_error;
};

PlaybackEngine::PlaybackEngine(QObject *parent) : QObject(parent), m_devices(new QMediaDevices(this)) {
    auto *timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, &PlaybackEngine::poll);
    timer->start();
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &PlaybackEngine::restartOnDeviceChange);
}

PlaybackEngine::~PlaybackEngine() {
    if (m_sink) m_sink->stop();
    delete m_sink;
}

void PlaybackEngine::startSink() {
    if (m_sink) return;
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    QAudioFormat format;
    format.setSampleRate(device.isNull() ? 48000 : device.preferredFormat().sampleRate());
    format.setChannelCount(Decoder::Channels);
    format.setSampleFormat(QAudioFormat::Float);
    bool int16 = false;
    if (!device.isNull() && !device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        int16 = true;
    }
    m_format = format;
    m_source = std::make_unique<RingSource>(format.sampleRate(), int16);
    m_upcoming.clear();
    m_sink = new QAudioSink(device, format, this);
    m_sink->setBufferSize(format.bytesForDuration(150'000));   // 150 ms device buffer
    m_sink->setVolume(m_volume);
    m_sink->start(m_source.get());
}

void PlaybackEngine::play(const QString &path, qint64 token, double startSeconds) {
    startSink();
    m_paused = false;
    m_reportedToken = -1;
    m_source->start(path, token, startSeconds);
    if (m_sink->state() == QAudio::SuspendedState) m_sink->resume();
    emit stateChanged();
}

void PlaybackEngine::setNextProvider(NextProvider provider) { m_nextProvider = std::move(provider); }

QVector<qint64> PlaybackEngine::upcomingTokens() const { return m_upcoming; }
bool PlaybackEngine::streamEnded() const { return m_streamEnded; }

void PlaybackEngine::pause() {
    if (!m_sink || m_paused) return;
    m_sink->suspend();
    m_paused = true;
    emit stateChanged();
}

void PlaybackEngine::resume() {
    if (!m_sink || !m_paused) return;
    m_sink->resume();
    m_paused = false;
    emit stateChanged();
}

void PlaybackEngine::stop() {
    if (m_source) m_source->stopAll();
    m_paused = false;
    m_reportedToken = -1;
    emit stateChanged();
    emit positionChanged();
}

void PlaybackEngine::seek(double seconds) {
    if (m_source) m_source->seekCurrent(seconds);
    emit positionChanged();
}

void PlaybackEngine::setVolume(double v) {
    v = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(v, m_volume)) return;
    m_volume = v;
    if (m_sink) m_sink->setVolume(v);
    emit volumeChanged();
}

static qint64 latencyFrames(QAudioSink *sink, const QAudioFormat &format) {
    if (!sink || format.bytesPerFrame() == 0) return 0;
    return std::max<qint64>(0, sink->bufferSize() - sink->bytesFree()) / format.bytesPerFrame();
}

bool PlaybackEngine::isPlaying() const { return m_source && m_reportedToken >= 0 && !m_paused; }
bool PlaybackEngine::isPaused() const { return m_paused; }
qint64 PlaybackEngine::currentToken() const { return m_reportedToken; }

double PlaybackEngine::position() const { return std::max(0.0, m_lastPosition); }
double PlaybackEngine::duration() const { return m_lastDuration; }

QVector<float> PlaybackEngine::recentSamples(int count) const {
    return m_source ? m_source->recent(count) : QVector<float>(count, 0.f);
}

void PlaybackEngine::poll() {
    if (!m_source) return;
    const auto st = m_source->status(latencyFrames(m_sink, m_format));
    m_upcoming = st.upcoming;
    m_streamEnded = st.streamEnded;
    if (st.awaitingNext) {
        // The decoder reached the end of the tail track: ask what follows it.
        const auto next = m_nextProvider ? m_nextProvider(st.tailToken) : std::nullopt;
        m_source->provideNext(st.tailToken, next ? next->first : QString(), next ? next->second : -1);
    }
    if (!st.error.isEmpty()) emit error(st.error);
    if (st.token != m_reportedToken && st.token >= 0) {
        m_reportedToken = st.token;
        m_lastDuration = st.duration;
        emit trackChanged(st.token);
        emit stateChanged();
    }
    if (st.ended && m_reportedToken >= 0) {
        m_reportedToken = -1;
        m_lastPosition = 0;
        emit positionChanged();
        emit stateChanged();
        emit finished();
        return;
    }
    if (st.token >= 0 && std::abs(st.position - m_lastPosition) > 1e-3) {
        m_lastPosition = st.position;
        emit positionChanged();
    }
}

void PlaybackEngine::restartOnDeviceChange() {
    // Follow the system default output (headphones plugged in, etc.). Resume the
    // current track at its position on the new device.
    if (!m_sink) return;
    const qint64 token = m_reportedToken;
    const double at = m_lastPosition;
    const bool wasPaused = m_paused;
    m_sink->stop();
    delete m_sink; m_sink = nullptr;
    m_source.reset();
    if (token < 0) return;
    emit deviceRestartRequested(token, at, wasPaused);
}
