#include "MusicLookupClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QTimeZone>
#include <QUrlQuery>
#include <cmath>

MusicLookupClient::MusicLookupClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_nam(nam ? nam : new QNetworkAccessManager(this)) {}

QString MusicLookupClient::userAgent() {
    return QStringLiteral("CoverFlow/%1 (https://github.com/dustalexw/cover-flow)").arg(COVERFLOW_VERSION);
}

static QUrl withQuery(const QString &base, const QVector<QPair<QString, QString>> &items) {
    QStringList parts;
    for (const auto &[k, v] : items) parts << k + "=" + QString::fromLatin1(QUrl::toPercentEncoding(v));
    QUrl url(base);
    url.setQuery(parts.join('&'), QUrl::StrictMode);
    return url;
}

QUrl MusicLookupClient::lyricsURL(const QString &title, const QString &artist) {
    QVector<QPair<QString, QString>> items{{"track_name", title.trimmed()}};
    const QString a = artist.trimmed();
    if (!a.isEmpty()) items.append({"artist_name", a});
    return withQuery("https://lrclib.net/api/search", items);
}

QUrl MusicLookupClient::artworkURL(const QString &title, const QString &artist) {
    auto escaped = [](const QString &v) {
        static const QString special = R"(\+-&|!(){}[]^"~*?:/)";
        QString out;
        for (QChar c : v) { if (special.contains(c)) out += '\\'; out += c; }
        return out;
    };
    const QString a = artist.trimmed();
    const QString query = QString("releasegroup:\"%1\"").arg(escaped(title)) + (a.isEmpty() ? QString() : QString(" AND artist:\"%1\"").arg(escaped(a)));
    return withQuery("https://musicbrainz.org/ws/2/release-group/", {{"query", query}, {"fmt", "json"}, {"limit", "12"}});
}

QVector<LyricMatch> MusicLookupClient::rankLyrics(QVector<LyricMatch> matches, double duration) {
    const double reference = std::isfinite(duration) ? std::max(0.0, duration) : 0;
    matches.erase(std::remove_if(matches.begin(), matches.end(), [](const LyricMatch &m) {
        return !std::isfinite(m.duration) || m.duration < 0 || m.duration > 86400 || m.instrumental || m.text().trimmed().isEmpty();
    }), matches.end());
    std::sort(matches.begin(), matches.end(), [&](const LyricMatch &a, const LyricMatch &b) {
        const double da = std::abs(a.duration - reference), db = std::abs(b.duration - reference);
        const double ba = std::floor(da / 2), bb = std::floor(db / 2);
        if (ba != bb) return ba < bb;
        if (a.isSynced() != b.isSynced()) return a.isSynced();
        return da == db ? a.id < b.id : da < db;
    });
    return matches;
}

void MusicLookupClient::lyrics(const QString &title, const QString &artist, double duration, LyricsCallback done) {
    enqueue({lyricsURL(title, artist), 8 * 1024 * 1024, [duration, done](QByteArray data, QString error) {
        if (!error.isEmpty()) return done({}, error);
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) return done({}, "The service returned an unreadable response. Try again later.");
        QVector<LyricMatch> matches;
        for (const QJsonValue &v : doc.array()) {
            const QJsonObject o = v.toObject();
            LyricMatch m;
            m.id = o["id"].toInt();
            m.trackName = o["trackName"].toString(); m.artistName = o["artistName"].toString(); m.albumName = o["albumName"].toString();
            m.duration = o["duration"].toDouble(-1);
            m.instrumental = o["instrumental"].toBool();
            m.plainLyrics = o["plainLyrics"].toString(); m.syncedLyrics = o["syncedLyrics"].toString();
            matches.append(m);
        }
        matches = rankLyrics(matches, duration);
        if (matches.isEmpty()) return done({}, "No matches found. Try changing the title or artist.");
        done(matches, {});
    }});
}

void MusicLookupClient::artwork(const QString &title, const QString &artist, ArtworkCallback done) {
    enqueue({artworkURL(title, artist), 2 * 1024 * 1024, [done](QByteArray data, QString error) {
        if (!error.isEmpty()) return done({}, error);
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject() || !doc["release-groups"].isArray())
            return done({}, "The service returned an unreadable response. Try again later.");
        QVector<ArtworkMatch> out;
        for (const QJsonValue &v : doc["release-groups"].toArray()) {
            const QJsonObject g = v.toObject();
            const QUuid id = QUuid::fromString(g["id"].toString());
            if (id.isNull()) continue;
            QStringList credits;
            for (const QJsonValue &c : g["artist-credit"].toArray()) {
                const QJsonObject co = c.toObject();
                credits << (co.contains("name") ? co["name"].toString() : co["artist"].toObject()["name"].toString());
            }
            out.append({id, g["title"].toString(), credits.join(", "), g["first-release-date"].toString()});
        }
        if (out.isEmpty()) return done({}, "No matches found. Try changing the title or artist.");
        done(out, {});
    }});
}

