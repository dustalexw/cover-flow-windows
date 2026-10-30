#include "Artwork.h"
#include "Paths.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QSaveFile>
#include <QLinearGradient>
#include <QFont>

namespace ArtworkStore {

QString pathFor(const QString &fileName) { return Paths::artworkDir() + "/" + fileName; }

static QString extensionFor(const QByteArray &format) {
    const QByteArray f = format.toLower();
    if (f == "jpeg" || f == "jpg") return "jpg";
    if (f.isEmpty()) return "img";
    return QString::fromLatin1(f);
}

static QImage readImage(QIODevice *device, int maxPixels, QByteArray *format = nullptr, QSize *originalSize = nullptr) {
    QImageReader reader(device);
    reader.setAutoTransform(true);           // EXIF orientation
    if (format) *format = reader.format();
    QSize size = reader.size();
    // Rotated by 90° per EXIF: report the displayed size.
    if (size.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90)) size.transpose();
    if (originalSize) *originalSize = size;
    if (size.isValid() && maxPixels > 0 && std::max(size.width(), size.height()) > maxPixels)
        reader.setScaledSize(size.scaled(maxPixels, maxPixels, Qt::KeepAspectRatio));
    return reader.read();
}

QString averageColorHex(const QImage &image) {
    if (image.isNull()) return {};
    const QImage small = image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             .scaled(12, 12, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGBA8888);
    double r = 0, g = 0, b = 0, total = 0;
    for (int y = 0; y < small.height(); ++y)
        for (int x = 0; x < small.width(); ++x) {
            const QColor c = small.pixelColor(x, y);
            if (c.alphaF() <= 0.01) continue;
            const double mx = std::max({c.redF(), c.greenF(), c.blueF()});
            const double mn = std::min({c.redF(), c.greenF(), c.blueF()});
            const double w = 0.35 + (mx - mn);
            r += c.redF() * w; g += c.greenF() * w; b += c.blueF() * w; total += w;
        }
    if (total <= 0) return {};
    return QColor::fromRgbF(float(r / total), float(g / total), float(b / total)).name().toUpper();
}

std::optional<Saved> save(const QByteArray &data, const QUuid &albumID, QString *error) {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QByteArray format;
    QSize size;
    const QImage thumb = readImage(&buffer, 48, &format, &size);
    if (thumb.isNull() || !size.isValid() || size.isEmpty()) {
        if (error) *error = QStringLiteral("That file isn’t an image Cover Flow can read.");
        return std::nullopt;
    }
    const QString prefix = albumID.toString(QUuid::WithoutBraces).toUpper();
    const QString name = prefix + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper() + "." + extensionFor(format);
    QSaveFile file(pathFor(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("The artwork couldn’t be saved.");
        return std::nullopt;
    }
    // Keep one cover per album.
    const QDir dir(Paths::artworkDir());
    for (const QString &old : dir.entryList({prefix + "-*"}, QDir::Files))
        if (old != name) QFile::remove(dir.filePath(old));
    return Saved{name, size.width(), size.height(), averageColorHex(thumb)};
}

void remove(const QString &fileName) {
    if (!fileName.isEmpty()) QFile::remove(pathFor(fileName));
}

QImage decode(const QString &fileName, int maxPixels) {
    QFile f(pathFor(fileName));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return readImage(&f, maxPixels);
}

int bucketFor(int requested) {
    if (requested <= 160) return 160;
    if (requested <= 360) return 360;
    if (requested <= 720) return 720;
    if (requested <= 1100) return 1100;
    return 4096;
}

QColor placeholderColor(const QString &title, const QString &artist) {
    const int hue = int(Text::stableHash(title + artist) % 360);
    return QColor::fromHsvF(hue / 360.0f, 0.32f, 0.30f);
}

QImage placeholder(const QString &title, const QString &artist, int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    const QColor base = placeholderColor(title, artist);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0, base.lighter(130));
    g.setColorAt(1, base.darker(140));
    p.fillRect(image.rect(), g);
    p.setPen(QColor(255, 255, 255, 40));
    for (int i = 1; i < 6; ++i) p.drawEllipse(QPointF(size * 0.5, size * 0.5), size * i * 0.09, size * i * 0.09);
    p.setPen(QColor(255, 255, 255, 220));
    QFont font;
    font.setPixelSize(std::max(10, size / 11));
    font.setBold(true);
    p.setFont(font);
    const QRectF box(size * 0.08, size * 0.62, size * 0.84, size * 0.3);
    p.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, title);
    font.setBold(false);
    font.setPixelSize(std::max(8, size / 16));
    p.setFont(font);
    p.setPen(QColor(255, 255, 255, 170));
    p.drawText(QRectF(size * 0.08, size * 0.08, size * 0.84, size * 0.2), Qt::AlignLeft | Qt::AlignTop, artist);
    return image;
}
}
