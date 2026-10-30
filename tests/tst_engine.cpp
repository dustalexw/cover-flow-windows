#include "audio/PlaybackEngine.h"
#include "audio/Player.h"
#include "core/LibraryStore.h"
#include "TestAudio.h"

#include <QAudioDevice>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// Real-time playback through the default output device. Skipped when there is none.
class TestEngine : public QObject {
    Q_OBJECT
    QTemporaryDir m_data, m_music;
private slots:
    void initTestCase() {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio output device");
        qputenv("COVERFLOW_DATA_DIR", m_data.path().toUtf8());
    }
    void gaplessQueuePlaysBackToBack() {
        QVERIFY(writeWav(m_music.filePath("1 A.wav"), 1.0, 44100));
        QVERIFY(writeWav(m_music.filePath("2 B.wav"), 0.7, 48000, 1, 660));
        QVERIFY(writeWav(m_music.filePath("3 C.wav"), 1.2, 22050, 2, 330));
        LibraryStore library;
        QCOMPARE(library.importNow({m_music.path()}).added, 3);
        const QVector<QUuid> tracks = library.albums()[0].trackIDs;

        PlaybackEngine engine;
        Player player(&library, &engine);
        QVector<QPair<qint64, QUuid>> starts;
        QElapsedTimer clock;
        connect(&player, &Player::trackStarted, this, [&](const QUuid &id) { starts.append({clock.elapsed(), id}); });
        QSignalSpy finished(&engine, &PlaybackEngine::finished);
        double lastPosition = -1;
        bool monotonic = true;
        qint64 audible = -1;      // when track 1 actually started sounding (device start-up varies)
        connect(&player, &Player::positionChanged, this, [&] {
            if (audible < 0 && starts.size() == 1 && player.position() > 0.05) audible = clock.elapsed() - qint64(player.position() * 1000);
            if (starts.size() == 1 && player.position() + 0.06 < lastPosition) monotonic = false;
            if (starts.size() == 1) lastPosition = player.position();
        });

        clock.start();
        player.setQueue(tracks, 0, true);
        QVERIFY(finished.wait(10000));
        const qint64 total = clock.elapsed() - audible;
        QVERIFY(audible >= 0);

        QCOMPARE(starts.size(), 3);
        QCOMPARE(starts[0].second, tracks[0]);
        QCOMPARE(starts[1].second, tracks[1]);
        QCOMPARE(starts[2].second, tracks[2]);
        // Transitions land where the previous track ends (no gap): 1.0 s after track 1
        // became audible, then 0.7 s later. Allow for the 50 ms status poll.
        QVERIFY2(std::abs((starts[1].first - audible) - 1000) < 150, qPrintable(QString::number(starts[1].first - audible)));
        QVERIFY2(std::abs((starts[2].first - starts[1].first) - 700) < 150, qPrintable(QString::number(starts[2].first - starts[1].first)));
        // 2.9 s of audio, then the end is reported once the last samples were heard.
        QVERIFY2(total >= 2800 && total < 3400, qPrintable(QString::number(total)));
        QVERIFY(monotonic);
        QVERIFY(!player.isPlaying());
    }
    void playNextReplacesDecodedAheadTrack() {
        QTemporaryDir music;
        QVERIFY(writeWav(music.filePath("1 One.wav"), 1.5));
        QVERIFY(writeWav(music.filePath("2 Two.wav"), 0.6, 44100, 2, 550));
        QVERIFY(writeWav(music.filePath("3 Three.wav"), 0.6, 44100, 2, 770));
        LibraryStore library;
        library.importNow({music.path()});
        const QVector<QUuid> t = library.albums()[0].trackIDs;
        PlaybackEngine engine;
        Player player(&library, &engine);
        QVector<QUuid> started;
        connect(&player, &Player::trackStarted, this, [&](const QUuid &id) { started.append(id); });
        QSignalSpy finished(&engine, &PlaybackEngine::finished);
        player.setQueue({t[0], t[1]}, 0, true);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0.2, 4000);   // track 2 is decoded ahead by now
        player.playNext({t[2]});
        QVERIFY(finished.wait(10000));
        QCOMPARE(started.last(), t[1]);
        QVERIFY(started.contains(t[2]));
        QVERIFY(started.indexOf(t[2]) < started.lastIndexOf(t[1]));
    }
    void seekAndPause() {
        QVERIFY(writeWav(m_music.filePath("long.wav"), 6.0));
        LibraryStore library;
        library.importNow({m_music.filePath("long.wav")});
        PlaybackEngine engine;
        Player player(&library, &engine);
        player.setQueue({library.albums()[0].trackIDs[0]}, 0, true);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0.3, 3000);
        player.seek(4.0);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() >= 4.0 && player.position() < 4.6, 2000);
        player.pause();
        QTest::qWait(150);
        const double paused = player.position();
        QTest::qWait(500);
        QVERIFY(std::abs(player.position() - paused) < 0.05);
        QVERIFY(!player.isPlaying());
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > paused + 0.3, 2000);
    }
};

QTEST_MAIN(TestEngine)
#include "tst_engine.moc"
