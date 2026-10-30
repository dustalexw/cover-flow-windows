#include "LibraryScanner.h"
#include "Paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

#include <fileref.h>
#include <tpropertymap.h>
#include <tag.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

namespace LibraryScanner {

const QStringList &supportedExtensions() {
    static const QStringList list = {
        "mp3", "m4a", "m4b", "m4p", "aac", "adts", "wav", "wave", "aif", "aiff", "aifc", "caf", "flac", "mp4", "3gp",
        "ac3", "eac3", "amr", "opus", "ogg", "oga", "spx", "ape", "wv", "wma", "asf", "dsf", "dff", "mka", "webm",
        "weba", "mpc", "mpp", "mp+", "tta", "tak", "shn", "dts", "w64"};
    return list;
}
const QStringList &knownUnsupportedExtensions() {
    static const QStringList list = {"mid", "midi", "ra", "rm", "xm", "mod"};
    return list;
}
bool isAudioFile(const QString &path) {
    return supportedExtensions().contains(QFileInfo(path).suffix().toLower());
}

FileNameParts parseFileName(const QString &baseName) {
    static const QRegularExpression re(R"(^\s*(?:(\d{1,2})[-.](?=\d))?(\d{1,3})\s*(?:[.\-_)]\s*|\s+)(.+)$)");
    FileNameParts parts;
    const auto m = re.match(baseName);
    if (!m.hasMatch()) { parts.title = baseName; return parts; }
    if (m.capturedLength(1)) parts.disc = m.captured(1).toInt();
    parts.track = m.captured(2).toInt();
    parts.title = m.captured(3).trimmed();
    return parts;
}

std::optional<int> parseNumberPair(const QString &s) {
    bool ok = false;
    const int n = s.section('/', 0, 0).trimmed().toInt(&ok);
    return ok && n > 0 ? std::optional<int>(n) : std::nullopt;
}

std::optional<int> parseYear(const QString &s) {
    static const QRegularExpression re(R"((1[89]|20)\d\d)");
    const auto m = re.match(s);
    return m.hasMatch() ? std::optional<int>(m.captured(0).toInt()) : std::nullopt;
}

QByteArray folderImage(const QString &audioPath) {
    static const QStringList names = {"cover", "folder", "front", "album", "artwork", "albumart"};
    static const QStringList exts = {"jpg", "jpeg", "png", "heic", "webp", "tif", "tiff"};
    const QDir dir = QFileInfo(audioPath).dir();
    QStringList files = dir.entryList(QDir::Files, QDir::Name);
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) { return Text::naturalCompare(a, b) < 0; });
    for (const QString &name : files) {
        const QFileInfo info(name);
        if (names.contains(info.completeBaseName().toLower()) && exts.contains(info.suffix().toLower())) {
            QFile f(dir.filePath(name));
            if (f.size() > 0 && f.size() < 40 * 1024 * 1024 && f.open(QIODevice::ReadOnly)) return f.readAll();
        }
    }
    return {};
}

static std::optional<QString> nonEmpty(const QString &s) {
    const QString t = s.trimmed();
    return t.isEmpty() ? std::nullopt : std::optional<QString>(t);
}

#ifdef _WIN32
static TagLib::FileName tagName(const QString &path) { return TagLib::FileName(reinterpret_cast<const wchar_t *>(path.utf16())); }
#else
static QByteArray tagNameBytes(const QString &path) { return QFile::encodeName(path); }
#endif

static void readTagLib(const QString &path, ScannedMetadata &m) {
#ifdef _WIN32
    TagLib::FileRef ref(tagName(path), false);
#else
    const QByteArray name = tagNameBytes(path);
    TagLib::FileRef ref(name.constData(), false);
#endif
    if (ref.isNull() || !ref.file()) return;
    const TagLib::PropertyMap props = ref.file()->properties();
    auto first = [&](std::initializer_list<const char *> keys) -> std::optional<QString> {
        for (const char *k : keys) {
            auto it = props.find(k);
            if (it != props.end() && !it->second.isEmpty())
                if (auto v = nonEmpty(QString::fromStdWString(it->second.front().toWString()))) return v;
        }
        return std::nullopt;
    };
    if (!m.title) m.title = first({"TITLE"});
    if (!m.artist) m.artist = first({"ARTIST"});
    if (!m.album) m.album = first({"ALBUM"});
    if (!m.albumArtist) m.albumArtist = first({"ALBUMARTIST", "ALBUM ARTIST", "ALBUM_ARTIST"});
    if (!m.trackNumber) if (auto v = first({"TRACKNUMBER"})) m.trackNumber = parseNumberPair(*v);
    if (!m.discNumber) if (auto v = first({"DISCNUMBER"})) m.discNumber = parseNumberPair(*v);
    if (!m.year) if (auto v = first({"DATE", "YEAR", "ORIGINALDATE"})) m.year = parseYear(*v);
    if (!m.genre) if (auto v = first({"GENRE"})) {
        // ID3 "(17)Rock" style prefixes; purely numeric genres are discarded.
        QString g = *v;
        g.remove(QRegularExpression(R"(^\(\d+\))"));
        bool numeric = false; g.trimmed().toInt(&numeric);
        if (!numeric) m.genre = nonEmpty(g);
    }
}

