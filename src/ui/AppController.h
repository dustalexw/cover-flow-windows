#pragma once
#include "core/Lyrics.h"
#include "core/MusicLookupClient.h"
#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QVariant>

class LibraryStore;
class PlaybackEngine;
#include "audio/Player.h"
#include "ui/AlbumModel.h"
class ArtworkImageProvider;

// Everything QML needs: library, player, settings, banners, lyrics and lookups.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(AlbumModel *albums READ albums CONSTANT)
    Q_PROPERTY(Player *player READ player CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString debugState READ debugState CONSTANT)
    // Settings (same key names as the macOS UserDefaults)
    Q_PROPERTY(int browseMode READ browseMode WRITE setBrowseMode NOTIFY settingsChanged)
    Q_PROPERTY(QString browsedAlbumId READ browsedAlbumId WRITE setBrowsedAlbumId NOTIFY browsedChanged)
    Q_PROPERTY(bool showQueue READ showQueue WRITE setShowQueue NOTIFY settingsChanged)
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(bool albumViewOpen READ albumViewOpen WRITE setAlbumViewOpen NOTIFY settingsChanged)
    Q_PROPERTY(bool albumViewKeepOnTop READ albumViewKeepOnTop WRITE setAlbumViewKeepOnTop NOTIFY settingsChanged)
    // Import + banners
    Q_PROPERTY(bool importing READ importing NOTIFY importChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importChanged)
    Q_PROPERTY(QString bannerText READ bannerText NOTIFY bannerChanged)
    Q_PROPERTY(QString bannerDetails READ bannerDetails NOTIFY bannerChanged)
    Q_PROPERTY(QString bannerTone READ bannerTone NOTIFY bannerChanged)
    // Queue
    Q_PROPERTY(QVariantList queueItems READ queueItems NOTIFY queueItemsChanged)
    Q_PROPERTY(QString currentArtwork READ currentArtwork NOTIFY currentArtworkChanged)
    // Lyrics for the current track
    Q_PROPERTY(QVariantList lyricLines READ lyricLines NOTIFY lyricsChanged)
    Q_PROPERTY(QString lyricsPlain READ lyricsPlain NOTIFY lyricsChanged)
    Q_PROPERTY(bool lyricsSynced READ lyricsSynced NOTIFY lyricsChanged)
    Q_PROPERTY(bool hasLyrics READ hasLyrics NOTIFY lyricsChanged)
    Q_PROPERTY(QString lyricsSource READ lyricsSource NOTIFY lyricsChanged)
    Q_PROPERTY(QString lyricsError READ lyricsError NOTIFY lyricsChanged)
    // Online lookups
    Q_PROPERTY(bool lookupBusy READ lookupBusy NOTIFY lookupChanged)
    Q_PROPERTY(QString lookupMessage READ lookupMessage NOTIFY lookupChanged)
    Q_PROPERTY(QVariantList lyricResults READ lyricResults NOTIFY lookupChanged)
    Q_PROPERTY(QVariantList artworkResults READ artworkResults NOTIFY lookupChanged)
    Q_PROPERTY(QString artworkPreview READ artworkPreview NOTIFY lookupChanged)
