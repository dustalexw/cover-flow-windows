#pragma once
#include <QString>
#include <QUuid>
#include <QDateTime>
#include <QVector>
#include <QUrl>
#include <optional>
#include <QTimeZone>

// Mirrors Model/Models.swift so Library.json keeps the macOS schema.
struct Track {
    QUuid id;
    QString path;                  // absolute local path (stored as fileURL)
    QString title, artist, albumTitle;
    std::optional<QString> albumArtist;
    std::optional<int> trackNumber, discNumber, year;
    double duration = 0;
    std::optional<QString> genre;
    QDateTime dateAdded;
    bool isDemo = false;
    bool missingMetadata = false;
    QString fingerprint;
};

struct AlbumAppearance {
    std::optional<QString> backgroundHex, textHex, accentHex;
    bool sceneEnabled = true;
    double sceneStrength = 0.55;
    int sceneVariation = 0;
    std::optional<bool> sceneRefinement;
    std::optional<QString> refinementStyle, immersiveRefinementStyle;
    bool isDefault() const {
        return !backgroundHex && !textHex && !accentHex && sceneEnabled && qFuzzyCompare(sceneStrength, 0.55)
            && sceneVariation == 0 && !sceneRefinement && !refinementStyle && !immersiveRefinementStyle;
    }
};

struct Album {
    QUuid id;
    QString groupKey;
    QString title, artist;
    std::optional<int> year;
    std::optional<QString> genre;
    QVector<QUuid> trackIDs;
    std::optional<QString> artworkFile;     // file name inside Artwork/
    std::optional<int> artworkPixelWidth, artworkPixelHeight;
    int artworkVersion = 0;
    std::optional<QString> tintHex;         // "#RRGGBB"
    QDateTime dateAdded;
    std::optional<QDateTime> lastPlayed;
    bool isDemo = false, missingMetadata = false, userEdited = false;
    std::optional<AlbumAppearance> appearance;

    QString artworkKey() const { return id.toString(QUuid::WithoutBraces).toUpper() + "-" + QString::number(artworkVersion); }
    double artworkAspect() const {
        return (artworkPixelWidth && artworkPixelHeight && *artworkPixelHeight > 0)
            ? double(*artworkPixelWidth) / *artworkPixelHeight : 1.0;
    }
};

enum class SortOrder { Artist, Title, RecentlyAdded, RecentlyPlayed };
enum class RepeatMode { Off, All, One };

// Swift's default JSONEncoder writes Dates as seconds since 2001-01-01 UTC.
inline constexpr qint64 AppleEpochOffset = 978307200;
inline double toAppleTime(const QDateTime &t) { return t.toMSecsSinceEpoch() / 1000.0 - AppleEpochOffset; }
inline QDateTime fromAppleTime(double s) { return QDateTime::fromMSecsSinceEpoch(qint64((s + AppleEpochOffset) * 1000.0), QTimeZone::utc()); }
