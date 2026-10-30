#pragma once
#include <QQuickAsyncImageProvider>
#include <QCache>
#include <QHash>
#include <QMutex>
#include <QThreadPool>
#include <QUuid>

// image://artwork/<albumUUID>/<artworkVersion> — decoded on one worker thread
// (like ArtworkRasterCache) at bucketed sizes, with LRU budgets of 192 MiB for
// thumbnails and 96 MiB for full-size images. Albums without art get the placeholder.
class ArtworkImageProvider : public QQuickAsyncImageProvider {
public:
    ArtworkImageProvider();
    ~ArtworkImageProvider() override;
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    struct Entry { QString artworkFile, title, artist; };
    void update(const QHash<QUuid, Entry> &albums);

    QImage load(const QString &id, const QSize &requestedSize);   // synchronous, any thread

private:
    QMutex m_mutex;
    QHash<QUuid, Entry> m_albums;
    QCache<QString, QImage> m_thumbs{192 * 1024};   // KiB cost
    QCache<QString, QImage> m_full{96 * 1024};
    QThreadPool m_pool;
};
