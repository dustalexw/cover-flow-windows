#pragma once
#include <QColor>
#include <QImage>
#include <QVector>
#include <QString>

// AlbumDetailPalette.swift: panel colours picked from the cover, with WCAG-checked text.
namespace Palette {
double luminance(const QColor &c);
double contrast(const QColor &a, const QColor &b);
QColor mixed(const QColor &a, const QColor &b, double t);
QColor readable(const QColor &color, const QColor &background, double minimum = 4.8);

struct AlbumPalette {
    QColor background, foreground, secondary, accent;
    bool isDark() const { return luminance(foreground) > 0.5; }
};
AlbumPalette make(const QColor &background, const QVector<QColor> &candidates);
AlbumPalette extract(const QImage &cover);
AlbumPalette fallback(const QString &tintHex, const QString &title, const QString &artist);
AlbumPalette customized(AlbumPalette base, const QString &bgHex, const QString &textHex, const QString &accentHex);

// Global theme tint of an album colour for the window backdrop (Support.swift tint()).
QColor themeTint(const QColor &c, bool dark);
}
