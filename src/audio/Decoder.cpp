#include "Decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <QFile>
#include <algorithm>
#include <cstring>

namespace {
QString avError(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof buf);
    return QString::fromUtf8(buf);
}
}

struct Decoder::Impl {
    AVFormatContext *fmt = nullptr;
    AVCodecContext *codec = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int stream = -1;
    bool drained = false;      // decoder flushed after end of file
    bool flushedSwr = false;
    QByteArray pending;        // converted samples not yet handed out

    ~Impl() { reset(); }
    void reset() {
        if (swr) swr_free(&swr);
        if (codec) avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        stream = -1; drained = false; flushedSwr = false; pending.clear();
    }
};

Decoder::Decoder(int outputRate) : d(std::make_unique<Impl>()), m_rate(outputRate) {}
Decoder::~Decoder() = default;

bool Decoder::isOpen() const { return d->codec != nullptr; }
void Decoder::close() { d->reset(); }

bool Decoder::open(const QString &path, QString *error) {
    close();
    auto fail = [&](const QString &message) {
        m_error = message;
        if (error) *error = message;
        close();
        return false;
    };
    // FFmpeg takes UTF-8 paths on every platform, including Windows.
    const QByteArray utf8 = path.toUtf8();
    int rc = avformat_open_input(&d->fmt, utf8.constData(), nullptr, nullptr);
    if (rc < 0) return fail(QStringLiteral("Could not open file: %1").arg(avError(rc)));
    if ((rc = avformat_find_stream_info(d->fmt, nullptr)) < 0)
        return fail(QStringLiteral("Could not read stream info: %1").arg(avError(rc)));
    const AVCodec *codec = nullptr;
    d->stream = av_find_best_stream(d->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (d->stream < 0 || !codec) return fail(QStringLiteral("No playable audio stream."));
    // Discard everything else (cover art "video" streams in particular).
    for (unsigned i = 0; i < d->fmt->nb_streams; ++i)
        if (int(i) != d->stream) d->fmt->streams[i]->discard = AVDISCARD_ALL;

    d->codec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(d->codec, d->fmt->streams[d->stream]->codecpar);
    d->codec->pkt_timebase = d->fmt->streams[d->stream]->time_base;
    if ((rc = avcodec_open2(d->codec, codec, nullptr)) < 0)
        return fail(QStringLiteral("Unsupported codec: %1").arg(avError(rc)));

    AVChannelLayout inLayout;
    if (d->codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || d->codec->ch_layout.nb_channels == 0)
        av_channel_layout_default(&inLayout, std::max(1, d->codec->ch_layout.nb_channels));
    else
        av_channel_layout_copy(&inLayout, &d->codec->ch_layout);
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    rc = swr_alloc_set_opts2(&d->swr, &outLayout, AV_SAMPLE_FMT_FLT, m_rate,
                             &inLayout, d->codec->sample_fmt, d->codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&inLayout);
    if (rc < 0 || swr_init(d->swr) < 0) return fail(QStringLiteral("Could not set up resampling."));

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    m_error.clear();
    return true;
}

qint64 Decoder::read(QByteArray &out, qint64 maxFrames) {
    if (!isOpen() || maxFrames <= 0) return 0;
    const qint64 frameBytes = Channels * qint64(sizeof(float));
    const qint64 wanted = maxFrames * frameBytes;

    auto convert = [&](const uint8_t **input, int inSamples) {
        const int capacity = swr_get_out_samples(d->swr, inSamples);
        if (capacity <= 0) return;
        const qsizetype start = d->pending.size();
        d->pending.resize(start + capacity * frameBytes);
        uint8_t *dst = reinterpret_cast<uint8_t *>(d->pending.data() + start);
        const int got = swr_convert(d->swr, &dst, capacity, input, inSamples);
        d->pending.resize(start + std::max(0, got) * frameBytes);
    };

    while (d->pending.size() < wanted) {
        int rc = avcodec_receive_frame(d->codec, d->frame);
        if (rc == 0) {
            convert(const_cast<const uint8_t **>(d->frame->extended_data), d->frame->nb_samples);
            av_frame_unref(d->frame);
            continue;
        }
        if (rc == AVERROR_EOF) {
            if (!d->flushedSwr) { convert(nullptr, 0); d->flushedSwr = true; }
            break;
        }
        if (rc != AVERROR(EAGAIN)) { m_error = avError(rc); break; }
        if (d->drained) break;
        rc = av_read_frame(d->fmt, d->packet);
        if (rc < 0) {
            avcodec_send_packet(d->codec, nullptr);   // flush
            d->drained = true;
            continue;
        }
        if (d->packet->stream_index == d->stream) {
            rc = avcodec_send_packet(d->codec, d->packet);
            // A single corrupt packet should not end the track.
            if (rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_INVALIDDATA) m_error = avError(rc);
        }
        av_packet_unref(d->packet);
    }

    const qint64 take = std::min<qint64>(wanted, d->pending.size()) / frameBytes * frameBytes;
    out.append(d->pending.constData(), take);
    d->pending.remove(0, take);
    return take / frameBytes;
}

bool Decoder::seek(double seconds) {
    if (!isOpen()) return false;
    const AVStream *s = d->fmt->streams[d->stream];
    const int64_t ts = av_rescale_q(int64_t(std::max(0.0, seconds) * AV_TIME_BASE), AV_TIME_BASE_Q, s->time_base);
    if (av_seek_frame(d->fmt, d->stream, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(d->codec);
    swr_close(d->swr);
    swr_init(d->swr);
    d->pending.clear();
    d->drained = false;
    d->flushedSwr = false;
    // Decode forward and drop audio before the target so seeks are sample accurate.
    const qint64 frameBytes = Channels * qint64(sizeof(float));
    for (;;) {
        int rc = avcodec_receive_frame(d->codec, d->frame);
        if (rc == 0) {
            const double start = d->frame->best_effort_timestamp == AV_NOPTS_VALUE ? seconds
                : d->frame->best_effort_timestamp * av_q2d(s->time_base);
            const double end = start + double(d->frame->nb_samples) / d->codec->sample_rate;
            if (end > seconds) {
                QByteArray chunk;
                std::swap(chunk, d->pending);
                const int capacity = swr_get_out_samples(d->swr, d->frame->nb_samples);
                chunk.resize(capacity * frameBytes);
                uint8_t *dst = reinterpret_cast<uint8_t *>(chunk.data());
                const int got = swr_convert(d->swr, &dst, capacity,
                    const_cast<const uint8_t **>(d->frame->extended_data), d->frame->nb_samples);
                chunk.resize(std::max(0, got) * frameBytes);
                const qint64 skip = std::clamp<qint64>(qint64((seconds - start) * m_rate), 0, got) * frameBytes;
                d->pending = chunk.mid(skip);
                av_frame_unref(d->frame);
                return true;
            }
            av_frame_unref(d->frame);
            continue;
        }
        if (rc != AVERROR(EAGAIN)) return true;
        rc = av_read_frame(d->fmt, d->packet);
        if (rc < 0) { avcodec_send_packet(d->codec, nullptr); d->drained = true; continue; }
        if (d->packet->stream_index == d->stream) avcodec_send_packet(d->codec, d->packet);
        av_packet_unref(d->packet);
    }
}

double Decoder::duration() const {
    if (!d->fmt) return 0;
    const AVStream *s = d->fmt->streams[d->stream];
    if (s->duration != AV_NOPTS_VALUE) return s->duration * av_q2d(s->time_base);
    if (d->fmt->duration != AV_NOPTS_VALUE) return double(d->fmt->duration) / AV_TIME_BASE;
    return 0;
}

QString Decoder::codecName() const {
    return d->codec ? QString::fromUtf8(d->codec->codec->name) : QString();
}
int Decoder::sourceRate() const { return d->codec ? d->codec->sample_rate : 0; }
int Decoder::sourceBits() const {
    if (!d->codec) return 0;
    if (d->codec->bits_per_raw_sample > 0) return d->codec->bits_per_raw_sample;
    return av_get_bytes_per_sample(d->codec->sample_fmt) * 8;
}
