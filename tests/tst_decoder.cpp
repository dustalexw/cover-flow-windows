#include "audio/Decoder.h"
#include "TestAudio.h"
#include <QTemporaryDir>
#include <QtTest>

class TestDecoder : public QObject {
    Q_OBJECT
private slots:
    void decodesAndResamplesToOutputRate() {
        QTemporaryDir dir;
        const QString path = dir.filePath("tone.wav");
        QVERIFY(writeWav(path, 2.0, 44100, 1));            // mono 44.1 kHz
        Decoder d(48000);
        QVERIFY(d.open(path));
        QCOMPARE(d.sourceRate(), 44100);
        QVERIFY(std::abs(d.duration() - 2.0) < 1e-3);
        QByteArray out;
        qint64 frames = 0, got;
        while ((got = d.read(out, 4096)) > 0) frames += got;
        // 2 s at 48 kHz, allowing for resampler edge samples.
        QVERIFY2(std::abs(frames - 96000) <= 64, qPrintable(QString::number(frames)));
        QCOMPARE(out.size(), frames * 2 * qint64(sizeof(float)));
        // Mono is duplicated to both channels.
        const float *s = reinterpret_cast<const float *>(out.constData());
        QCOMPARE(s[2000], s[2001]);
    }
    void seekIsSampleAccurate() {
        QTemporaryDir dir;
        const QString path = dir.filePath("tone.wav");
        QVERIFY(writeWav(path, 3.0, 48000, 2));
        Decoder d(48000);
        QVERIFY(d.open(path));
        QVERIFY(d.seek(1.0));
        QByteArray out;
        qint64 frames = 0, got;
        while ((got = d.read(out, 8192)) > 0) frames += got;
        QVERIFY2(std::abs(frames - 96000) <= 2, qPrintable(QString::number(frames)));
    }
    void reportsUnreadableFiles() {
        QTemporaryDir dir;
        const QString path = dir.filePath("bad.mp3");
        { QFile f(path); f.open(QIODevice::WriteOnly); f.write("not audio at all"); }
        Decoder d(48000);
        QString error;
        QVERIFY(!d.open(path, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDecoder)
#include "tst_decoder.moc"