void MusicLookupClient::cover(const QUuid &releaseGroup, DataCallback done) {
    const QUrl url("https://coverartarchive.org/release-group/" + releaseGroup.toString(QUuid::WithoutBraces).toLower() + "/front-1200");
    enqueue({url, 25 * 1024 * 1024, done});
}

void MusicLookupClient::enqueue(Request request) {
    m_queue.push_back(std::move(request));
    pump();
}

void MusicLookupClient::pump() {
    if (m_busy || m_queue.empty()) return;
    m_busy = true;
    Request request = std::move(m_queue.front());
    m_queue.pop_front();
    const qint64 wait = QDateTime::currentDateTimeUtc().msecsTo(m_nextRequest.value(request.url.host(), QDateTime()));
    QTimer::singleShot(std::max<qint64>(0, qint64(wait * m_timeScale)), this, [this, request] { perform(request); });
}

QString MusicLookupClient::errorFor(int status, const QUrl &url) const {
    if (status == 404 && url.host() == "coverartarchive.org") return "No artwork is available for this edition. Try another result.";
    return QString("The service is unavailable (%1). Try again later.").arg(status);
}

void MusicLookupClient::perform(Request request) {
    QNetworkRequest req(request.url);
    const QString host = request.url.host();
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Accept", host == "coverartarchive.org" ? "image/*" : "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setTransferTimeout(25'000);
    QNetworkReply *reply = m_nam->get(req);

    auto buffer = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, reply, [reply, buffer, tooLarge, limit = request.limit] {
        buffer->append(reply->readAll());
        if (buffer->size() > limit) { *tooLarge = true; reply->abort(); }
    });
    // Hard cap on the whole transfer (45 s resource timeout).
    QTimer::singleShot(45'000, reply, [reply] { if (reply->isRunning()) reply->abort(); });

    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, tooLarge, request, host]() mutable {
        reply->deleteLater();
        // Minimum gap after every request, even failed ones; MusicBrainz allows one per second.
        const QDateTime gap = QDateTime::currentDateTimeUtc().addMSecs(host == "musicbrainz.org" ? 1100 : 350);
        if (m_nextRequest.value(host) < gap) m_nextRequest[host] = gap;
        auto finish = [this](const DataCallback &done, QByteArray data, QString error) {
            m_busy = false;
            done(std::move(data), std::move(error));
            pump();
        };
        buffer->append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (*tooLarge) return finish(request.done, {}, "The download is too large to import.");
        if (status == 429 || status == 503) {
            const QByteArray header = reply->rawHeader("Retry-After");
            bool ok = false;
            double delay = header.trimmed().toDouble(&ok);
            if (!ok) {
                const QDateTime when = QLocale::c().toDateTime(QString::fromLatin1(header).trimmed().replace(" GMT", ""), "ddd, dd MMM yyyy HH:mm:ss");
                delay = when.isValid() ? QDateTime::currentDateTimeUtc().msecsTo(QDateTime(when.date(), when.time(), QTimeZone::utc())) / 1000.0 : 5;
            }
            delay = std::max(1.0, delay);
            m_nextRequest[host] = QDateTime::currentDateTimeUtc().addMSecs(qint64(delay * 1000));
            const int seconds = int(std::ceil(delay));
            if (request.attempt < MaxRetries && seconds <= MaxAutoRetryWait) {
                // Brief overload (LRCLIB answers with 503 + Retry-After: 1): retry quietly with a growing pad.
                ++request.attempt;
                m_nextRequest[host] = m_nextRequest[host].addMSecs(500 * request.attempt);
                m_busy = false;
                m_queue.push_front(std::move(request));
                pump();
                return;
            }
            const QString message = status == 503
                ? QStringLiteral("The service is busy right now. Try searching again in a moment.")
                : QString("The service asked us to wait %1 %2 before searching again.").arg(seconds).arg(seconds == 1 ? "second" : "seconds");
            return finish(request.done, {}, message);
        }
        if (reply->error() != QNetworkReply::NoError && status == 0)
            return finish(request.done, {}, "Couldn’t reach the service. Check your connection and try again.");
        if (status < 200 || status >= 300) return finish(request.done, {}, errorFor(status, request.url));
        finish(request.done, *buffer, {});
    });
}
