#pragma once
#include <QObject>
#include <QString>
#include <QAudioFormat>
#include <memory>
#include <functional>
#include <optional>
#include <QVector>

class QAudioSink;
class QMediaDevices;
class RingSource;

// Gapless player. A decode thread feeds a ring buffer that QAudioSink pulls from;
// the next queued track is decoded straight into the same stream when the
// current one ends, and trackChanged() fires when its first sample is heard.
class PlaybackEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ isPlaying NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY trackChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
public:
    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;

    // Starts `path` (from startSeconds). `token` is echoed back in trackChanged so
    // the caller can map it to a queue entry.
    void play(const QString &path, qint64 token, double startSeconds = 0);
    // Called (on this object's thread) when the decoder reaches the end of the track
    // `tailToken`; returns {path, token} of the track to continue with, or nothing.
    using NextProvider = std::function<std::optional<std::pair<QString, qint64>>(qint64 tailToken)>;
    void setNextProvider(NextProvider provider);
    // Tracks already decoded ahead of the one being heard, in order.
    QVector<qint64> upcomingTokens() const;
    // True when nothing more will be decoded (the queue ended at the tail).
    bool streamEnded() const;
    void pause();
    void resume();
    void stop();
    void seek(double seconds);

    bool isPlaying() const;
    bool isPaused() const;
    double position() const;
    double duration() const;
    qint64 currentToken() const;
    double volume() const { return m_volume; }
    void setVolume(double v);
    // Last few hundred ms of output, mono, for visualizers.
    QVector<float> recentSamples(int count) const;

signals:
    void stateChanged();
    void positionChanged();
    void volumeChanged();
    void trackChanged(qint64 token);
    void finished();                 // queue ran out
    void error(const QString &message);
    // Output device changed; the controller should replay `token` from `seconds`.
    void deviceRestartRequested(qint64 token, double seconds, bool paused);

private:
    void startSink();
    void poll();
    void restartOnDeviceChange();

    std::unique_ptr<RingSource> m_source;
    QAudioSink *m_sink = nullptr;
    QMediaDevices *m_devices = nullptr;
    QAudioFormat m_format;
    double m_volume = 1.0;
    bool m_paused = false;
    qint64 m_reportedToken = -1;
    double m_lastPosition = -1;
    double m_lastDuration = 0;
    NextProvider m_nextProvider;
    QVector<qint64> m_upcoming;
    bool m_streamEnded = false;
};
