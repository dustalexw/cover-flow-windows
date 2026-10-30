#include "AlbumModel.h"
#include "core/LibraryStore.h"
#include <cmath>

AlbumModel::AlbumModel(LibraryStore *library, QObject *parent) : QAbstractListModel(parent), m_library(library) {
    connect(library, &LibraryStore::changed, this, &AlbumModel::refresh);
    refresh();
}

QString AlbumModel::formatLongDuration(double seconds) {
    const int total = int(std::lround(seconds));
    if (total < 60) return QString("%1 sec").arg(total);
    const int minutes = int(std::lround(seconds / 60));
    if (minutes < 60) return QString("%1 min").arg(minutes);
    return QString("%1 hr %2 min").arg(minutes / 60).arg(minutes % 60);
}

void AlbumModel::refresh() {
    QVector<QUuid> ids;
    for (const Album *a : m_library->visibleAlbums(m_search, m_order)) ids.append(a->id);
    if (ids == m_ids) {
        // Same order; contents (artwork, titles) may still have changed.
        if (!m_ids.isEmpty()) emit dataChanged(index(0), index(m_ids.size() - 1));
        return;
    }
    beginResetModel();
    m_ids = ids;
    endResetModel();
    emit countChanged();
}

void AlbumModel::setSearch(const QString &s) {
    if (s == m_search) return;
    m_search = s;
    refresh();
    emit searchChanged();
}

void AlbumModel::setSortOrder(int order) {
    const SortOrder o = SortOrder(std::clamp(order, 0, 3));
    if (o == m_order) return;
    m_order = o;
    refresh();
    emit sortOrderChanged();
}

int AlbumModel::totalCount() const { return m_library->albums().size(); }
int AlbumModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_ids.size(); }

QVariant AlbumModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_ids.size()) return {};
    const Album *a = m_library->album(m_ids[index.row()]);
    if (!a) return {};
    switch (role) {
    case IdRole: return a->id.toString(QUuid::WithoutBraces);
    case TitleRole: case Qt::DisplayRole: return a->title;
    case ArtistRole: return a->artist;
    case YearRole: return a->year ? QVariant(*a->year) : QVariant();
    case GenreRole: return a->genre.value_or(QString());
    case TrackCountRole: return int(a->trackIDs.size());
    case DurationRole: {
        double d = 0;
        for (const Track *t : m_library->tracksOf(*a)) d += t->duration;
        return d;
    }
    case ArtworkRole: return QString("image://artwork/%1/%2").arg(a->id.toString(QUuid::WithoutBraces)).arg(a->artworkVersion);
    case TintRole: return a->tintHex.value_or(QString());
    case AspectRole: return a->artworkAspect();
    case IsDemoRole: return a->isDemo;
    case MissingMetadataRole: return a->missingMetadata;
    case MetaRole: {
        // "year · N tracks · duration · genre"
        QStringList parts;
        if (a->year) parts << QString::number(*a->year);
        parts << (a->trackIDs.size() == 1 ? QStringLiteral("1 track") : QString("%1 tracks").arg(a->trackIDs.size()));
        double d = 0;
        for (const Track *t : m_library->tracksOf(*a)) d += t->duration;
        parts << formatLongDuration(d);
        if (a->genre && *a->genre != "Demo") parts << *a->genre;
        return parts.join(QStringLiteral(" · "));
    }
    }
    return {};
}

QHash<int, QByteArray> AlbumModel::roleNames() const {
    return {{IdRole, "albumId"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {YearRole, "year"}, {GenreRole, "genre"},
            {TrackCountRole, "trackCount"}, {DurationRole, "duration"}, {ArtworkRole, "artwork"}, {TintRole, "tint"},
            {AspectRole, "aspect"}, {IsDemoRole, "isDemo"}, {MissingMetadataRole, "missingMetadata"}, {MetaRole, "meta"}};
}

int AlbumModel::indexOf(const QString &albumId) const { return int(m_ids.indexOf(QUuid::fromString(albumId))); }
QString AlbumModel::idAt(int row) const { return row >= 0 && row < m_ids.size() ? m_ids[row].toString(QUuid::WithoutBraces) : QString(); }
QVector<QUuid> AlbumModel::orderedIds() const { return m_ids; }

QVariantMap AlbumModel::get(int row) const {
    QVariantMap map;
    if (row < 0 || row >= m_ids.size()) return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it) map[QString::fromLatin1(it.value())] = data(index(row), it.key());
    return map;
}
