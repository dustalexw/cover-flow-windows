#include "Lyrics.h"
#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <algorithm>

LyricsDocument LyricsDocument::parse(const QString &input) {
    static const QRegularExpression stamp(R"(\[(\d+):(\d{1,2})(?:[.:](\d{1,3}))?\])");
    static const QRegularExpression meta(R"(^\[([A-Za-z]+):(.*)\]$)");
    static const QRegularExpression word(R"(<\d+:\d{1,2}(?:[.:]\d{1,3})?>)");

    LyricsDocument doc;
    QString text = input;
    text.remove(QChar(0xFEFF));
    struct Raw { double time; QString text; int order; };
    QVector<Raw> raw;
    QStringList plain;
    const QStringList lines = text.split(QRegularExpression("\r\n|\r|\n"));
    for (const QString &original : lines) {
        // Trim spaces and tabs only.
        int b = 0, e = original.size();
        while (b < e && (original[b] == ' ' || original[b] == '\t')) ++b;
        while (e > b && (original[e - 1] == ' ' || original[e - 1] == '\t')) --e;
        const QString line = original.mid(b, e - b);

        if (const auto m = meta.match(line); m.hasMatch()) {
            if (m.captured(1).compare("offset", Qt::CaseInsensitive) == 0) {
                bool ok = false;
                const double ms = m.captured(2).trimmed().toDouble(&ok);
                if (ok) doc.offset = ms / 1000.0;
            }
            continue;
        }
        // Leading timestamps only.
        QVector<double> times;
        int pos = 0;
        for (;;) {
            int p = pos;
            while (p < line.size() && line[p].isSpace()) ++p;
            const auto m = stamp.match(line, p, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (!m.hasMatch()) break;
            const int sec = m.captured(2).toInt();
            if (sec < 60) {
                const double frac = m.capturedLength(3) ? ("0." + m.captured(3)).toDouble() : 0.0;
                times.append(m.captured(1).toInt() * 60.0 + sec + frac);
            }
            pos = m.capturedEnd(0);
        }
        if (pos == 0) {
            if (line.isEmpty() || !line.startsWith('[')) plain << line;
            continue;
        }
        QString rest = line.mid(pos);
        rest.remove(word);
        rest = rest.trimmed();
        for (double t : times) raw.append({t, rest, int(raw.size())});
    }
    std::stable_sort(raw.begin(), raw.end(), [](const Raw &a, const Raw &b) { return a.time < b.time; });
    for (const Raw &r : raw) {
        if (!doc.cues.isEmpty() && doc.cues.back().time == r.time) {
            LyricCue &last = doc.cues.back();
            if (!r.text.isEmpty() && !last.text.split('\n').contains(r.text))
                last.text = last.text.isEmpty() ? r.text : last.text + "\n" + r.text;
            continue;
        }
        doc.cues.append({int(doc.cues.size()), r.time, r.text});
    }
    // Plain text keeps its shape but loses leading/trailing blank lines.
    while (!plain.isEmpty() && plain.front().trimmed().isEmpty()) plain.removeFirst();
    while (!plain.isEmpty() && plain.back().trimmed().isEmpty()) plain.removeLast();
    doc.plainText = plain.join('\n');
    return doc;
}

int LyricsDocument::activeIndex(double time) const {
    const double t = time + offset;
    auto it = std::upper_bound(cues.begin(), cues.end(), t, [](double v, const LyricCue &c) { return v < c.time; });
    return int(it - cues.begin()) - 1;
}

bool LyricsDocument::operator==(const LyricsDocument &o) const {
    if (cues.size() != o.cues.size() || plainText != o.plainText || offset != o.offset) return false;
    for (int i = 0; i < cues.size(); ++i)
        if (cues[i].time != o.cues[i].time || cues[i].text != o.cues[i].text) return false;
    return true;
}

namespace LyricsStore {
static constexpr qint64 MaxBytes = 2 * 1024 * 1024;

static QString dirOr(const QString &directory) { return directory.isEmpty() ? Paths::lyricsDir() : Paths::ensure(directory); }
static QString savedPath(const QUuid &id, const QString &directory) {
    return dirOr(directory) + "/" + id.toString(QUuid::WithoutBraces).toUpper() + ".lrc";
}

std::optional<QString> decode(const QByteArray &bytes, QString *error) {
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        auto decoder = QStringDecoder(bytes.startsWith("\xFF\xFE") ? QStringDecoder::Utf16LE : QStringDecoder::Utf16BE);
        QString s = decoder(bytes.mid(2));
        if (!decoder.hasError()) return s;
    } else {
        QStringDecoder decoder(QStringDecoder::Utf8);
        QString s = decoder(bytes);
        if (!decoder.hasError()) return s;
    }
    if (error) *error = "That lyric file could not be read. Save it as UTF-8 or UTF-16 and try again.";
    return std::nullopt;
}

static std::optional<LyricsDocument> readDocument(const QString &path, QString *error) {
    QFile f(path);
    if (f.size() > MaxBytes) { if (error) *error = "That lyric file is too large. Choose a lyric file smaller than 2 MB."; return std::nullopt; }
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "That lyric file could not be opened."; return std::nullopt; }
    const auto text = decode(f.readAll(), error);
    if (!text) return std::nullopt;
    LyricsDocument doc = LyricsDocument::parse(*text);
    if (doc.isEmpty()) { if (error) *error = "That file has no readable lyrics. Check its text and timestamps, then try again."; return std::nullopt; }
    return doc;
}

