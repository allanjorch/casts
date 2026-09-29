#include "models.h"

#include <QFileInfo>
#include <QUrl>

namespace {
QString coverUrl(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    return QUrl::fromLocalFile(path).toString();
}
}

ShowModel::ShowModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ShowModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ShowModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const ShowRow &row = m_rows.at(index.row());
    switch (role) {
    case ShowIdRole:
        return row.id;
    case TitleRole:
        return row.title;
    case AuthorRole:
        return row.author;
    case CoverRole:
        return coverUrl(row.imagePath);
    case UnheardRole:
        return row.unheard;
    default:
        return {};
    }
}

QHash<int, QByteArray> ShowModel::roleNames() const
{
    return {
        {ShowIdRole, "showId"},
        {TitleRole, "title"},
        {AuthorRole, "author"},
        {CoverRole, "cover"},
        {UnheardRole, "unheard"},
    };
}

void ShowModel::setRows(const QList<ShowRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

EpisodeModel::EpisodeModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EpisodeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant EpisodeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const EpisodeRow &row = m_rows.at(index.row());
    switch (role) {
    case EpisodeIdRole:
        return row.id;
    case ShowIdRole:
        return row.showId;
    case TitleRole:
        return row.title;
    case PublishedRole:
        return row.published;
    case DurationRole:
        return row.durationSecs;
    case PlayedRole:
        return row.played;
    case PositionRole:
        return row.positionMs;
    case AudioRole:
        return row.audioUrl;
    default:
        return {};
    }
}

QHash<int, QByteArray> EpisodeModel::roleNames() const
{
    return {
        {EpisodeIdRole, "episodeId"},
        {ShowIdRole, "showId"},
        {TitleRole, "title"},
        {PublishedRole, "published"},
        {DurationRole, "duration"},
        {PlayedRole, "played"},
        {PositionRole, "positionMs"},
        {AudioRole, "audioUrl"},
    };
}

void EpisodeModel::setRows(const QList<EpisodeRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}
