#include "ArtworkImageProvider.h"
#include "core/Artwork.h"

#include <QRunnable>
#include <QMutexLocker>

namespace {
class Response : public QQuickImageResponse, public QRunnable {
public:
    Response(ArtworkImageProvider *provider, QString id, QSize size) : m_provider(provider), m_id(std::move(id)), m_size(size) {
        setAutoDelete(false);
    }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    void run() override {
        if (!m_cancelled) m_image = m_provider->load(m_id, m_size);
        emit finished();
    }
    void cancel() override { m_cancelled = true; }
private:
    ArtworkImageProvider *m_provider;
    QString m_id;
    QSize m_size;
    QImage m_image;
    std::atomic<bool> m_cancelled{false};
};
}

ArtworkImageProvider::ArtworkImageProvider() { m_pool.setMaxThreadCount(1); }
ArtworkImageProvider::~ArtworkImageProvider() { m_pool.clear(); m_pool.waitForDone(); }

void ArtworkImageProvider::update(const QHash<QUuid, Entry> &albums) {
    QMutexLocker lock(&m_mutex);
    m_albums = albums;
}

QQuickImageResponse *ArtworkImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    auto *response = new Response(this, id, requestedSize);
    m_pool.start(response);
    return response;
}

QImage ArtworkImageProvider::load(const QString &id, const QSize &requestedSize) {
    const QUuid albumID = QUuid::fromString(id.section('/', 0, 0));
    const int requested = std::max(requestedSize.width(), requestedSize.height());
    const int pixels = ArtworkStore::bucketFor(requested <= 0 ? 720 : requested);
    Entry entry;
    {
        QMutexLocker lock(&m_mutex);
        entry = m_albums.value(albumID);
        const QString key = id + "@" + QString::number(pixels);
        QCache<QString, QImage> &cache = pixels > 1100 ? m_full : m_thumbs;
        if (QImage *hit = cache.object(key)) return *hit;
    }
    QImage image;
    if (!entry.artworkFile.isEmpty()) image = ArtworkStore::decode(entry.artworkFile, pixels);
    if (image.isNull()) image = ArtworkStore::placeholder(entry.title, entry.artist, std::min(pixels, 720));
    QMutexLocker lock(&m_mutex);
    QCache<QString, QImage> &cache = pixels > 1100 ? m_full : m_thumbs;
    if (pixels > 1100 && cache.count() >= 3) cache.clear();   // keep at most 3 full-size images
    cache.insert(id + "@" + QString::number(pixels), new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}
