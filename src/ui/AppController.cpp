#include "AppController.h"
#include "AlbumModel.h"
#include "ArtworkImageProvider.h"
#include "audio/Player.h"
#include "audio/PlaybackEngine.h"
#include "core/Artwork.h"
#include "core/LibraryStore.h"
#include "core/Palette.h"
#include "core/Paths.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <cmath>

AppController::AppController(LibraryStore *library, PlaybackEngine *engine, Player *player, ArtworkImageProvider *images, QObject *parent)
    : QObject(parent), m_library(library), m_engine(engine), m_player(player),
      m_albums(new AlbumModel(library, this)), m_images(images) {
    connect(library, &LibraryStore::changed, this, &AppController::syncImages);
    connect(library, &LibraryStore::changed, this, &AppController::queueItemsChanged);
    connect(library, &LibraryStore::importProgress, this, [this](int done, int total, const QString &current) {
        m_importStatus = total > 0 ? QString("Importing %1 of %2 — %3").arg(done + 1).arg(total).arg(current) : current;
        emit importChanged();
    });
    connect(library, &LibraryStore::importFinished, this, [this](const QString &headline, const QString &details) {
        m_importStatus.clear();
        emit importChanged();
        const bool good = !headline.startsWith("No ") && !headline.startsWith("Nothing") && !headline.startsWith("An import");
        showBanner(headline, details, good ? "success" : "warning");
        if (browsedAlbumId().isEmpty() && m_albums->rowCount() > 0) setBrowsedAlbumId(m_albums->idAt(0));
    });
    connect(library, &LibraryStore::tracksRemoved, player, &Player::purge);
    connect(player, &Player::queueChanged, this, &AppController::queueItemsChanged);
    connect(player, &Player::currentChanged, this, &AppController::loadLyricsForCurrent);
    connect(player, &Player::currentChanged, this, &AppController::currentArtworkChanged);
    connect(library, &LibraryStore::changed, this, &AppController::currentArtworkChanged);
    connect(player, &Player::errorChanged, this, [this] {
        if (!m_player->errorMessage().isEmpty()) showBanner(m_player->errorMessage(), QString(), "error");
    });
    syncImages();
    loadLyricsForCurrent();
}

void AppController::syncImages() {
    QHash<QUuid, ArtworkImageProvider::Entry> entries;
    for (const Album &a : m_library->albums()) entries.insert(a.id, {a.artworkFile.value_or(QString()), a.title, a.artist});
    m_images->update(entries);
}

void AppController::setBrowsedAlbumId(const QString &id) {
    if (id == browsedAlbumId()) return;
    m_settings.setValue("browsedAlbum", id);
    emit browsedChanged();
}

bool AppController::importing() const { return m_library->isImporting(); }

void AppController::showBanner(const QString &text, const QString &details, const QString &tone) {
    m_bannerText = text; m_bannerDetails = details; m_bannerTone = tone;
    const int serial = ++m_bannerSerial;
    emit bannerChanged();
    // success/warning 6 s (14 s with details), errors 9 s.
    const int ms = tone == "error" ? 9000 : details.isEmpty() ? 6000 : 14000;
    QTimer::singleShot(ms, this, [this, serial] { if (serial == m_bannerSerial) dismissBanner(); });
}

void AppController::dismissBanner() {
    m_bannerText.clear(); m_bannerDetails.clear();
    emit bannerChanged();
}

QString AppController::formatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int s = int(seconds);
    if (s >= 3600) return QString("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// ---------- library ----------

void AppController::importUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &u : urls) if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty()) m_library->importPaths(paths);
    emit importChanged();
}

QVariantList AppController::albumTracks(const QString &albumId) const {
    QVariantList out;
    const Album *album = m_library->album(QUuid::fromString(albumId));
    if (!album) return out;
    const auto tracks = m_library->tracksOf(*album);
    QSet<int> discs;
    for (const Track *t : tracks) discs.insert(t->discNumber.value_or(1));
    int position = 0;
    for (const Track *t : tracks) {
        ++position;
        out.append(QVariantMap{
            {"trackId", t->id.toString(QUuid::WithoutBraces)},
            {"index", position - 1},
            {"number", t->trackNumber.value_or(position)},
            {"disc", t->discNumber.value_or(1)},
            {"multiDisc", discs.size() > 1},
            {"title", t->title},
            {"artist", t->artist},
            {"showArtist", Text::normalizedKey(t->artist) != Text::normalizedKey(album->artist)},
            {"duration", formatTime(t->duration)},
            {"seconds", t->duration},
            {"missing", !QFileInfo::exists(t->path)},
            {"path", QDir::toNativeSeparators(t->path)},
            {"isDemo", t->isDemo},
        });
    }
    return out;
}

