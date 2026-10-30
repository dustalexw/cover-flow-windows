#include "LibraryStore.h"
#include "LibraryScanner.h"
#include "Artwork.h"
#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QtConcurrent>
#include <cmath>

// ---------- ImportReport ----------

static QString plural(int n, const QString &one, const QString &many) { return QString::number(n) + " " + (n == 1 ? one : many); }

QString ImportReport::headline() const {
    if (found == 0 && unsupported.isEmpty() && failed.isEmpty()) return "No audio files found there.";
    if (added == 0 && duplicates == 1 && found == 1) return "That track is already in your collection.";
    if (added == 0 && duplicates > 0 && duplicates == found) return QString("Those %1 tracks are already in your collection.").arg(duplicates);
    if (added == 0) return "Nothing new was imported.";
    return QString("Imported %1 into %2.").arg(plural(added, "track", "tracks"), plural(albums, "album", "albums"));
}

QString ImportReport::details() const {
    QStringList parts;
    if (added > 0 && duplicates > 0) parts << plural(duplicates, "duplicate", "duplicates") + " skipped";
    auto names = [](const QStringList &paths, int limit) {
        QStringList shown;
        for (int i = 0; i < paths.size() && i < limit; ++i) shown << QFileInfo(paths[i]).fileName();
        QString text = shown.join(", ");
        if (paths.size() > limit) text += QString(" and %1 more").arg(paths.size() - limit);
        return text;
    };
    if (!unsupported.isEmpty()) parts << "Unsupported format: " + names(unsupported, 3);
    if (!failed.isEmpty()) {
        QStringList shown;
        for (int i = 0; i < failed.size() && i < 2; ++i) shown << failed[i];
        parts << "Couldn’t read: " + shown.join("; ") + (failed.size() > 2 ? QString(" and %1 more").arg(failed.size() - 2) : QString());
    }
    return parts.join(". ");
}

// ---------- JSON helpers (macOS Codable schema) ----------

namespace {
QString uuidString(const QUuid &id) { return id.toString(QUuid::WithoutBraces).toUpper(); }
QJsonValue opt(const std::optional<QString> &v) { return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Undefined); }
QJsonValue opt(const std::optional<int> &v) { return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Undefined); }
void put(QJsonObject &o, const char *key, const QJsonValue &v) { if (!v.isUndefined()) o.insert(key, v); }
std::optional<QString> optString(const QJsonObject &o, const char *k) {
    return o.contains(k) && o[k].isString() ? std::optional<QString>(o[k].toString()) : std::nullopt;
}
std::optional<int> optInt(const QJsonObject &o, const char *k) {
    return o.contains(k) && o[k].isDouble() ? std::optional<int>(o[k].toInt()) : std::nullopt;
}
// Swift encodes URL as {"relative": "file:///..."}; accept plain strings too.
QString pathFromURL(const QJsonValue &v) {
    const QString s = v.isObject() ? v.toObject()["relative"].toString() : v.toString();
    const QUrl url(s);
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : s;
}
QJsonObject urlObject(const QString &path) { return {{"relative", QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}}; }
}

QString LibraryStore::fingerprint(const QString &title, const QString &artist, const QString &album, double duration) {
    return Text::normalizedKey(title) + "|" + Text::normalizedKey(artist) + "|" + Text::normalizedKey(album) + "|"
        + QString::number(qint64(std::llround(duration)));
}