public:
    AppController(LibraryStore *library, PlaybackEngine *engine, Player *player, ArtworkImageProvider *images, QObject *parent = nullptr);

    AlbumModel *albums() const { return m_albums; }
    Player *player() const { return m_player; }
    QString version() const { return QStringLiteral(COVERFLOW_VERSION); }
    QString debugState() const { return qEnvironmentVariable("COVERFLOW_DEBUG_STATE"); }

    int browseMode() const { return m_settings.value("browseMode", 0).toInt(); }
    void setBrowseMode(int m) { m_settings.setValue("browseMode", m); emit settingsChanged(); }
    QString browsedAlbumId() const { return m_settings.value("browsedAlbum").toString(); }
    void setBrowsedAlbumId(const QString &id);
    bool showQueue() const { return m_settings.value("showQueue", false).toBool(); }
    void setShowQueue(bool v) { m_settings.setValue("showQueue", v); emit settingsChanged(); }
    int theme() const { return m_settings.value("theme", 0).toInt(); }
    void setTheme(int t) { m_settings.setValue("theme", t); emit settingsChanged(); }
    bool albumViewOpen() const { return m_settings.value("albumView.open", false).toBool(); }
    void setAlbumViewOpen(bool v) { m_settings.setValue("albumView.open", v); emit settingsChanged(); }
    bool albumViewKeepOnTop() const { return m_settings.value("albumView.keepOnTop", true).toBool(); }
    void setAlbumViewKeepOnTop(bool v) { m_settings.setValue("albumView.keepOnTop", v); emit settingsChanged(); }

    bool importing() const;
    QString importStatus() const { return m_importStatus; }
    QString bannerText() const { return m_bannerText; }
    QString bannerDetails() const { return m_bannerDetails; }
    QString bannerTone() const { return m_bannerTone; }
    QVariantList queueItems() const;
    QString currentArtwork() const;

    QVariantList lyricLines() const;
    QString lyricsPlain() const { return m_lyrics.plainText; }
    bool lyricsSynced() const { return m_lyrics.isSynced(); }
    bool hasLyrics() const { return !m_lyrics.isEmpty(); }
    QString lyricsSource() const { return m_lyricsSource; }
    QString lyricsError() const { return m_lyricsError; }

    bool lookupBusy() const { return m_lookupBusy; }
    QString lookupMessage() const { return m_lookupMessage; }
    QVariantList lyricResults() const;
    QVariantList artworkResults() const;
    QString artworkPreview() const { return m_artworkPreview; }

    // Library
    Q_INVOKABLE void importUrls(const QList<QUrl> &urls);
    Q_INVOKABLE QVariantList albumTracks(const QString &albumId) const;
    Q_INVOKABLE QVariantMap albumInfo(const QString &albumId) const;
    Q_INVOKABLE QVariantMap albumPalette(const QString &albumId) const;
    Q_INVOKABLE QString backdropTint(const QString &albumId, bool dark) const;
    Q_INVOKABLE void removeAlbums(const QStringList &albumIds);
    Q_INVOKABLE void removeTrack(const QString &trackId);
    Q_INVOKABLE void revealTrack(const QString &trackId);
    Q_INVOKABLE void replaceArtwork(const QString &albumId, const QUrl &file);
    Q_INVOKABLE void removeArtwork(const QString &albumId);
    Q_INVOKABLE void updateAlbumInfo(const QString &albumId, const QString &title, const QString &artist, const QString &year, const QString &genre);
    Q_INVOKABLE void dismissBanner();

    // Playback
    Q_INVOKABLE void playAlbum(const QString &albumId, int startIndex = 0, bool shuffle = false);
    Q_INVOKABLE void toggleAlbum(const QString &albumId);
    Q_INVOKABLE void playAlbums(const QStringList &albumIds);
    Q_INVOKABLE void playNextAlbums(const QStringList &albumIds);
    Q_INVOKABLE void addAlbumsToQueue(const QStringList &albumIds);
    Q_INVOKABLE void playTrackNext(const QString &trackId);
    Q_INVOKABLE void addTrackToQueue(const QString &trackId);
    Q_INVOKABLE QStringList orderedSelection(const QStringList &albumIds) const;

    // Lyrics
    Q_INVOKABLE int lyricsActiveIndex(double position) const { return m_lyrics.activeIndex(position); }
    Q_INVOKABLE double lyricsSeekTime(int cue) const;
    Q_INVOKABLE void reloadLyrics();
    Q_INVOKABLE void importLyricsFile(const QUrl &file);
    Q_INVOKABLE void searchLyrics(const QString &title, const QString &artist);
    Q_INVOKABLE QString lyricPreview(int index) const;
    Q_INVOKABLE void useLyricResult(int index);
    Q_INVOKABLE QString currentTrackFolder() const;

    // Artwork lookup
    Q_INVOKABLE void searchArtwork(const QString &title, const QString &artist);
    Q_INVOKABLE void previewArtwork(int index);
    Q_INVOKABLE void useArtworkPreview(const QString &albumId);
    Q_INVOKABLE void clearLookup();

    Q_INVOKABLE static QString formatTime(double seconds);

signals:
    void settingsChanged();
    void browsedChanged();
    void importChanged();
    void bannerChanged();
    void queueItemsChanged();
    void currentArtworkChanged();
    void lyricsChanged();
    void lookupChanged();
    void lyricsSaved();
    void artworkSaved();

private:
    void showBanner(const QString &text, const QString &details, const QString &tone);
    void syncImages();
    void loadLyricsForCurrent();
    QVector<QUuid> tracksForAlbums(const QStringList &albumIds) const;

    LibraryStore *m_library;
    PlaybackEngine *m_engine;
    Player *m_player;
    AlbumModel *m_albums;
    ArtworkImageProvider *m_images;
    MusicLookupClient m_lookup;
    QSettings m_settings;

    QString m_importStatus;
    QString m_bannerText, m_bannerDetails, m_bannerTone;
    int m_bannerSerial = 0;

    LyricsDocument m_lyrics;
    QString m_lyricsSource, m_lyricsError;
    QUuid m_lyricsTrack;

    bool m_lookupBusy = false;
    int m_lookupSerial = 0;
    QString m_lookupMessage;
    QVector<LyricMatch> m_lyricMatches;
    QUuid m_lyricSearchTrack;
    QVector<ArtworkMatch> m_artworkMatches;
    QByteArray m_artworkData;
    QString m_artworkPreview;
};