std::optional<Found> find(const QUuid &trackID, const QString &audioPath, QString *error, const QString &directory) {
    const QString saved = savedPath(trackID, directory);
    if (QFile::exists(saved)) {
        if (auto doc = readDocument(saved, error)) return Found{*doc, saved, true};
        return std::nullopt;
    }
    if (audioPath.isEmpty()) return std::nullopt;
    const QFileInfo audio(audioPath);
    const QString sidecarName = audio.completeBaseName() + ".lrc";
    QString sidecar = audio.dir().filePath(sidecarName);
    if (!QFileInfo(sidecar).isFile()) {
        sidecar.clear();
        QStringList names = audio.dir().entryList(QDir::Files, QDir::Name);
        for (const QString &n : names)
            if (n.compare(sidecarName, Qt::CaseInsensitive) == 0) { sidecar = audio.dir().filePath(n); break; }
    }
    if (sidecar.isEmpty()) return std::nullopt;
    if (auto doc = readDocument(sidecar, error)) return Found{*doc, sidecar, false};
    return std::nullopt;
}

static std::optional<Found> store(const QByteArray &bytes, const QUuid &trackID, QString *error, const QString &directory) {
    if (bytes.size() > MaxBytes) { if (error) *error = "That lyric file is too large. Choose a lyric file smaller than 2 MB."; return std::nullopt; }
    const auto text = decode(bytes, error);
    if (!text) return std::nullopt;
    LyricsDocument doc = LyricsDocument::parse(*text);
    if (doc.isEmpty()) { if (error) *error = "That file has no readable lyrics. Check its text and timestamps, then try again."; return std::nullopt; }
    const QString path = savedPath(trackID, directory);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = "The lyrics couldn’t be saved.";
        return std::nullopt;
    }
    return Found{doc, path, true};
}

std::optional<Found> importFile(const QString &path, const QUuid &trackID, QString *error, const QString &directory) {
    static const QStringList allowed = {"lrc", "txt", "lyric", "lyrics"};
    if (!allowed.contains(QFileInfo(path).suffix().toLower())) {
        if (error) *error = "Choose an .lrc, .txt, .lyric or .lyrics file.";
        return std::nullopt;
    }
    QFile f(path);
    if (f.size() > MaxBytes) { if (error) *error = "That lyric file is too large. Choose a lyric file smaller than 2 MB."; return std::nullopt; }
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "That lyric file could not be opened."; return std::nullopt; }
    return store(f.readAll(), trackID, error, directory);
}

std::optional<Found> importText(const QString &text, const QUuid &trackID, QString *error, const QString &directory) {
    return store(text.toUtf8(), trackID, error, directory);
}
}
