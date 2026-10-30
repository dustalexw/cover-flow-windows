#pragma once
#include "Model.h"
#include "LibraryScanner.h"
#include <QObject>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QJsonObject>
#include <atomic>

struct ImportReport {
    int added = 0, albums = 0, duplicates = 0, found = 0;
    QStringList unsupported, failed;
    QString headline() const;
    QString details() const;
};

// LibraryStore.swift: the collection, its grouping rules and Library.json.
class LibraryStore : public QObject {
    Q_OBJECT
public:
    explicit LibraryStore(QObject *parent = nullptr);

    bool load(QString *notice = nullptr);
    void saveNow();
    void scheduleSave();          // debounced 0.6 s

    const QVector<Album> &albums() const { return m_albums; }
    const Album *album(const QUuid &id) const;
    const Track *track(const QUuid &id) const;
    QVector<const Track *> tracksOf(const Album &album) const;

    QVector<const Album *> visibleAlbums(const QString &search, SortOrder order) const;

    bool isImporting() const { return m_importing; }
    // Imports on a worker thread; emits importProgress and importFinished.
    bool importPaths(const QStringList &paths);
    // Synchronous core used by importPaths and tests.
    ImportReport importNow(const QStringList &paths, const std::function<void(int, int, QString)> &progress = {});

    void removeTracks(const QVector<QUuid> &trackIDs);
    void removeAlbums(const QVector<QUuid> &albumIDs);
    void removeAll();
    void markPlayed(const QUuid &trackID);
    bool setArtwork(const QUuid &albumID, const QByteArray &data, QString *error = nullptr);
    void removeArtwork(const QUuid &albumID);
    void updateAlbumInfo(const QUuid &albumID, const QString &title, const QString &artist, std::optional<int> year, std::optional<QString> genre);
    void updateAppearance(const QUuid &albumID, const std::optional<AlbumAppearance> &appearance);

    // JSON round trip in the macOS schema (exposed for tests).
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &root);
    static QString fingerprint(const QString &title, const QString &artist, const QString &album, double duration);

signals:
    void changed();
    void importProgress(int done, int total, const QString &current);
    void importFinished(const QString &headline, const QString &details);
    void tracksRemoved(const QVector<QUuid> &trackIDs);

private:
    void rebuildIndexes();
    void finalizeAlbum(Album &album);
    QString libraryPath() const;

    QVector<Album> m_albums;
    QVector<Track> m_tracks;
    QHash<QUuid, int> m_albumIndex, m_trackIndex;
    QHash<QString, int> m_albumByGroup;
    QSet<QString> m_knownPaths, m_knownFingerprints;
    QTimer m_saveTimer;
    std::atomic<bool> m_importing{false};
    QHash<QString, LibraryScanner::Result> m_prescanned;   // filled by the async import
};
