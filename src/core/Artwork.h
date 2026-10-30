#pragma once
#include <QString>
#include <QByteArray>
#include <QImage>
#include <QUuid>
#include <QColor>
#include <optional>

// ArtworkStore.swift: original cover bytes kept under Artwork/<album>-<random>.<ext>.
namespace ArtworkStore {
struct Saved { QString fileName; int width = 0, height = 0; QString tintHex; };
std::optional<Saved> save(const QByteArray &data, const QUuid &albumID, QString *error = nullptr);
void remove(const QString &fileName);
QString pathFor(const QString &fileName);
// Orientation-corrected decode bounded to maxPixels on the long side.
QImage decode(const QString &fileName, int maxPixels);
// Weighted mean colour favouring saturated pixels (averageColor).
QString averageColorHex(const QImage &image);
// Thumbnail size buckets: 160, 360, 720, 1100; "full" = 4096.
int bucketFor(int requested);
// Placeholder cover colour: HSB(stableHash(title+artist) % 360, 0.32, 0.30).
QColor placeholderColor(const QString &title, const QString &artist);
QImage placeholder(const QString &title, const QString &artist, int size);
}
