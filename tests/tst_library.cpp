#include "core/LibraryStore.h"
#include "core/LibraryScanner.h"
#include "core/Paths.h"
#include "core/Palette.h"
#include "TestAudio.h"

#include <QImage>
#include <QBuffer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

static void tag(const QString &path, const QString &title, const QString &artist, const QString &album, int track,
                const QString &albumArtist = {}) {
    TagLib::FileRef f(QFile::encodeName(path).constData());
    QVERIFY(!f.isNull());
    TagLib::PropertyMap props;
    props["TITLE"] = TagLib::String(title.toStdString(), TagLib::String::UTF8);
    props["ARTIST"] = TagLib::String(artist.toStdString(), TagLib::String::UTF8);
    props["ALBUM"] = TagLib::String(album.toStdString(), TagLib::String::UTF8);
    props["TRACKNUMBER"] = TagLib::String(QString("%1/12").arg(track).toStdString());
    props["DATE"] = TagLib::String("1997-08-21");
    if (!albumArtist.isEmpty()) props["ALBUMARTIST"] = TagLib::String(albumArtist.toStdString(), TagLib::String::UTF8);
    f.file()->setProperties(props);
    QVERIFY(f.save());
}

class TestLibrary : public QObject {
    Q_OBJECT
    QTemporaryDir m_data;
private slots:
    void initTestCase() { qputenv("COVERFLOW_DATA_DIR", m_data.path().toUtf8()); }