QJsonObject LibraryStore::toJson() const {
    QJsonArray albums, tracks;
    for (const Album &a : m_albums) {
        QJsonObject o{{"id", uuidString(a.id)}, {"groupKey", a.groupKey}, {"title", a.title}, {"artist", a.artist},
                      {"artworkVersion", a.artworkVersion}, {"dateAdded", toAppleTime(a.dateAdded)},
                      {"isDemo", a.isDemo}, {"missingMetadata", a.missingMetadata}, {"userEdited", a.userEdited}};
        QJsonArray ids;
        for (const QUuid &id : a.trackIDs) ids.append(uuidString(id));
        o["trackIDs"] = ids;
        put(o, "year", opt(a.year));
        put(o, "genre", opt(a.genre));
        put(o, "artworkFile", opt(a.artworkFile));
        put(o, "artworkPixelWidth", opt(a.artworkPixelWidth));
        put(o, "artworkPixelHeight", opt(a.artworkPixelHeight));
        put(o, "tintHex", opt(a.tintHex));
        if (a.lastPlayed) o["lastPlayed"] = toAppleTime(*a.lastPlayed);
        if (a.appearance && !a.appearance->isDefault()) {
            const AlbumAppearance &ap = *a.appearance;
            QJsonObject p{{"sceneEnabled", ap.sceneEnabled}, {"sceneStrength", ap.sceneStrength}, {"sceneVariation", ap.sceneVariation}};
            put(p, "backgroundHex", opt(ap.backgroundHex));
            put(p, "textHex", opt(ap.textHex));
            put(p, "accentHex", opt(ap.accentHex));
            if (ap.sceneRefinement) p["sceneRefinement"] = *ap.sceneRefinement;
            put(p, "refinementStyle", opt(ap.refinementStyle));
            put(p, "immersiveRefinementStyle", opt(ap.immersiveRefinementStyle));
            o["appearance"] = p;
        }
        albums.append(o);
    }
    for (const Track &t : m_tracks) {
        QJsonObject o{{"id", uuidString(t.id)}, {"fileURL", urlObject(t.path)}, {"title", t.title}, {"artist", t.artist},
                      {"albumTitle", t.albumTitle}, {"duration", t.duration}, {"dateAdded", toAppleTime(t.dateAdded)},
                      {"isDemo", t.isDemo}, {"missingMetadata", t.missingMetadata}, {"fingerprint", t.fingerprint}};
        put(o, "albumArtist", opt(t.albumArtist));
        put(o, "trackNumber", opt(t.trackNumber));
        put(o, "discNumber", opt(t.discNumber));
        put(o, "year", opt(t.year));
        put(o, "genre", opt(t.genre));
        tracks.append(o);
    }
    return {{"version", 1}, {"albums", albums}, {"tracks", tracks}};
}

bool LibraryStore::fromJson(const QJsonObject &root) {
    if (root["version"].toInt() != 1 || !root["albums"].isArray() || !root["tracks"].isArray()) return false;
    QVector<Album> albums;
    QVector<Track> tracks;
    for (const QJsonValue &v : root["tracks"].toArray()) {
        const QJsonObject o = v.toObject();
        Track t;
        t.id = QUuid::fromString(o["id"].toString());
        if (t.id.isNull()) return false;
        t.path = pathFromURL(o["fileURL"]);
        t.title = o["title"].toString(); t.artist = o["artist"].toString(); t.albumTitle = o["albumTitle"].toString();
        t.albumArtist = optString(o, "albumArtist");
        t.trackNumber = optInt(o, "trackNumber"); t.discNumber = optInt(o, "discNumber"); t.year = optInt(o, "year");
        t.duration = o["duration"].toDouble();
        t.genre = optString(o, "genre");
        t.dateAdded = fromAppleTime(o["dateAdded"].toDouble());
        t.isDemo = o["isDemo"].toBool(); t.missingMetadata = o["missingMetadata"].toBool();
        t.fingerprint = o["fingerprint"].toString();
        tracks.append(t);
    }
    for (const QJsonValue &v : root["albums"].toArray()) {
        const QJsonObject o = v.toObject();
        Album a;
        a.id = QUuid::fromString(o["id"].toString());
        if (a.id.isNull()) return false;
        a.groupKey = o["groupKey"].toString(); a.title = o["title"].toString(); a.artist = o["artist"].toString();
        a.year = optInt(o, "year"); a.genre = optString(o, "genre");
        for (const QJsonValue &id : o["trackIDs"].toArray()) a.trackIDs.append(QUuid::fromString(id.toString()));
        a.artworkFile = optString(o, "artworkFile");
        a.artworkPixelWidth = optInt(o, "artworkPixelWidth"); a.artworkPixelHeight = optInt(o, "artworkPixelHeight");
        a.artworkVersion = o["artworkVersion"].toInt();
        a.tintHex = optString(o, "tintHex");
        a.dateAdded = fromAppleTime(o["dateAdded"].toDouble());
        if (o.contains("lastPlayed")) a.lastPlayed = fromAppleTime(o["lastPlayed"].toDouble());
        a.isDemo = o["isDemo"].toBool(); a.missingMetadata = o["missingMetadata"].toBool(); a.userEdited = o["userEdited"].toBool();
        if (o["appearance"].isObject()) {
            const QJsonObject p = o["appearance"].toObject();
            AlbumAppearance ap;
            ap.backgroundHex = optString(p, "backgroundHex"); ap.textHex = optString(p, "textHex"); ap.accentHex = optString(p, "accentHex");
            ap.sceneEnabled = p["sceneEnabled"].toBool(true);
            ap.sceneStrength = p["sceneStrength"].toDouble(0.55);
            ap.sceneVariation = p["sceneVariation"].toInt();
            if (p.contains("sceneRefinement")) ap.sceneRefinement = p["sceneRefinement"].toBool();
            ap.refinementStyle = optString(p, "refinementStyle");
            ap.immersiveRefinementStyle = optString(p, "immersiveRefinementStyle");
            a.appearance = ap;
        }
        albums.append(a);
    }
    m_albums = albums;
    m_tracks = tracks;
    rebuildIndexes();
    return true;
}

