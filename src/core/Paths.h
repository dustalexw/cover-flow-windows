#pragma once
#include <QString>

namespace Paths {
// %APPDATA%/CoverFlow on Windows. Overridable with COVERFLOW_DATA_DIR (tests, portable use),
// mirroring the macOS CoverFlowDataDirectory override.
QString dataDir();
QString artworkDir();
QString lyricsDir();
QString karaokeDir();
QString ensure(const QString &dir);
}

namespace Text {
// Case-, diacritic- and width-insensitive fold with collapsed whitespace
// (String.normalizedKey in Support.swift).
QString normalizedKey(const QString &s);
// djb2 over UTF-8 bytes (String.stableHash) — seeds generated scenes.
quint64 stableHash(const QString &s);
// Natural ("Track 2" < "Track 10"), case-insensitive comparison.
int naturalCompare(const QString &a, const QString &b);
}
