#pragma once
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <optional>

// Tag reading for one file (MetadataReader.swift). TagLib supplies text tags;
// FFmpeg supplies duration, embedded pictures and tags for formats TagLib can't open.
struct ScannedMetadata {
    std::optional<QString> title, artist, album, albumArtist, genre;
    std::optional<int> trackNumber, discNumber, year;
    double duration = 0;
    QByteArray picture;          // embedded or folder image bytes
    bool usedFallback = false;   // title or album came from the filename/folder
};

namespace LibraryScanner {
// Extension lists from MetadataReader.swift + FFmpegAudio.swift.
const QStringList &supportedExtensions();
const QStringList &knownUnsupportedExtensions();
bool isAudioFile(const QString &path);

struct Result { std::optional<ScannedMetadata> metadata; QString error; bool notPlayable = false; };
Result read(const QString &path);

// Recursively lists audio files (natural path order) and unsupported ones that were
// explicitly chosen or have a known-unsupported extension.
void enumerate(const QStringList &roots, QStringList *audio, QStringList *unsupported);

// Helpers exposed for tests.
struct FileNameParts { std::optional<int> disc, track; QString title; };
FileNameParts parseFileName(const QString &baseName);
std::optional<int> parseNumberPair(const QString &s);
std::optional<int> parseYear(const QString &s);
QByteArray folderImage(const QString &audioPath);
}
