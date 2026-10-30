#include "Palette.h"
#include "Paths.h"
#include <QHash>
#include <QMap>
#include <cmath>
#include <algorithm>

namespace Palette {

static double lin(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double luminance(const QColor &c) {
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}
double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
static double saturation(const QColor &c) {
    return std::max({c.redF(), c.greenF(), c.blueF()}) - std::min({c.redF(), c.greenF(), c.blueF()});
}
QColor mixed(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QColor readable(const QColor &color, const QColor &bg, double minimum) {
    if (contrast(color, bg) >= minimum) return color;
    const QColor white(Qt::white), black(Qt::black);
    const QColor ink = contrast(white, bg) >= contrast(black, bg) ? white : black;
    const double target = std::min(minimum, contrast(ink, bg));
    double lo = 0, hi = 1;
    for (int i = 0; i < 20; ++i) {
        const double mid = (lo + hi) / 2;
        if (contrast(mixed(color, ink, mid), bg) >= target) hi = mid; else lo = mid;
    }
    return mixed(color, ink, hi);
}

AlbumPalette make(const QColor &bg, const QVector<QColor> &candidates) {
    AlbumPalette p;
    p.background = bg;
    const QColor ink = contrast(Qt::white, bg) >= contrast(Qt::black, bg) ? QColor(Qt::white) : QColor(Qt::black);
    const QColor fg = mixed(ink, bg, 0.08);
    p.foreground = contrast(fg, bg) >= 4.5 ? fg : ink;
    const QColor sec = mixed(ink, bg, 0.28);
    p.secondary = contrast(sec, bg) >= 4.5 ? sec : p.foreground;
    p.accent = p.foreground;
    for (const QColor &c : candidates)
        if (saturation(c) > 0.12 && contrast(c, bg) >= 4.5) { p.accent = c; break; }
    return p;
}

AlbumPalette extract(const QImage &cover) {
    if (cover.isNull()) return make(QColor(16, 16, 18), {});
    const QImage img = cover.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
    struct Bucket { int count = 0, edge = 0; double r = 0, g = 0, b = 0; };
    QMap<int, Bucket> buckets;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.alphaF() <= 0.5) continue;
            const int key = (int(std::lround(c.redF() * 15)) << 8) | (int(std::lround(c.greenF() * 15)) << 4) | int(std::lround(c.blueF() * 15));
            Bucket &b = buckets[key];
            ++b.count;
            if (x < 12) ++b.edge;      // left edge meets the track list
            b.r += c.redF(); b.g += c.greenF(); b.b += c.blueF();
        }
    if (buckets.isEmpty()) return make(QColor(16, 16, 18), {});
    auto colorOf = [](const Bucket &b) { return QColor::fromRgbF(float(b.r / b.count), float(b.g / b.count), float(b.b / b.count)); };
    int bestKey = -1, bestScore = -1;
    for (auto it = buckets.begin(); it != buckets.end(); ++it) {
        const int score = it->edge * 4 + it->count;
        if (score > bestScore) { bestScore = score; bestKey = it.key(); }   // QMap iterates keys ascending → ties keep lower key
    }
    QVector<QPair<double, QColor>> ranked;
    for (const Bucket &b : buckets) { const QColor c = colorOf(b); ranked.append({b.count * (0.25 + saturation(c)), c}); }
    std::stable_sort(ranked.begin(), ranked.end(), [](auto &a, auto &b) { return a.first > b.first; });
    QVector<QColor> candidates;
    for (auto &r : ranked) candidates.append(r.second);
    return make(colorOf(buckets[bestKey]), candidates);
}

AlbumPalette fallback(const QString &tintHex, const QString &title, const QString &artist) {
    QColor bg(tintHex);
    if (tintHex.isEmpty() || !bg.isValid())
        bg = QColor::fromHsvF(float((Text::stableHash(title + artist) % 360) / 360.0), 0.32f, 0.30f);
    return make(bg, {});
}

AlbumPalette customized(AlbumPalette p, const QString &bgHex, const QString &textHex, const QString &accentHex) {
    if (QColor c(bgHex); !bgHex.isEmpty() && c.isValid()) p = make(c, {p.accent});
    if (QColor c(textHex); !textHex.isEmpty() && c.isValid()) p.foreground = readable(c, p.background);
    if (QColor c(accentHex); !accentHex.isEmpty() && c.isValid()) p.accent = readable(c, p.background);
    if (!textHex.isEmpty()) p.secondary = readable(mixed(p.foreground, p.background, 0.22), p.background);
    return p;
}

QColor themeTint(const QColor &c, bool dark) {
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h < 0) h = 0;
    return dark ? QColor::fromHsvF(h, std::min(s, 0.6f), std::clamp(v * 0.8f, 0.28f, 0.6f))
                : QColor::fromHsvF(h, std::min(s, 0.5f), std::max(v, 0.72f));
}
}