    void textHelpers() {
        QCOMPARE(Text::normalizedKey("  Béyoncé   ＫＮＯＷＬＥＳ "), QString("beyonce knowles"));
        QVERIFY(Text::naturalCompare("Track 2", "track 10") < 0);
        QVERIFY(Text::naturalCompare("b", "A") > 0);
        QVERIFY(Text::naturalCompare("01 x", "1 x") != 0);
        QCOMPARE(Text::stableHash(""), quint64(5381));
        QCOMPARE(Text::stableHash("a"), quint64(5381 * 33 + 97));
    }
    void fileNameFallbacks() {
        auto p = LibraryScanner::parseFileName("1-03 Song Name");
        QCOMPARE(*p.disc, 1); QCOMPARE(*p.track, 3); QCOMPARE(p.title, QString("Song Name"));
        p = LibraryScanner::parseFileName("07. Another");
        QVERIFY(!p.disc); QCOMPARE(*p.track, 7); QCOMPARE(p.title, QString("Another"));
        p = LibraryScanner::parseFileName("Just a title");
        QVERIFY(!p.track); QCOMPARE(p.title, QString("Just a title"));
        QCOMPARE(*LibraryScanner::parseNumberPair("3/12"), 3);
        QVERIFY(!LibraryScanner::parseNumberPair("0"));
        QCOMPARE(*LibraryScanner::parseYear("Released 1997-08-21"), 1997);
        QVERIFY(!LibraryScanner::parseYear("1700"));
    }
    void readsMacLibraryJson() {
        // Shape written by Swift's JSONEncoder on macOS.
        QFile file(QStringLiteral(TEST_DATA_DIR "/mac-library.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray json = file.readAll();
        LibraryStore store;
        QVERIFY(store.fromJson(QJsonDocument::fromJson(json).object()));
        QCOMPARE(store.albums().size(), 1);
        const Track *t = store.track(QUuid::fromString(QStringLiteral("0D6B6F34-6F06-4A2B-9E3A-111111111111")));
        QVERIFY(t);
        QCOMPARE(t->path, QString("/Users/me/Music/Oasis/03 Song.flac"));
        QCOMPARE(t->dateAdded.toSecsSinceEpoch(), qint64(800000000 + AppleEpochOffset));
        QVERIFY(store.albums()[0].appearance && !store.albums()[0].appearance->sceneEnabled);
        // Round trip keeps the Swift encoding.
        const QJsonObject out = store.toJson();
        const QJsonObject track = out["tracks"].toArray()[0].toObject();
        QVERIFY(track["fileURL"].toObject()["relative"].toString().startsWith("file:///Users/me/Music/Oasis/03%20Song.flac"));
        QCOMPARE(track["dateAdded"].toDouble(), 800000000.5);
        QVERIFY(!track.contains("albumArtist"));   // nil optionals omitted
        LibraryStore again;
        QVERIFY(again.fromJson(out));
        QCOMPARE(again.toJson(), out);
    }
    void importGroupsSortsAndDeduplicates() {
        QTemporaryDir music;
        const QString a = music.filePath("Oasis/Be Here Now"), b = music.filePath("Comp"), loose = music.filePath("Loose Folder");
        QDir().mkpath(a); QDir().mkpath(b); QDir().mkpath(loose);
        QVERIFY(writeWav(a + "/10 Ten.wav", 1.0)); tag(a + "/10 Ten.wav", "Ten", "Oasis", "Be Here Now", 10);
        QVERIFY(writeWav(a + "/2 Two.wav", 1.2)); tag(a + "/2 Two.wav", "Two", "Oasis", "Be Here Now", 2);
        QVERIFY(writeWav(b + "/1.wav", 1.0)); tag(b + "/1.wav", "One", "Artist A", "Mix", 1);
        QVERIFY(writeWav(b + "/2.wav", 1.1)); tag(b + "/2.wav", "Two", "Artist B", "Mix", 2);
        QVERIFY(writeWav(loose + "/03 - Untagged Song.wav", 0.8));
        { QImage img(64, 48, QImage::Format_RGB32); img.fill(QColor("#C03020")); img.save(loose + "/cover.jpg"); }
        { QFile f(loose + "/notes.mid"); f.open(QIODevice::WriteOnly); f.write("x"); }

        LibraryStore store;
        const ImportReport r = store.importNow({music.path()});
        QCOMPARE(r.added, 5);
        QCOMPARE(r.albums, 3);
        QCOMPARE(r.unsupported.size(), 1);
        QCOMPARE(r.headline(), QString("Imported 5 tracks into 3 albums."));

        const auto albums = store.visibleAlbums({}, SortOrder::Artist);
        QCOMPARE(albums.size(), 3);
        const Album *oasis = nullptr, *mix = nullptr, *folder = nullptr;
        for (const Album *al : albums) {
            if (al->title == "Be Here Now") oasis = al;
            if (al->title == "Mix") mix = al;
            if (al->title == "Loose Folder") folder = al;
        }
        QVERIFY(oasis && mix && folder);
        QCOMPARE(*oasis->year, 1997);
        QCOMPARE(store.tracksOf(*oasis)[0]->title, QString("Two"));      // sorted by track number
        QCOMPARE(mix->artist, QString("Various Artists"));
        QVERIFY(folder->missingMetadata);
        QCOMPARE(store.tracksOf(*folder)[0]->title, QString("Untagged Song"));
        QCOMPARE(*store.tracksOf(*folder)[0]->trackNumber, 3);
        QVERIFY(folder->artworkFile);                                    // folder image picked up
        { const QColor t(*folder->tintHex), e("#C03020"); QVERIFY(std::abs(t.red() - e.red()) + std::abs(t.green() - e.green()) + std::abs(t.blue() - e.blue()) < 12); }

        QCOMPARE(store.visibleAlbums("untagged", SortOrder::Title).size(), 1);   // matches track titles
        QCOMPARE(store.visibleAlbums("OASÍS", SortOrder::Title).size(), 1);

        const ImportReport again = store.importNow({music.path()});
        QCOMPARE(again.added, 0);
        QCOMPARE(again.duplicates, 5);
        QCOMPARE(again.headline(), QString("Those 5 tracks are already in your collection."));

        // Removing the last track of an album removes the album; audio stays on disk.
        const QUuid only = folder->trackIDs[0];
        const QString path = store.track(only)->path;
        store.removeTracks({only});
        QCOMPARE(store.albums().size(), 2);
        QVERIFY(QFile::exists(path));
    }
    void paletteContrast() {
        const auto p = Palette::make(QColor("#203040"), {QColor("#FF3300"), QColor("#808080")});
        QVERIFY(Palette::contrast(p.foreground, p.background) >= 4.5);
        QVERIFY(Palette::contrast(Palette::readable(QColor("#333333"), QColor("#222222")), QColor("#222222")) >= 4.8 - 1e-6);
    }
};

QTEST_GUILESS_MAIN(TestLibrary)
#include "tst_library.moc"
