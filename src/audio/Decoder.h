#pragma once
#include <QString>
#include <QByteArray>
#include <memory>

// Decodes any FFmpeg-supported audio file to interleaved float32 stereo at a fixed
// output rate. Encoder delay/padding (LAME, iTunSMPB, Opus pre-skip) is trimmed by
// libavcodec's skip_samples handling, which keeps album playback gapless.
class Decoder {
public:
    static constexpr int Channels = 2;

    explicit Decoder(int outputRate);
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;

    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const;

    // Appends up to maxFrames frames of float32 stereo to out. Returns frames
    // appended; 0 means end of stream (or a decode error, see lastError()).
    qint64 read(QByteArray &out, qint64 maxFrames);
    bool seek(double seconds);

    double duration() const;     // seconds, 0 if unknown
    int outputRate() const { return m_rate; }
    QString lastError() const { return m_error; }
    // Original stream properties, for the UI ("FLAC · 96 kHz · 24-bit").
    QString codecName() const;
    int sourceRate() const;
    int sourceBits() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_rate;
    QString m_error;
};
