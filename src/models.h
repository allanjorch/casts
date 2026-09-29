#pragma once

#include "library.h"

#include <QAbstractListModel>

class ShowModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ShowIdRole = Qt::UserRole + 1,
        TitleRole,
        AuthorRole,
        CoverRole,
        UnheardRole,
    };

    explicit ShowModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QList<ShowRow> &rows);

private:
    QList<ShowRow> m_rows;
};

class EpisodeModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        EpisodeIdRole = Qt::UserRole + 1,
        ShowIdRole,
        TitleRole,
        PublishedRole,
        DurationRole,
        PlayedRole,
        PositionRole,
        AudioRole,
    };

    explicit EpisodeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QList<EpisodeRow> &rows);

private:
    QList<EpisodeRow> m_rows;
};