// Duration, attached picture and fallback tags through FFmpeg. Returns false if the
// file has no playable audio.
static bool readFFmpeg(const QString &path, ScannedMetadata &m, QString *error) {
    AVFormatContext *fmt = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr) < 0) { *error = "unreadable file"; return false; }
    struct Closer { AVFormatContext **f; ~Closer() { avformat_close_input(f); } } closer{&fmt};
    if (avformat_find_stream_info(fmt, nullptr) < 0) { *error = "unreadable stream"; return false; }
    const int audio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio < 0) { *error = "no audio"; return false; }
    const AVStream *s = fmt->streams[audio];
    if (s->duration != AV_NOPTS_VALUE) m.duration = s->duration * av_q2d(s->time_base);
    else if (fmt->duration != AV_NOPTS_VALUE) m.duration = double(fmt->duration) / AV_TIME_BASE;

    // Attached pictures: prefer one labelled front cover.
    const AVPacket *best = nullptr;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream *st = fmt->streams[i];
        if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC) || st->attached_pic.size <= 0) continue;
        const AVDictionaryEntry *c = av_dict_get(st->metadata, "comment", nullptr, 0);
        const bool front = c && (QString::fromUtf8(c->value).contains("front", Qt::CaseInsensitive));
        if (!best || front) best = &st->attached_pic;
        if (front) break;
    }
    if (best) m.picture = QByteArray(reinterpret_cast<const char *>(best->data), best->size);

    auto tag = [&](std::initializer_list<const char *> keys) -> std::optional<QString> {
        for (const AVDictionary *dict : {static_cast<const AVDictionary *>(fmt->metadata), static_cast<const AVDictionary *>(s->metadata)})
            for (const char *k : keys)
                if (const AVDictionaryEntry *e = av_dict_get(dict, k, nullptr, 0))
                    if (auto v = nonEmpty(QString::fromUtf8(e->value))) return v;
        return std::nullopt;
    };
    if (!m.title) m.title = tag({"title"});
    if (!m.artist) m.artist = tag({"artist"});
    if (!m.album) m.album = tag({"album"});
    if (!m.albumArtist) m.albumArtist = tag({"album_artist", "albumartist", "album artist", "WM/AlbumArtist"});
    if (!m.trackNumber) if (auto v = tag({"track", "tracknumber", "WM/TrackNumber"})) m.trackNumber = parseNumberPair(*v);
    if (!m.discNumber) if (auto v = tag({"disc", "discnumber", "WM/PartOfSet"})) m.discNumber = parseNumberPair(*v);
    if (!m.year) if (auto v = tag({"date", "year", "originaldate", "WM/Year"})) m.year = parseYear(*v);
    if (!m.genre) m.genre = tag({"genre", "WM/Genre"});
    if (m.duration <= 0) { *error = "The file contains no audio."; return false; }
    return true;
}

Result read(const QString &path) {
    Result result;
    ScannedMetadata m;
    readTagLib(path, m);
    QString error;
    if (!readFFmpeg(path, m, &error)) {
        result.error = error;
        result.notPlayable = true;
        return result;
    }
    const QFileInfo info(path);
    const FileNameParts parts = parseFileName(info.completeBaseName());
    if (!m.title) { m.title = parts.title; m.usedFallback = true; }
    if (!m.trackNumber) m.trackNumber = parts.track;
    if (!m.discNumber) m.discNumber = parts.disc;
    if (!m.album) m.usedFallback = true;
    if (m.picture.isEmpty()) m.picture = folderImage(path);
    result.metadata = m;
    return result;
}

void enumerate(const QStringList &roots, QStringList *audio, QStringList *unsupported) {
    auto consider = [&](const QString &file, bool explicitlyChosen) {
        const QString ext = QFileInfo(file).suffix().toLower();
        if (supportedExtensions().contains(ext)) audio->append(QDir::cleanPath(file));
        else if (knownUnsupportedExtensions().contains(ext) || (explicitlyChosen && !ext.isEmpty()))
            unsupported->append(QDir::cleanPath(file));
    };
    for (const QString &root : roots) {
        const QFileInfo info(root);
        if (info.isDir()) {
            QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
            while (it.hasNext()) {
                const QString file = it.next();
                if (it.fileInfo().isHidden() || file.contains("/.")) continue;
                consider(file, false);
            }
        } else if (info.isFile()) {
            consider(root, true);
        }
    }
    auto natural = [](const QString &a, const QString &b) { return Text::naturalCompare(a, b) < 0; };
    std::sort(audio->begin(), audio->end(), natural);
    audio->erase(std::unique(audio->begin(), audio->end()), audio->end());
    std::sort(unsupported->begin(), unsupported->end(), natural);
}
}