// ---------- store ----------

LibraryStore::LibraryStore(QObject *parent) : QObject(parent) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &LibraryStore::saveNow);
}

QString LibraryStore::libraryPath() const { return Paths::dataDir() + "/Library.json"; }

bool LibraryStore::load(QString *notice) {
    QFile file(libraryPath());
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error == QJsonParseError::NoError && doc.isObject() && fromJson(doc.object())) {
        emit changed();
        return true;
    }
    // Keep the unreadable file and start empty.
    const QString backup = Paths::dataDir() + QString("/Library-unreadable-%1.json").arg(QDateTime::currentSecsSinceEpoch());
    QFile::rename(libraryPath(), backup);
    if (notice) *notice = "Your library file couldn’t be read, so Cover Flow started with an empty collection. A copy was kept as " + QFileInfo(backup).fileName() + ".";
    return false;
}

void LibraryStore::saveNow() {
    m_saveTimer.stop();
    QSaveFile file(libraryPath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    file.commit();
}

void LibraryStore::scheduleSave() { m_saveTimer.start(); }

void LibraryStore::rebuildIndexes() {
    m_albumIndex.clear(); m_trackIndex.clear(); m_albumByGroup.clear(); m_knownPaths.clear(); m_knownFingerprints.clear();
    for (int i = 0; i < m_albums.size(); ++i) { m_albumIndex[m_albums[i].id] = i; m_albumByGroup[m_albums[i].groupKey] = i; }
    for (int i = 0; i < m_tracks.size(); ++i) {
        m_trackIndex[m_tracks[i].id] = i;
        m_knownPaths.insert(QDir::cleanPath(m_tracks[i].path));
        m_knownFingerprints.insert(m_tracks[i].fingerprint);
    }
}

const Album *LibraryStore::album(const QUuid &id) const {
    auto it = m_albumIndex.find(id);
    return it == m_albumIndex.end() ? nullptr : &m_albums[*it];
}
const Track *LibraryStore::track(const QUuid &id) const {
    auto it = m_trackIndex.find(id);
    return it == m_trackIndex.end() ? nullptr : &m_tracks[*it];
}
QVector<const Track *> LibraryStore::tracksOf(const Album &album) const {
    QVector<const Track *> out;
    for (const QUuid &id : album.trackIDs) if (const Track *t = track(id)) out.append(t);
    return out;
}

static bool trackLess(const Track *a, const Track *b) {
    const int da = a->discNumber.value_or(1), db = b->discNumber.value_or(1);
    if (da != db) return da < db;
    const int ta = a->trackNumber.value_or(INT_MAX), tb = b->trackNumber.value_or(INT_MAX);
    if (ta != tb) return ta < tb;
    return Text::naturalCompare(QFileInfo(a->path).fileName(), QFileInfo(b->path).fileName()) < 0;
}

void LibraryStore::finalizeAlbum(Album &album) {
    QVector<const Track *> tracks = tracksOf(album);
    std::stable_sort(tracks.begin(), tracks.end(), trackLess);
    album.trackIDs.clear();
    for (const Track *t : tracks) album.trackIDs.append(t->id);
    if (album.userEdited || tracks.isEmpty()) return;
    for (const Track *t : tracks)
        if (t->albumArtist) { album.artist = *t->albumArtist; return; }
    QSet<QString> artists;
    for (const Track *t : tracks) artists.insert(Text::normalizedKey(t->artist));
    album.artist = artists.size() > 1 ? QStringLiteral("Various Artists") : tracks.front()->artist;
}

QVector<const Album *> LibraryStore::visibleAlbums(const QString &search, SortOrder order) const {
    const QString q = Text::normalizedKey(search);
    QVector<const Album *> out;
    for (const Album &a : m_albums) {
        if (!q.isEmpty()) {
            bool hit = Text::normalizedKey(a.title).contains(q) || Text::normalizedKey(a.artist).contains(q);
            for (int i = 0; !hit && i < a.trackIDs.size(); ++i)
                if (const Track *t = track(a.trackIDs[i]))
                    hit = Text::normalizedKey(t->title).contains(q) || Text::normalizedKey(t->artist).contains(q);
            if (!hit) continue;
        }
        out.append(&a);
    }
    auto byArtist = [](const Album *a, const Album *b) {
        if (int c = Text::naturalCompare(Text::normalizedKey(a->artist), Text::normalizedKey(b->artist))) return c < 0;
        const int ya = a->year.value_or(9999), yb = b->year.value_or(9999);
        if (ya != yb) return ya < yb;
        return Text::naturalCompare(a->title, b->title) < 0;
    };
    switch (order) {
    case SortOrder::Artist: std::stable_sort(out.begin(), out.end(), byArtist); break;
    case SortOrder::Title:
        std::stable_sort(out.begin(), out.end(), [](const Album *a, const Album *b) { return Text::naturalCompare(a->title, b->title) < 0; });
        break;
    case SortOrder::RecentlyAdded:
        std::stable_sort(out.begin(), out.end(), [&](const Album *a, const Album *b) {
            if (a->dateAdded != b->dateAdded) return a->dateAdded > b->dateAdded;
            return byArtist(a, b);
        });
        break;
    case SortOrder::RecentlyPlayed:
        std::stable_sort(out.begin(), out.end(), [&](const Album *a, const Album *b) {
            if (a->lastPlayed && b->lastPlayed && *a->lastPlayed != *b->lastPlayed) return *a->lastPlayed > *b->lastPlayed;
            if (a->lastPlayed.has_value() != b->lastPlayed.has_value()) return a->lastPlayed.has_value();
            return byArtist(a, b);
        });
        break;
    }
    return out;
}

// ---------- import ----------

namespace {
struct ScannedFile { QString path; LibraryScanner::Result result; };
}

ImportReport LibraryStore::importNow(const QStringList &paths, const std::function<void(int, int, QString)> &progress) {
    QStringList audio, unsupported;
    LibraryScanner::enumerate(paths, &audio, &unsupported);
    ImportReport report;
    report.found = audio.size();
    report.unsupported = unsupported;
    QSet<QUuid> touched;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (int i = 0; i < audio.size(); ++i) {
        const QString path = audio[i];
        if (progress) progress(i, audio.size(), QFileInfo(path).fileName());
        if (m_knownPaths.contains(path)) { ++report.duplicates; continue; }
        const LibraryScanner::Result r = m_prescanned.contains(path) ? m_prescanned.value(path) : LibraryScanner::read(path);
        if (!r.metadata) {
            if (r.notPlayable) report.unsupported << path;
            else report.failed << QFileInfo(path).fileName() + " (" + r.error + ")";
            continue;
        }
        const ScannedMetadata &m = *r.metadata;
        const QFileInfo info(path);
        const QString folder = QDir::cleanPath(info.absolutePath());
        Track t;
        t.id = QUuid::createUuid();
        t.path = path;
        t.title = m.title.value_or(info.completeBaseName());
        t.artist = m.artist ? *m.artist : m.albumArtist.value_or("Unknown Artist");
        t.albumTitle = m.album.value_or(info.dir().dirName());
        t.albumArtist = m.albumArtist;
        t.trackNumber = m.trackNumber; t.discNumber = m.discNumber; t.year = m.year; t.genre = m.genre;
        t.duration = m.duration;
        t.dateAdded = now;
        t.missingMetadata = m.usedFallback;
        t.fingerprint = fingerprint(t.title, t.artist, t.albumTitle, t.duration);
        if (m_knownFingerprints.contains(t.fingerprint)) { ++report.duplicates; continue; }

        const QString groupKey = m.album
            ? "album:" + Text::normalizedKey(*m.album) + "|" + (m.albumArtist ? Text::normalizedKey(*m.albumArtist) : "dir:" + folder)
            : "folder:" + folder;
        int albumIdx;
        if (auto it = m_albumByGroup.find(groupKey); it != m_albumByGroup.end()) {
            albumIdx = *it;
            Album &a = m_albums[albumIdx];
            if (!a.year) a.year = t.year;
            if (!a.genre) a.genre = t.genre;
        } else {
            Album a;
            a.id = QUuid::createUuid();
            a.groupKey = groupKey;
            a.title = t.albumTitle;
            a.artist = t.albumArtist.value_or(t.artist);
            a.year = t.year; a.genre = t.genre;
            a.dateAdded = now;
            a.missingMetadata = !m.album;
            m_albums.append(a);
            albumIdx = m_albums.size() - 1;
            m_albumByGroup[groupKey] = albumIdx;
            m_albumIndex[a.id] = albumIdx;
        }
        Album &album = m_albums[albumIdx];
        album.trackIDs.append(t.id);
        if (!album.artworkFile && !m.picture.isEmpty()) {
            if (auto saved = ArtworkStore::save(m.picture, album.id)) {
                album.artworkFile = saved->fileName;
                album.artworkPixelWidth = saved->width; album.artworkPixelHeight = saved->height;
                album.tintHex = saved->tintHex;
                ++album.artworkVersion;
            }
        }
        touched.insert(album.id);
        m_tracks.append(t);
        m_trackIndex[t.id] = m_tracks.size() - 1;
        m_knownPaths.insert(path);
        m_knownFingerprints.insert(t.fingerprint);
        ++report.added;
    }
    for (Album &a : m_albums) if (touched.contains(a.id)) finalizeAlbum(a);
    report.albums = touched.size();
    rebuildIndexes();
    if (progress) progress(audio.size(), audio.size(), QString());
    return report;
}

bool LibraryStore::importPaths(const QStringList &paths) {
    if (m_importing.exchange(true)) {
        emit importFinished("An import is already running. Try again when it finishes.", QString());
        return false;
    }
    emit importProgress(0, 0, "Looking for audio files…");
    // Metadata is read off the UI thread; the store is mutated back on this thread.
    auto *watcher = new QFutureWatcher<QVector<ScannedFile>>(this);
    QStringList roots = paths;
    auto future = QtConcurrent::run([this, roots]() {
        QStringList audio, unsupported;
        LibraryScanner::enumerate(roots, &audio, &unsupported);
        QVector<ScannedFile> out;
        for (int i = 0; i < audio.size(); ++i) {
            QMetaObject::invokeMethod(this, [this, i, n = audio.size(), name = QFileInfo(audio[i]).fileName()] {
                emit importProgress(i, n, name);
            }, Qt::QueuedConnection);
            out.append({audio[i], LibraryScanner::read(audio[i])});
        }
        for (const QString &u : unsupported) out.append({u, {std::nullopt, QString(), true}});
        return out;
    });
    connect(watcher, &QFutureWatcher<QVector<ScannedFile>>::finished, this, [this, watcher, roots] {
        watcher->deleteLater();
        // Apply: reuse importNow's grouping by feeding it prescanned results through a cache.
        const QVector<ScannedFile> scanned = watcher->result();
        QHash<QString, LibraryScanner::Result> cache;
        for (const ScannedFile &f : scanned) cache.insert(f.path, f.result);
        m_prescanned = std::move(cache);
        const ImportReport report = importNow(roots);
        m_prescanned.clear();
        m_importing = false;
        saveNow();
        emit changed();
        emit importFinished(report.headline(), report.details());
    });
    watcher->setFuture(future);
    return true;
}

// ---------- edits ----------

void LibraryStore::removeTracks(const QVector<QUuid> &trackIDs) {
    const QSet<QUuid> remove(trackIDs.begin(), trackIDs.end());
    QSet<QUuid> affected;
    for (Album &a : m_albums) {
        const int before = a.trackIDs.size();
        a.trackIDs.erase(std::remove_if(a.trackIDs.begin(), a.trackIDs.end(), [&](const QUuid &id) { return remove.contains(id); }), a.trackIDs.end());
        if (a.trackIDs.size() != before) affected.insert(a.id);
    }
    for (const Track &t : m_tracks)
        if (remove.contains(t.id) && t.isDemo) QFile::remove(t.path);   // generated demo audio only
    m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(), [&](const Track &t) { return remove.contains(t.id); }), m_tracks.end());
    for (const Album &a : m_albums)
        if (a.trackIDs.isEmpty() && a.artworkFile) ArtworkStore::remove(*a.artworkFile);
    m_albums.erase(std::remove_if(m_albums.begin(), m_albums.end(), [](const Album &a) { return a.trackIDs.isEmpty(); }), m_albums.end());
    rebuildIndexes();
    for (Album &a : m_albums) if (affected.contains(a.id)) finalizeAlbum(a);
    saveNow();
    emit tracksRemoved(trackIDs);
    emit changed();
}

