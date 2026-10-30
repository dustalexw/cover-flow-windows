#pragma once
#include "core/Model.h"
#include <QObject>
#include <QTimer>
#include <QHash>
#include <optional>

class LibraryStore;
class PlaybackEngine;

// PlayerEngine.swift: queue, shuffle/repeat, transport rules and Playback.json.
class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ isPlaying NOTIFY stateChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY currentChanged)
    Q_PROPERTY(QString currentTrackId READ currentTrackIdString NOTIFY currentChanged)
    Q_PROPERTY(QString currentAlbumId READ currentAlbumIdString NOTIFY currentChanged)
    Q_PROPERTY(QString title READ title NOTIFY currentChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY currentChanged)
    Q_PROPERTY(QString albumTitle READ albumTitle NOTIFY currentChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY currentChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modesChanged)
    Q_PROPERTY(int repeatMode READ repeatModeInt WRITE setRepeatModeInt NOTIFY modesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY queueChanged)
    Q_PROPERTY(int queueLength READ queueLength NOTIFY queueChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
public:
    Player(LibraryStore *library, PlaybackEngine *engine, QObject *parent = nullptr);

    // Queue setup
    void playAlbum(const QUuid &albumID, std::optional<int> startAt = std::nullopt, std::optional<bool> shuffled = std::nullopt);
    void setQueue(const QVector<QUuid> &tracks, int startIndex, bool autoplay = true);
    void playNext(const QVector<QUuid> &tracks);
    void addToQueue(const QVector<QUuid> &tracks);
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void skip(double delta) { seek(position() + delta); }
    Q_INVOKABLE void stop();
    Q_INVOKABLE void jumpTo(int queueIndex);
    Q_INVOKABLE void removeFromQueue(int queueIndex);
    Q_INVOKABLE void moveInQueue(int from, int to);
    Q_INVOKABLE void clearUpNext();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void volumeStep(double delta);
    void purge(const QVector<QUuid> &removedTracks);

    const QVector<QUuid> &queue() const { return m_queue; }
    std::optional<QUuid> currentTrackID() const;
    bool isPlaying() const;
    bool hasTrack() const { return currentTrackID().has_value(); }
    QString currentTrackIdString() const;
    QString currentAlbumIdString() const;
    QString title() const;
    QString artist() const;
    QString albumTitle() const;
    double position() const;
    double duration() const;
    double volume() const { return m_volume; }
    void setVolume(double v);
    bool isMuted() const { return m_muted; }
    void setMuted(bool m);
    bool shuffle() const { return m_unshuffled.has_value(); }
    void setShuffle(bool on);
    RepeatMode repeatMode() const { return m_repeat; }
    int repeatModeInt() const { return int(m_repeat); }
    void setRepeatModeInt(int m);
    int currentIndex() const { return m_index ? *m_index : -1; }
    int queueLength() const { return int(m_queue.size()); }
    QString errorMessage() const { return m_error; }

    // Successor for automatic advance (PlaybackOrder.successor).
    static std::optional<int> successor(int index, int count, RepeatMode mode, bool automatic);

    void restore();
    void saveNow();

signals:
    void stateChanged();
    void currentChanged();
    void positionChanged();
    void volumeChanged();
    void modesChanged();
    void queueChanged();
    void errorChanged();
    void trackStarted(const QUuid &trackID);   // for scrobbling / lastPlayed

private:
    void load(int index, bool autoplay, double at = 0);
    void refreshUpcoming();
    qint64 tokenFor(int index);
    std::optional<int> indexForToken(qint64 token) const;
    void onEngineTrackChanged(qint64 token);
    void onEngineFinished();
    void onEngineError(const QString &message);
    void applyVolume();
    void scheduleSave();
    QString pathOf(const QUuid &id) const;
    const Track *currentTrack() const;

    LibraryStore *m_library;
    PlaybackEngine *m_engine;
    QVector<QUuid> m_queue;
    std::optional<QVector<QUuid>> m_unshuffled;
    std::optional<int> m_index;
    RepeatMode m_repeat = RepeatMode::Off;
    double m_volume = 0.8;
    bool m_muted = false;
    bool m_loaded = false;          // engine has the current track
    double m_pausedAt = 0;          // restored position before first play
    QString m_error;
    int m_consecutiveFailures = 0;
    bool m_wantsPlayback = false;
    qint64 m_nextToken = 1;
    QHash<qint64, int> m_tokenIndex;
    QHash<qint64, QUuid> m_tokenTrack;
    QTimer m_saveTimer, m_periodicSave;
};
