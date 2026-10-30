#include "core/MusicLookupClient.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtTest>
#include <functional>

// Sends every request to a local fake server, keeping the original path and query.
class RedirectingNAM : public QNetworkAccessManager {
public:
    quint16 port = 0;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override {
        QNetworkRequest r(request);
        QUrl url = request.url();
        r.setRawHeader("X-Original-Host", url.host().toUtf8());
        url.setScheme("http"); url.setHost("127.0.0.1"); url.setPort(port);
        r.setUrl(url);
        return QNetworkAccessManager::createRequest(op, r, data);
    }
};

class FakeServer : public QTcpServer {
public:
    std::function<QByteArray(const QByteArray &request)> respond;
    int requests = 0;
    FakeServer() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray req = s->readAll();
                    ++requests;
                    s->write(respond(req));
                    s->disconnectFromHost();
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
};

static QByteArray http(int status, const QByteArray &body, const QByteArray &extra = {}) {
    return "HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n" + extra + "\r\n" + body;
}

class TestLookup : public QObject {
    Q_OBJECT
private slots:
    void urls() {
        const QUrlQuery q(MusicLookupClient::lyricsURL(" I Hope, I Think, I Know ", "Oasis & Friends+1"));
        QCOMPARE(q.queryItemValue("track_name", QUrl::FullyDecoded), QString("I Hope, I Think, I Know"));
        QCOMPARE(q.queryItemValue("artist_name", QUrl::FullyDecoded), QString("Oasis & Friends+1"));
        QVERIFY(!QUrlQuery(MusicLookupClient::lyricsURL("Song", " ")).hasQueryItem("artist_name"));
        const QString mb = QUrlQuery(MusicLookupClient::artworkURL("Album \"Deluxe\"", "")).queryItemValue("query", QUrl::FullyDecoded);
        QCOMPARE(mb, QString("releasegroup:\"Album \\\"Deluxe\\\"\""));
    }
    void ranking() {
        auto m = [](int id, double d, bool synced, bool instrumental = false) {
            LyricMatch x; x.id = id; x.duration = d; x.instrumental = instrumental;
            (synced ? x.syncedLyrics : x.plainLyrics) = "[00:01]x";
            return x;
        };
        const auto ranked = MusicLookupClient::rankLyrics({m(1, 400, true), m(2, 201, true), m(3, 200, false), m(4, 200, true, true)}, 200);
        QCOMPARE(ranked.size(), 3);
        QCOMPARE(ranked[0].id, 2);   // same 2-second bucket: synced first
        QCOMPARE(ranked[1].id, 3);
        QCOMPARE(ranked[2].id, 1);
    }
    void briefOverloadIsRetried() {
        FakeServer server;
        RedirectingNAM nam; nam.port = server.serverPort();
        int calls = 0;
        server.respond = [&](const QByteArray &) {
            ++calls;
            if (calls < 3) return http(503, "{\"name\":\"ServerOverloaded\"}", "Retry-After: 1\r\n");
            return http(200, "[{\"id\":7,\"trackName\":\"Song\",\"artistName\":\"A\",\"albumName\":\"B\",\"duration\":200,\"instrumental\":false,\"plainLyrics\":\"Words\"}]");
        };
        MusicLookupClient client(&nam);
        client.setTimeScale(0.01);
        QVector<LyricMatch> result; QString error; bool done = false;
        client.lyrics("Song", "A", 200, [&](QVector<LyricMatch> r, QString e) { result = r; error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(calls, 3);
        QCOMPARE(result.size(), 1);
    }
    void persistentOverloadReportsBusyAndLongWaitsAreNotRetried() {
        FakeServer server;
        RedirectingNAM nam; nam.port = server.serverPort();
        int calls = 0;
        server.respond = [&](const QByteArray &) { ++calls; return http(503, "{}", "Retry-After: 1\r\n"); };
        MusicLookupClient client(&nam);
        client.setTimeScale(0.01);
        QString error; bool done = false;
        client.lyrics("Song", "A", 200, [&](QVector<LyricMatch>, QString e) { error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(calls, MusicLookupClient::MaxRetries + 1);
        QCOMPARE(error, QString("The service is busy right now. Try searching again in a moment."));

        calls = 0; done = false;
        server.respond = [&](const QByteArray &) { ++calls; return http(429, "{}", "Retry-After: 17\r\n"); };
        client.lyrics("Song", "A", 200, [&](QVector<LyricMatch>, QString e) { error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(calls, 1);
        QCOMPARE(error, QString("The service asked us to wait 17 seconds before searching again."));
    }
    void sendsUserAgentAndReportsNoMatches() {
        FakeServer server;
        RedirectingNAM nam; nam.port = server.serverPort();
        QByteArray seen;
        server.respond = [&](const QByteArray &req) { seen = req; return http(200, "[]"); };
        MusicLookupClient client(&nam);
        client.setTimeScale(0.01);
        QString error; bool done = false;
        client.lyrics("Song", "", 1, [&](QVector<LyricMatch>, QString e) { error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(seen.contains("User-Agent: CoverFlow/"));
        QVERIFY(seen.contains("GET /api/search?track_name=Song "));
        QCOMPARE(error, QString("No matches found. Try changing the title or artist."));
    }
};

QTEST_GUILESS_MAIN(TestLookup)
#include "tst_lookup.moc"
