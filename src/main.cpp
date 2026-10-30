#include "audio/PlaybackEngine.h"
#include "audio/Player.h"
#include "core/LibraryStore.h"
#include "ui/AlbumModel.h"
#include "ui/AppController.h"
#include "ui/ArtworkImageProvider.h"

#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName("CoverFlow");
    QGuiApplication::setApplicationName("CoverFlow");
    QGuiApplication::setApplicationDisplayName("Cover Flow");
    QGuiApplication::setApplicationVersion(COVERFLOW_VERSION);
    QGuiApplication::setWindowIcon(QIcon(":/qt/qml/CoverFlow/resources/icon-256.png"));
    QQuickStyle::setStyle("Basic");

    // Single instance: a second launch brings the running window forward.
    const QString serverName = "CoverFlow-" + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) { probe.write("show"); probe.waitForBytesWritten(300); return 0; }
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    LibraryStore library;
    QString libraryNotice;
    library.load(&libraryNotice);
    PlaybackEngine engine;
    Player player(&library, &engine);
    player.restore();

    auto *images = new ArtworkImageProvider;   // owned by the QML engine
    AppController controller(&library, &engine, &player, images);

    QQmlApplicationEngine qml;
    qml.addImageProvider("artwork", images);
    qml.rootContext()->setContextProperty("app", &controller);
    qml.rootContext()->setContextProperty("player", &player);
    QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    qml.load(QUrl("qrc:/qt/qml/CoverFlow/Main.qml"));

    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *s = server.nextPendingConnection()) s->deleteLater();
        for (QObject *root : qml.rootObjects())
            if (auto *w = qobject_cast<QQuickWindow *>(root)) { w->show(); w->raise(); w->requestActivate(); }
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] { player.saveNow(); library.saveNow(); });
    // Files passed on the command line (Open with…) are imported.
    QList<QUrl> args;
    for (const QString &a : app.arguments().mid(1)) if (!a.startsWith('-')) args << QUrl::fromLocalFile(a);
    if (!args.isEmpty()) controller.importUrls(args);
    // Developer aid for headless checks: COVERFLOW_SCREENSHOT=<file.png> saves the main
    // window after COVERFLOW_SCREENSHOT_DELAY ms (default 3000) and quits.
    const QString shot = qEnvironmentVariable("COVERFLOW_SCREENSHOT");
    if (!shot.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("COVERFLOW_SCREENSHOT_DELAY");
        QTimer::singleShot(delay > 0 ? delay : 3000, &app, [&qml, shot] {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                auto *qw = qobject_cast<QQuickWindow *>(w);
                if (!qw || !qw->isVisible()) continue;
                const bool main = qw->title() == "Cover Flow";
                const QString file = main ? shot : QString(shot).replace(".png", "-" + qw->title().replace(' ', '_') + ".png");
                qw->grabWindow().save(file);
            }
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
