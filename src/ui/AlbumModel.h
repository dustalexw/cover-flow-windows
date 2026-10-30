#pragma once
#include "core/Model.h"
#include <QAbstractListModel>

class LibraryStore;

// Visible albums (search + sort) for Cover Flow and the grid.
class AlbumModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, ArtistRole, YearRole, GenreRole, TrackCountRole, DurationRole,
                 ArtworkRole, TintRole, AspectRole, IsDemoRole, MissingMetadataRole, MetaRole };
    explicit AlbumModel(LibraryStore *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString search() const { return m_search; }
    void setSearch(const QString &s);
    int sortOrder() const { return int(m_order); }
    void setSortOrder(int order);
    int totalCount() const;

    Q_INVOKABLE int indexOf(const QString &albumId) const;
    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    QVector<QUuid> orderedIds() const;

    static QString formatLongDuration(double seconds);

public slots:
    void refresh();

signals:
    void searchChanged();
    void sortOrderChanged();
    void countChanged();

private:
    LibraryStore *m_library;
    QVector<QUuid> m_ids;
    QString m_search;
    SortOrder m_order = SortOrder::Artist;
};