void LibraryStore::removeAlbums(const QVector<QUuid> &albumIDs) {
    QVector<QUuid> trackIDs;
    for (const QUuid &id : albumIDs) if (const Album *a = album(id)) trackIDs += a->trackIDs;
    removeTracks(trackIDs);
}

void LibraryStore::removeAll() {
    QVector<QUuid> ids;
    for (const Album &a : m_albums) ids.append(a.id);
    removeAlbums(ids);
}

void LibraryStore::markPlayed(const QUuid &trackID) {
    for (Album &a : m_albums)
        if (a.trackIDs.contains(trackID)) { a.lastPlayed = QDateTime::currentDateTimeUtc(); scheduleSave(); return; }
}

bool LibraryStore::setArtwork(const QUuid &albumID, const QByteArray &data, QString *error) {
    auto it = m_albumIndex.find(albumID);
    if (it == m_albumIndex.end()) return false;
    auto saved = ArtworkStore::save(data, albumID, error);
    if (!saved) return false;
    Album &a = m_albums[*it];
    a.artworkFile = saved->fileName;
    a.artworkPixelWidth = saved->width; a.artworkPixelHeight = saved->height;
    a.tintHex = saved->tintHex;
    ++a.artworkVersion;
    saveNow();
    emit changed();
    return true;
}

