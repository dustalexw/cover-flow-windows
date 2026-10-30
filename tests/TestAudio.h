#pragma once
#include <QFile>
#include <QString>
#include <QtEndian>
#include <cmath>
#include <numbers>

// Writes a 16-bit PCM WAV containing a sine tone.
inline bool writeWav(const QString &path, double seconds, int rate = 44100, int channels = 2, double hz = 440) {
    const qint32 frames = qint32(seconds * rate);
    const qint32 dataBytes = frames * channels * 2;
    QByteArray b;
    auto u32 = [&](quint32 v) { char c[4]; qToLittleEndian(v, c); b.append(c, 4); };
    auto u16 = [&](quint16 v) { char c[2]; qToLittleEndian(v, c); b.append(c, 2); };
    b.append("RIFF"); u32(36 + dataBytes); b.append("WAVE");
    b.append("fmt "); u32(16); u16(1); u16(quint16(channels)); u32(quint32(rate)); u32(quint32(rate * channels * 2)); u16(quint16(channels * 2)); u16(16);
    b.append("data"); u32(quint32(dataBytes));
    for (qint32 i = 0; i < frames; ++i) {
        const qint16 s = qint16(std::sin(2 * std::numbers::pi * hz * i / rate) * 12000);
        for (int c = 0; c < channels; ++c) u16(quint16(s));
    }
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}
