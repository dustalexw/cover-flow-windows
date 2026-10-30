#pragma once
#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QUrl>
#include <QUuid>
#include <QVector>
#include <deque>
#include <functional>

class QNetworkAccessManager;

struct LyricMatch {
    int id = 0;
    QString trackName, artistName, albumName;
    double duration = 0;
    bool instrumental = false;
    QString plainLyrics, syncedLyrics;
    bool isSynced() const { return !syncedLyrics.trimmed().isEmpty(); }
    QString text() const { return isSynced() ? syncedLyrics : plainLyrics; }
};

struct ArtworkMatch { QUuid id; QString title, artist, year; };

// MusicLookupClient.swift. User-initiated lookups only, run one at a time with
// per-host spacing; 429/503 honour Retry-After and short waits are retried quietly.
class MusicLookupClient : public QObject {
    Q_OBJECT
public:
    static constexpr int MaxRetries = 3;
    static constexpr int MaxAutoRetryWait = 5;

    explicit MusicLookupClient(QNetworkAccessManager *nam = nullptr, QObject *parent = nullptr);

    using LyricsCallback = std::function<void(QVector<LyricMatch>, QString error)>;
    using ArtworkCallback = std::function<void(QVector<ArtworkMatch>, QString error)>;
    using DataCallback = std::function<void(QByteArray, QString error)>;

    void lyrics(const QString &title, const QString &artist, double duration, LyricsCallback done);
    void artwork(const QString &title, const QString &artist, ArtworkCallback done);
    void cover(const QUuid &releaseGroup, DataCallback done);

    static QUrl lyricsURL(const QString &title, const QString &artist);
    static QUrl artworkURL(const QString &title, const QString &artist);
    static QString userAgent();
    // Ranking used for LRCLIB results (exposed for tests).
    static QVector<LyricMatch> rankLyrics(QVector<LyricMatch> matches, double duration);
    // Tests can shrink the waits.
    void setTimeScale(double scale) { m_timeScale = scale; }

private:
    struct Request { QUrl url; qint64 limit; DataCallback done; int attempt = 0; };
    void enqueue(Request request);
    void pump();
    void perform(Request request);
    QString errorFor(int status, const QUrl &url) const;

    QNetworkAccessManager *m_nam;
    std::deque<Request> m_queue;
    bool m_busy = false;
    QHash<QString, QDateTime> m_nextRequest;
    double m_timeScale = 1.0;
};