QVariantMap AppController::albumInfo(const QString &albumId) const {
    const Album *a = m_library->album(QUuid::fromString(albumId));
    if (!a) return {};
    double total = 0;
    for (const Track *t : m_library->tracksOf(*a)) total += t->duration;
    QStringList meta;
    if (a->genre && *a->genre != "Demo") meta << *a->genre;
    if (a->year) meta << QString::number(*a->year);
    return {{"albumId", albumId}, {"title", a->title}, {"artist", a->artist}, {"year", a->year ? QString::number(*a->year) : QString()},
            {"genre", a->genre.value_or(QString())}, {"meta", meta.join(" · ")}, {"isDemo", a->isDemo},
            {"missingMetadata", a->missingMetadata}, {"hasArtwork", a->artworkFile.has_value()},
            {"artwork", QString("image://artwork/%1/%2").arg(albumId).arg(a->artworkVersion)},
            {"trackCount", int(a->trackIDs.size())},
            {"footer", QString("%1 %2, %3").arg(a->trackIDs.size()).arg(a->trackIDs.size() == 1 ? "song" : "songs", AlbumModel::formatLongDuration(total))}};
}

QVariantMap AppController::albumPalette(const QString &albumId) const {
    const Album *a = m_library->album(QUuid::fromString(albumId));
    if (!a) return {};
    Palette::AlbumPalette p = a->artworkFile
        ? Palette::extract(ArtworkStore::decode(*a->artworkFile, 720))
        : Palette::fallback(a->tintHex.value_or(QString()), a->title, a->artist);
    if (a->appearance)
        p = Palette::customized(p, a->appearance->backgroundHex.value_or(QString()), a->appearance->textHex.value_or(QString()),
                                a->appearance->accentHex.value_or(QString()));
    return {{"background", p.background}, {"foreground", p.foreground}, {"secondary", p.secondary}, {"accent", p.accent}, {"dark", p.isDark()}};
}

QString AppController::backdropTint(const QString &albumId, bool dark) const {
    const Album *a = m_library->album(QUuid::fromString(albumId));
    if (!a) return {};
    const QColor base = a->tintHex ? QColor(*a->tintHex) : ArtworkStore::placeholderColor(a->title, a->artist);
    return Palette::themeTint(base, dark).name();
}

void AppController::removeAlbums(const QStringList &albumIds) {
    QVector<QUuid> ids;
    for (const QString &s : albumIds) ids.append(QUuid::fromString(s));
    m_library->removeAlbums(ids);
}

void AppController::removeTrack(const QString &trackId) { m_library->removeTracks({QUuid::fromString(trackId)}); }

void AppController::revealTrack(const QString &trackId) {
    const Track *t = m_library->track(QUuid::fromString(trackId));
    if (!t) return;
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(t->path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(t->path).absolutePath()));
#endif
}

void AppController::replaceArtwork(const QString &albumId, const QUrl &file) {
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::ReadOnly)) { showBanner("That image couldn’t be opened.", QString(), "warning"); return; }
    QString error;
    if (!m_library->setArtwork(QUuid::fromString(albumId), f.readAll(), &error)) showBanner(error, QString(), "warning");
    else emit artworkSaved();
}

void AppController::removeArtwork(const QString &albumId) { m_library->removeArtwork(QUuid::fromString(albumId)); }

void AppController::updateAlbumInfo(const QString &albumId, const QString &title, const QString &artist, const QString &year, const QString &genre) {
    const Album *a = m_library->album(QUuid::fromString(albumId));
    if (!a) return;
    bool ok = false;
    const int y = year.trimmed().toInt(&ok);
    m_library->updateAlbumInfo(a->id, title.trimmed().isEmpty() ? a->title : title, artist.trimmed().isEmpty() ? a->artist : artist,
                               ok && y > 0 ? std::optional<int>(y) : std::nullopt,
                               genre.trimmed().isEmpty() ? std::nullopt : std::optional<QString>(genre.trimmed()));
}

// ---------- playback ----------

QVector<QUuid> AppController::tracksForAlbums(const QStringList &albumIds) const {
    QVector<QUuid> out;
    for (const QString &id : orderedSelection(albumIds))
        if (const Album *a = m_library->album(QUuid::fromString(id))) out += a->trackIDs;
    return out;
}

