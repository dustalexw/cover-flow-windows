#include "core/Lyrics.h"
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

class TestLyrics : public QObject {
    Q_OBJECT
private slots:
    void parsesTimestampsAndFractions() {
        const auto doc = LyricsDocument::parse("\xEF\xBB\xBF[00:01.5]half\n[00:02.05]five hundredths\n[01:00.500]minute\n");
        QCOMPARE(doc.cues.size(), 3);
        QCOMPARE(doc.cues[0].time, 1.5);
        QCOMPARE(doc.cues[1].time, 2.05);
        QCOMPARE(doc.cues[2].time, 60.5);
        QVERIFY(doc.isSynced());
    }
    void repeatedStampsAndTranslationsMerge() {
        const auto doc = LyricsDocument::parse("[00:10][00:20]Chorus\n[00:10]Coro\n[00:10]Chorus\n[00:05]Intro");
        QCOMPARE(doc.cues.size(), 3);
        QCOMPARE(doc.cues[0].text, QString("Intro"));
        QCOMPARE(doc.cues[1].text, QString("Chorus\nCoro"));   // duplicates skipped
        QCOMPARE(doc.cues[2].time, 20.0);
        QCOMPARE(doc.cues[2].id, 2);
    }
    void enhancedWordStampsRemovedAndEmptyLinesKept() {
        const auto doc = LyricsDocument::parse("[00:01.00]<00:01.00>Hello <00:01.50>world\n[00:03.00]\n[ar:Someone]\n[00:61.00]bad seconds");
        QCOMPARE(doc.cues.size(), 2);
        QCOMPARE(doc.cues[0].text, QString("Hello world"));
        QCOMPARE(doc.cues[1].text, QString());
    }
    void offsetAdvancesHighlight() {
        const auto doc = LyricsDocument::parse("[offset:500]\n[00:01.00]a\n[00:02.00]b");
        QCOMPARE(doc.offset, 0.5);
        QCOMPARE(doc.activeIndex(0.4), -1);
        QCOMPARE(doc.activeIndex(0.5), 0);
        QCOMPARE(doc.activeIndex(1.6), 1);
        QCOMPARE(doc.playbackTime(doc.cues[1]), 1.5);
    }
    void plainTextIsUnsynced() {
        const auto doc = LyricsDocument::parse("\nFirst line\n\nSecond line\n[not a stamp]\n");
        QVERIFY(!doc.isSynced());
        QCOMPARE(doc.plainText, QString("First line\n\nSecond line"));
        QVERIFY(LyricsDocument::parse("  \n[ti:x]\n").isEmpty());
    }
    void decodesUtf16AndRejectsBadUtf8() {
        QByteArray le("\xFF\xFE", 2);
        le += QByteArray(reinterpret_cast<const char *>(u"[00:01]hi"), 18);
        QCOMPARE(*LyricsStore::decode(le), QString("[00:01]hi"));
        QString error;
        QVERIFY(!LyricsStore::decode(QByteArray("\xC3\x28", 2), &error));
        QVERIFY(error.contains("UTF-8"));
    }
    void storeLookupOrder() {
        QTemporaryDir dir;
        const QString music = dir.filePath("music"), store = dir.filePath("store");
        QDir().mkpath(music);
        const QString audio = music + "/01 Song.flac";
        QFile(audio).open(QIODevice::WriteOnly);
        const QUuid id = QUuid::createUuid();
        QVERIFY(!LyricsStore::find(id, audio, nullptr, store));
        { QFile f(music + "/01 Song.LRC"); f.open(QIODevice::WriteOnly); f.write("[00:01]sidecar"); }
        auto found = LyricsStore::find(id, audio, nullptr, store);
        QVERIFY(found);
        QVERIFY(!found->imported);
        QCOMPARE(found->document.cues[0].text, QString("sidecar"));
        QVERIFY(LyricsStore::importText("[00:02]saved", id, nullptr, store));
        found = LyricsStore::find(id, audio, nullptr, store);
        QVERIFY(found->imported);
        QCOMPARE(found->document.cues[0].text, QString("saved"));
        QString error;
        QVERIFY(!LyricsStore::importText("", id, &error, store));
        QVERIFY(error.contains("no readable lyrics"));
        // A failed import leaves the saved lyrics alone.
        QCOMPARE(LyricsStore::find(id, audio, nullptr, store)->document.cues[0].text, QString("saved"));
        QVERIFY(!LyricsStore::importFile(music + "/song.pdf", id, &error, store));
        QCOMPARE(error, QString("Choose an .lrc, .txt, .lyric or .lyrics file."));
    }
};

QTEST_GUILESS_MAIN(TestLyrics)
#include "tst_lyrics.moc"
