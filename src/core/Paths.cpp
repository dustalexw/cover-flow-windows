#include "Paths.h"
#include <QStandardPaths>
#include <QDir>

namespace Paths {
QString ensure(const QString &dir) { QDir().mkpath(dir); return dir; }

QString dataDir() {
    const QByteArray env = qgetenv("COVERFLOW_DATA_DIR");
    if (!env.isEmpty()) return ensure(QString::fromLocal8Bit(env));
    // AppDataLocation already includes the organization/application names.
    return ensure(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}
QString artworkDir() { return ensure(dataDir() + "/Artwork"); }
QString lyricsDir() { return ensure(dataDir() + "/Lyrics"); }
QString karaokeDir() { return ensure(dataDir() + "/Karaoke"); }
}

namespace Text {
QString normalizedKey(const QString &s) {
    // NFKD folds width/compatibility forms and splits accents off as combining marks.
    const QString decomposed = s.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (QChar c : decomposed) {
        const auto cat = c.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining || cat == QChar::Mark_Enclosing) continue;
        out.append(c);
    }
    return out.toCaseFolded().simplified();
}

quint64 stableHash(const QString &s) {
    quint64 h = 5381;
    for (unsigned char b : s.toUtf8()) h = h * 33 + b;
    return h;
}

int naturalCompare(const QString &a, const QString &b) {
    // Deterministic on every platform: digit runs compare by value, text runs
    // case-insensitively (then by the original, so the order is total).
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            int si = i, sj = j;
            while (si < a.size() && a[si] == '0') ++si;
            while (sj < b.size() && b[sj] == '0') ++sj;
            int ei = si, ej = sj;
            while (ei < a.size() && a[ei].isDigit()) ++ei;
            while (ej < b.size() && b[ej].isDigit()) ++ej;
            if (ei - si != ej - sj) return (ei - si) < (ej - sj) ? -1 : 1;
            if (int c = QStringView(a).mid(si, ei - si).compare(QStringView(b).mid(sj, ej - sj))) return c < 0 ? -1 : 1;
            i = ei; j = ej;
            continue;
        }
        const QChar ca = a[i].toCaseFolded(), cb = b[j].toCaseFolded();
        if (ca != cb) {
            const int c = QString::localeAwareCompare(QString(ca), QString(cb));
            if (c) return c < 0 ? -1 : 1;
            return ca < cb ? -1 : 1;
        }
        ++i; ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
}
}