QStringList AppController::orderedSelection(const QStringList &albumIds) const {
    // Selected albums in the displayed order.
    const QSet<QString> wanted(albumIds.begin(), albumIds.end());
    QStringList out;
    for (const QUuid &id : m_albums->orderedIds()) {
        const QString s = id.toString(QUuid::WithoutBraces);
        if (wanted.contains(s)) out << s;
    }
    return out;
}

void AppController::playAlbum(const QString &albumId, int startIndex, bool shuffle) {
    m_player->playAlbum(QUuid::fromString(albumId), shuffle ? std::nullopt : std::optional<int>(startIndex), shuffle ? std::optional<bool>(true) : std::nullopt);
}

void AppController::toggleAlbum(const QString &albumId) {
    if (m_player->currentAlbumIdString() == albumId) m_player->togglePlayPause();
    else playAlbum(albumId);
}

void AppController::playAlbums(const QStringList &ids) { const auto t = tracksForAlbums(ids); if (!t.isEmpty()) m_player->setQueue(t, 0, true); }
void AppController::playNextAlbums(const QStringList &ids) { m_player->playNext(tracksForAlbums(ids)); }
void AppController::addAlbumsToQueue(const QStringList &ids) { m_player->addToQueue(tracksForAlbums(ids)); }
void AppController::playTrackNext(const QString &id) { m_player->playNext({QUuid::fromString(id)}); }
void AppController::addTrackToQueue(const QString &id) { m_player->addToQueue({QUuid::fromString(id)}); }

QVariantList AppController::queueItems() const {
    QVariantList out;
    const auto &q = m_player->queue();
    for (int i = 0; i < q.size(); ++i) {
        const Track *t = m_library->track(q[i]);
        if (!t) continue;
        QString albumId;
        int version = 0;
        for (const Album &a : m_library->albums()) if (a.trackIDs.contains(t->id)) { albumId = a.id.toString(QUuid::WithoutBraces); version = a.artworkVersion; break; }
        out.append(QVariantMap{{"queueIndex", i}, {"trackId", t->id.toString(QUuid::WithoutBraces)}, {"title", t->title},
                               {"artist", t->artist}, {"duration", formatTime(t->duration)},
                               {"artwork", QString("image://artwork/%1/%2").arg(albumId).arg(version)}});
    }
    return out;
}

QString AppController::currentArtwork() const {
    const QString id = m_player->currentAlbumIdString();
    const Album *a = m_library->album(QUuid::fromString(id));
    return a ? QString("image://artwork/%1/%2").arg(id).arg(a->artworkVersion) : QString();
}

// ---------- lyrics ----------

QVariantList AppController::lyricLines() const {
    QVariantList out;
    for (const LyricCue &c : m_lyrics.cues)
        out.append(QVariantMap{{"text", c.text.isEmpty() ? QStringLiteral("♪") : c.text}, {"time", m_lyrics.playbackTime(c)}});
    return out;
}

double AppController::lyricsSeekTime(int cue) const {
    return cue >= 0 && cue < m_lyrics.cues.size() ? m_lyrics.playbackTime(m_lyrics.cues[cue]) : 0;
}

void AppController::loadLyricsForCurrent() {
    const auto id = m_player->currentTrackID();
    if (id && *id == m_lyricsTrack) return;
    m_lyricsTrack = id.value_or(QUuid());
    reloadLyrics();
}

void AppController::reloadLyrics() {
    m_lyrics = {};
    m_lyricsSource.clear();
    m_lyricsError.clear();
    if (const Track *t = m_library->track(m_lyricsTrack)) {
        QString error;
        if (auto found = LyricsStore::find(t->id, t->path, &error)) {
            m_lyrics = found->document;
            m_lyricsSource = found->imported ? QStringLiteral("Saved lyrics") : QFileInfo(found->sourcePath).fileName();
        } else {
            m_lyricsError = error;
        }
    }
    emit lyricsChanged();
}

QString AppController::currentTrackFolder() const {
    const Track *t = m_library->track(m_lyricsTrack);
    return t ? QUrl::fromLocalFile(QFileInfo(t->path).absolutePath()).toString() : QString();
}

void AppController::importLyricsFile(const QUrl &file) {
    if (m_lyricsTrack.isNull()) return;
    QString error;
    if (LyricsStore::importFile(file.toLocalFile(), m_lyricsTrack, &error)) reloadLyrics();
    else showBanner(error, QString(), "warning");
}

