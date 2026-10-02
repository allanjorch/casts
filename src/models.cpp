#include "models.h"

#include "covercache.h"

#include <QFileInfo>
#include <QUrl>

namespace {
QString coverUrl(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    return QUrl::fromLocalFile(path).toString();
}

bool sameEpisode(const EpisodeRow &a, const EpisodeRow &b)
{
    return a.id == b.id
        && a.showId == b.showId
        && a.title == b.title
        && a.description == b.description
        && a.audioUrl == b.audioUrl
        && a.imagePath == b.imagePath
        && a.published == b.published
        && a.durationSecs == b.durationSecs
        && a.played == b.played
        && a.positionMs == b.positionMs;
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
    case DescriptionRole:
        return row.description;
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
    case CoverRole:
        // Memory-cached local file. A plain file URL is decoded again as soon as
        // Qt Quick evicts its ~2 MB cache of unreferenced images.
        return localCoverSource(row.imagePath);
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
        {DescriptionRole, "description"},
        {PublishedRole, "published"},
        {DurationRole, "duration"},
        {PlayedRole, "played"},
        {PositionRole, "positionMs"},
        {AudioRole, "audioUrl"},
        {CoverRole, "cover"},
    };
}

void EpisodeModel::setRows(const QList<EpisodeRow> &rows)
{
    // Same episodes in the same order: update fields in place. beginResetModel()
    // destroys every delegate, and each Image then reloads its cover.
    if (m_rows.size() == rows.size()) {
        bool sameIds = true;
        for (int i = 0; i < rows.size(); ++i) {
            if (m_rows.at(i).id != rows.at(i).id) {
                sameIds = false;
                break;
            }
        }
        if (sameIds) {
            for (int i = 0; i < rows.size(); ++i) {
                if (sameEpisode(m_rows.at(i), rows.at(i)))
                    continue;
                const bool coverChanged = m_rows.at(i).imagePath != rows.at(i).imagePath;
                m_rows[i] = rows.at(i);
                QList<int> roles = {
                    ShowIdRole, TitleRole, DescriptionRole, PublishedRole,
                    DurationRole, PlayedRole, PositionRole, AudioRole,
                };
                if (coverChanged)
                    roles.append(CoverRole);
                const QModelIndex idx = index(i, 0);
                emit dataChanged(idx, idx, roles);
            }
            return;
        }
    }
    if (m_rows.isEmpty() && rows.isEmpty())
        return;
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void EpisodeModel::setImagePath(qint64 episodeId, const QString &imagePath)
{
    if (episodeId == 0 || imagePath.isEmpty())
        return;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id != episodeId)
            continue;
        if (m_rows.at(i).imagePath == imagePath)
            return;
        m_rows[i].imagePath = imagePath;
        const QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, {CoverRole});
        return;
    }
}