void LibraryStore::removeArtwork(const QUuid &albumID) {
    auto it = m_albumIndex.find(albumID);
    if (it == m_albumIndex.end()) return;
    Album &a = m_albums[*it];
    if (a.artworkFile) ArtworkStore::remove(*a.artworkFile);
    a.artworkFile.reset(); a.tintHex.reset(); a.artworkPixelWidth.reset(); a.artworkPixelHeight.reset();
    ++a.artworkVersion;
    saveNow();
    emit changed();
}

void LibraryStore::updateAlbumInfo(const QUuid &albumID, const QString &title, const QString &artist, std::optional<int> year, std::optional<QString> genre) {
    auto it = m_albumIndex.find(albumID);
    if (it == m_albumIndex.end()) return;
    Album &a = m_albums[*it];
    a.title = title.trimmed(); a.artist = artist.trimmed(); a.year = year; a.genre = genre;
    a.userEdited = true;
    scheduleSave();
    emit changed();
}

void LibraryStore::updateAppearance(const QUuid &albumID, const std::optional<AlbumAppearance> &appearance) {
    auto it = m_albumIndex.find(albumID);
    if (it == m_albumIndex.end()) return;
    m_albums[*it].appearance = (appearance && appearance->isDefault()) ? std::nullopt : appearance;
    scheduleSave();
    emit changed();
}
