#include "audio/Player.h"
#include "audio/PlaybackEngine.h"
#include "core/LibraryStore.h"
#include "TestAudio.h"
#include <QTemporaryDir>
#include <QtTest>

class TestPlayer : public QObject {
    Q_OBJECT
    QTemporaryDir m_data, m_music;
    LibraryStore *m_library = nullptr;
    QVector<QUuid> m_tracks;
private slots:
    void initTestCase() {
        qputenv("COVERFLOW_DATA_DIR", m_data.path().toUtf8());
        for (int i = 1; i <= 5; ++i) QVERIFY(writeWav(m_music.filePath(QString("%1 Track %2.wav").arg(i).arg(QChar('A' + i))), 0.5 + i * 0.1));
        m_library = new LibraryStore(this);
        QCOMPARE(m_library->importNow({m_music.path()}).added, 5);
        m_tracks = m_library->albums()[0].trackIDs;
    }
    void successorRules() {
        QCOMPARE(*Player::successor(1, 3, RepeatMode::Off, true), 2);
        QVERIFY(!Player::successor(2, 3, RepeatMode::Off, true));
        QCOMPARE(*Player::successor(2, 3, RepeatMode::All, true), 0);
        QCOMPARE(*Player::successor(1, 3, RepeatMode::One, true), 1);
        QCOMPARE(*Player::successor(1, 3, RepeatMode::One, false), 2);   // manual Next ignores Repeat One
    }
    void queueEditing() {
        PlaybackEngine engine;
        Player p(m_library, &engine);
        p.setQueue(m_tracks.mid(0, 3), 1, false);
        QCOMPARE(p.currentIndex(), 1);
        p.playNext({m_tracks[4]});
        QCOMPARE(p.queue()[2], m_tracks[4]);
        p.addToQueue({m_tracks[3]});
        QCOMPARE(p.queue().back(), m_tracks[3]);
        p.moveInQueue(0, 4);                      // current track keeps playing position
        QCOMPARE(*p.currentTrackID(), m_tracks[1]);
        p.clearUpNext();
        QCOMPARE(p.queue().back(), m_tracks[1]);
        p.removeFromQueue(0);
        QCOMPARE(*p.currentTrackID(), m_tracks[1]);
    }
    void shuffleRestoresOrder() {
        PlaybackEngine engine;
        Player p(m_library, &engine);
        p.setQueue(m_tracks, 2, false);
        p.setShuffle(true);
        QCOMPARE(p.queue().mid(0, 3), m_tracks.mid(0, 3));          // played part untouched
        QCOMPARE(QSet<QUuid>(p.queue().begin(), p.queue().end()), QSet<QUuid>(m_tracks.begin(), m_tracks.end()));
        p.setShuffle(false);
        QCOMPARE(p.queue(), m_tracks);
        QCOMPARE(*p.currentTrackID(), m_tracks[2]);
    }
    void previousRestartsAfterThreeSeconds() {
        PlaybackEngine engine;
        Player p(m_library, &engine);
        p.setQueue(m_tracks, 2, false);
        p.seek(0.5);
        p.previous();
        QCOMPARE(p.currentIndex(), 1);
        p.setRepeatModeInt(int(RepeatMode::All));
        p.setQueue(m_tracks, 0, false);
        p.previous();
        QCOMPARE(p.currentIndex(), 4);
    }
    void persistsAndRestores() {
        {
            PlaybackEngine engine;
            Player p(m_library, &engine);
            p.setQueue(m_tracks, 3, false);
            p.seek(0.25);
            p.setVolume(0.4);
            p.cycleRepeat();
            p.saveNow();
        }
        PlaybackEngine engine;
        Player p(m_library, &engine);
        p.restore();
        QCOMPARE(p.queue(), m_tracks);
        QCOMPARE(p.currentIndex(), 3);
        QCOMPARE(p.position(), 0.25);
        QCOMPARE(p.volume(), 0.4);
        QVERIFY(!p.isPlaying());                  // restored paused
    }
    void purgeDropsRemovedTracks() {
        PlaybackEngine engine;
        Player p(m_library, &engine);
        p.setQueue(m_tracks, 1, false);
        p.purge({m_tracks[1], m_tracks[0]});
        QCOMPARE(p.queue().size(), 3);
        QCOMPARE(*p.currentTrackID(), m_tracks[2]);
    }
};

QTEST_GUILESS_MAIN(TestPlayer)
#include "tst_player.moc"
