#pragma once
#include <QString>
#include <QVector>
#include <QUuid>
#include <optional>

struct LyricCue { int id = 0; double time = 0; QString text; };

// LyricsDocument.swift: LRC (incl. enhanced word stamps, offsets, multi-stamp lines)
// and plain text.
struct LyricsDocument {
    QVector<LyricCue> cues;
    QString plainText;
    double offset = 0;      // seconds; positive advances the highlight

    static LyricsDocument parse(const QString &text);
    bool isSynced() const { return !cues.isEmpty(); }
    bool isEmpty() const { return cues.isEmpty() && plainText.trimmed().isEmpty(); }
    int activeIndex(double time) const;          // -1 before the first cue
    double playbackTime(const LyricCue &cue) const { return std::max(0.0, cue.time - offset); }
    bool operator==(const LyricsDocument &o) const;
};

namespace LyricsStore {
struct Found { LyricsDocument document; QString sourcePath; bool imported = false; };
// 1) Lyrics/<trackUUID>.lrc  2) sidecar <basename>.lrc  3) case-insensitive sidecar match.
std::optional<Found> find(const QUuid &trackID, const QString &audioPath, QString *error = nullptr, const QString &directory = {});
// Validates and saves raw bytes as Lyrics/<trackUUID>.lrc.
std::optional<Found> importFile(const QString &path, const QUuid &trackID, QString *error = nullptr, const QString &directory = {});
std::optional<Found> importText(const QString &text, const QUuid &trackID, QString *error = nullptr, const QString &directory = {});
// Decodes lyric bytes: UTF-16 with BOM, else strict UTF-8.
std::optional<QString> decode(const QByteArray &bytes, QString *error = nullptr);
}