void AppController::searchLyrics(const QString &title, const QString &artist) {
    const Track *t = m_library->track(m_lyricsTrack);
    m_lyricSearchTrack = m_lyricsTrack;
    m_lookupBusy = true;
    m_lookupMessage = QStringLiteral("Searching…");
    m_lyricMatches.clear();
    const int serial = ++m_lookupSerial;
    emit lookupChanged();
    m_lookup.lyrics(title, artist, t ? t->duration : 0, [this, serial](QVector<LyricMatch> matches, QString error) {
        if (serial != m_lookupSerial) return;
        m_lookupBusy = false;
        m_lyricMatches = matches;
        m_lookupMessage = error;
        emit lookupChanged();
    });
}

QVariantList AppController::lyricResults() const {
    QVariantList out;
    for (const LyricMatch &m : m_lyricMatches)
        out.append(QVariantMap{{"track", m.trackName}, {"detail", m.artistName + (m.albumName.isEmpty() ? QString() : " · " + m.albumName)},
                               {"info", formatTime(m.duration) + " · " + (m.isSynced() ? "Synced LRC" : "Plain lyrics")},
                               {"synced", m.isSynced()}});
    return out;
}

QString AppController::lyricPreview(int index) const {
    if (index < 0 || index >= m_lyricMatches.size()) return {};
    const LyricsDocument doc = LyricsDocument::parse(m_lyricMatches[index].text());
    if (!doc.isSynced()) return doc.plainText;
    QStringList lines;
    for (int i = 0; i < doc.cues.size() && i < 8; ++i) lines << QString("%1  %2").arg(formatTime(doc.cues[i].time), doc.cues[i].text);
    return lines.join('\n');
}

void AppController::useLyricResult(int index) {
    if (index < 0 || index >= m_lyricMatches.size() || m_lyricSearchTrack.isNull()) return;
    QString error;
    if (LyricsStore::importText(m_lyricMatches[index].text(), m_lyricSearchTrack, &error)) {
        if (m_lyricSearchTrack == m_lyricsTrack) reloadLyrics();
        emit lyricsSaved();
    } else {
        m_lookupMessage = error;
        emit lookupChanged();
    }
}

// ---------- artwork lookup ----------

void AppController::searchArtwork(const QString &title, const QString &artist) {
    m_lookupBusy = true;
    m_lookupMessage = QStringLiteral("Searching…");
    m_artworkMatches.clear();
    m_artworkPreview.clear(); m_artworkData.clear();
    const int serial = ++m_lookupSerial;
    emit lookupChanged();
    m_lookup.artwork(title, artist, [this, serial](QVector<ArtworkMatch> matches, QString error) {
        if (serial != m_lookupSerial) return;
        m_lookupBusy = false;
        m_artworkMatches = matches;
        m_lookupMessage = error;
        emit lookupChanged();
    });
}

QVariantList AppController::artworkResults() const {
    QVariantList out;
    for (const ArtworkMatch &m : m_artworkMatches) out.append(QVariantMap{{"title", m.title}, {"artist", m.artist}, {"year", m.year}});
    return out;
}

void AppController::previewArtwork(int index) {
    if (index < 0 || index >= m_artworkMatches.size()) return;
    m_lookupBusy = true;
    m_lookupMessage = QStringLiteral("Loading cover…");
    m_artworkPreview.clear(); m_artworkData.clear();
    const int serial = ++m_lookupSerial;
    emit lookupChanged();
    m_lookup.cover(m_artworkMatches[index].id, [this, serial](QByteArray data, QString error) {
        if (serial != m_lookupSerial) return;
        m_lookupBusy = false;
        m_lookupMessage = error;
        if (error.isEmpty()) {
            m_artworkData = data;
            m_artworkPreview = "data:image/jpeg;base64," + QString::fromLatin1(data.toBase64());
        }
        emit lookupChanged();
    });
}

void AppController::useArtworkPreview(const QString &albumId) {
    if (m_artworkData.isEmpty()) return;
    QString error;
    if (m_library->setArtwork(QUuid::fromString(albumId), m_artworkData, &error)) emit artworkSaved();
    else { m_lookupMessage = error; emit lookupChanged(); }
}

void AppController::clearLookup() {
    ++m_lookupSerial;
    m_lookupBusy = false;
    m_lookupMessage.clear();
    m_lyricMatches.clear(); m_artworkMatches.clear(); m_artworkData.clear(); m_artworkPreview.clear();
    emit lookupChanged();
}
