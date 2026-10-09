#include "library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {
int connectionSerial = 0;

QSqlDatabase dbOf(const QString &connection)
{
    return QSqlDatabase::database(connection);
}
}

QString Library::defaultPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/library.db");
}

Library::Library(const QString &path)
{
    m_connection = QStringLiteral("library-%1").arg(++connectionSerial);
    m_path = path.isEmpty() ? defaultPath() : path;
    QDir().mkpath(QFileInfo(m_path).absolutePath());

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(m_path);
    if (!db.open()) {
        m_error = db.lastError().text();
        return;
    }
    if (!migrate()) {
        db.close();
        return;
    }
    m_open = true;
}

Library::~Library()
{
    {
        auto db = dbOf(m_connection);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

bool Library::migrate()
{
    QSqlQuery q(dbOf(m_connection));
    const QStringList statements = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("PRAGMA journal_mode = WAL"),
        QStringLiteral("PRAGMA busy_timeout = 5000"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS shows ("
            " id INTEGER PRIMARY KEY,"
            " feed_url TEXT NOT NULL UNIQUE,"
            " title TEXT NOT NULL,"
            " author TEXT NOT NULL DEFAULT '',"
            " image_url TEXT NOT NULL DEFAULT '',"
            " image_path TEXT NOT NULL DEFAULT '')"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS episodes ("
            " id INTEGER PRIMARY KEY,"
            " show_id INTEGER NOT NULL REFERENCES shows(id) ON DELETE CASCADE,"
            " guid TEXT NOT NULL,"
            " title TEXT NOT NULL,"
            " description TEXT NOT NULL DEFAULT '',"
            " audio_url TEXT NOT NULL,"
            " image_url TEXT NOT NULL DEFAULT '',"
            " image_path TEXT NOT NULL DEFAULT '',"
            " published_at INTEGER NOT NULL DEFAULT 0,"
            " duration_secs INTEGER NOT NULL DEFAULT 0,"
            " played INTEGER NOT NULL DEFAULT 0,"
            " position_ms INTEGER NOT NULL DEFAULT 0,"
            " UNIQUE(show_id, guid))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS settings ("
            " key TEXT PRIMARY KEY,"
            " value TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS queue ("
            " episode_id INTEGER PRIMARY KEY REFERENCES episodes(id) ON DELETE CASCADE,"
            " position INTEGER NOT NULL)"),
    };
    for (const auto &sql : statements) {
        if (!q.exec(sql)) {
            m_error = q.lastError().text();
            return false;
        }
    }

    const QStringList episodeColumns = {
        QStringLiteral("description TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("image_url TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("image_path TEXT NOT NULL DEFAULT ''"),
    };
    QStringList existing;
    if (!q.exec(QStringLiteral("PRAGMA table_info(episodes)"))) {
        m_error = q.lastError().text();
        return false;
    }
    while (q.next())
        existing.append(q.value(1).toString());
    for (const auto &column : episodeColumns) {
        const QString name = column.section(QLatin1Char(' '), 0, 0);
        if (existing.contains(name))
            continue;
        if (!q.exec(QStringLiteral("ALTER TABLE episodes ADD COLUMN %1").arg(column))) {
            m_error = q.lastError().text();
            return false;
        }
    }
    return true;
}

QList<ShowRow> Library::shows() const
{
    QList<ShowRow> rows;
    QSqlQuery q(dbOf(m_connection));
    q.exec(QStringLiteral(
        "SELECT s.id, s.feed_url, s.title, s.author, s.image_path,"
        " (SELECT COUNT(*) FROM episodes e WHERE e.show_id = s.id AND e.played = 0),"
        " COALESCE((SELECT MAX(published_at) FROM episodes e WHERE e.show_id = s.id), 0),"
        " COALESCE((SELECT MAX(published_at) FROM episodes e WHERE e.show_id = s.id AND e.played = 0), 0)"
        " FROM shows s"
        " ORDER BY"
        "  CASE WHEN (SELECT COUNT(*) FROM episodes e WHERE e.show_id = s.id AND e.played = 0) > 0 THEN 0 ELSE 1 END,"
        "  CASE WHEN (SELECT COUNT(*) FROM episodes e WHERE e.show_id = s.id AND e.played = 0) > 0"
        "       THEN (SELECT MAX(published_at) FROM episodes e WHERE e.show_id = s.id AND e.played = 0)"
        "       ELSE COALESCE((SELECT MAX(published_at) FROM episodes e WHERE e.show_id = s.id), 0) END DESC,"
        "  s.title COLLATE NOCASE ASC"));
    while (q.next()) {
        ShowRow row;
        row.id = q.value(0).toLongLong();
        row.feedUrl = q.value(1).toString();
        row.title = q.value(2).toString();
        row.author = q.value(3).toString();
        row.imagePath = q.value(4).toString();
        row.unheard = q.value(5).toInt();
        row.newest = q.value(6).toLongLong();
        row.youngestUnheard = q.value(7).toLongLong();
        rows.append(row);
    }
    return rows;
}

QList<EpisodeRow> Library::episodes(qint64 showId) const
{
    QList<EpisodeRow> rows;
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral(
        "SELECT id, show_id, guid, title, description, audio_url, image_url, image_path,"
        " published_at, duration_secs, played, position_ms"
        " FROM episodes WHERE show_id = ?"
        " ORDER BY published_at DESC, id DESC"));
    q.addBindValue(showId);
    q.exec();
    while (q.next()) {
        EpisodeRow row;
        row.id = q.value(0).toLongLong();
        row.showId = q.value(1).toLongLong();
        row.guid = q.value(2).toString();
        row.title = q.value(3).toString();
        row.description = q.value(4).toString();
        row.audioUrl = q.value(5).toString();
        row.imageUrl = q.value(6).toString();
        row.imagePath = q.value(7).toString();
        row.published = q.value(8).toLongLong();
        row.durationSecs = q.value(9).toInt();
        row.played = q.value(10).toInt() != 0;
        row.positionMs = q.value(11).toInt();
        rows.append(row);
    }
    return rows;
}

EpisodeRow Library::episode(qint64 id) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral(
        "SELECT id, show_id, guid, title, description, audio_url, image_url, image_path,"
        " published_at, duration_secs, played, position_ms"
        " FROM episodes WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    EpisodeRow row;
    if (!q.next())
        return row;
    row.id = q.value(0).toLongLong();
    row.showId = q.value(1).toLongLong();
    row.guid = q.value(2).toString();
    row.title = q.value(3).toString();
    row.description = q.value(4).toString();
    row.audioUrl = q.value(5).toString();
    row.imageUrl = q.value(6).toString();
    row.imagePath = q.value(7).toString();
    row.published = q.value(8).toLongLong();
    row.durationSecs = q.value(9).toInt();
    row.played = q.value(10).toInt() != 0;
    row.positionMs = q.value(11).toInt();
    return row;
}

QString Library::showTitle(qint64 showId) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("SELECT title FROM shows WHERE id = ?"));
    q.addBindValue(showId);
    q.exec();
    return q.next() ? q.value(0).toString() : QString();
}

QString Library::showImage(qint64 showId) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("SELECT image_path FROM shows WHERE id = ?"));
    q.addBindValue(showId);
    q.exec();
    return q.next() ? q.value(0).toString() : QString();
}

QString Library::showImageUrl(qint64 showId) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("SELECT image_url FROM shows WHERE id = ?"));
    q.addBindValue(showId);
    q.exec();
    return q.next() ? q.value(0).toString() : QString();
}

qint64 Library::episodeByAudioUrl(const QString &audioUrl) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("SELECT id FROM episodes WHERE audio_url = ? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(audioUrl);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : 0;
}

qint64 Library::upsertShow(const QString &feedUrl, const ParsedShow &parsed)
{
    auto db = dbOf(m_connection);
    if (!db.transaction())
        return 0;

    QSqlQuery find(db);
    find.prepare(QStringLiteral("SELECT id, image_url, image_path FROM shows WHERE feed_url = ?"));
    find.addBindValue(feedUrl);
    if (!find.exec()) {
        db.rollback();
        return 0;
    }

    qint64 showId = 0;
    QString imageUrl;
    QString imagePath;
    if (find.next()) {
        showId = find.value(0).toLongLong();
        imageUrl = find.value(1).toString();
        imagePath = find.value(2).toString();
        QSqlQuery update(db);
        update.prepare(QStringLiteral(
            "UPDATE shows SET title = ?, author = ?, image_url = ? WHERE id = ?"));
        update.addBindValue(parsed.title);
        update.addBindValue(parsed.author);
        update.addBindValue(parsed.imageUrl);
        update.addBindValue(showId);
        if (!update.exec()) {
            db.rollback();
            return 0;
        }
        // URL changed: drop cached path + file so downloadCover fetches the new cover.
        if (imageUrl != parsed.imageUrl) {
            if (!imagePath.isEmpty())
                QFile::remove(imagePath);
            imagePath.clear();
            QSqlQuery clearPath(db);
            clearPath.prepare(QStringLiteral("UPDATE shows SET image_path = '' WHERE id = ?"));
            clearPath.addBindValue(showId);
            if (!clearPath.exec()) {
                db.rollback();
                return 0;
            }
        }
    } else {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral(
            "INSERT INTO shows (feed_url, title, author, image_url) VALUES (?, ?, ?, ?)"));
        insert.addBindValue(feedUrl);
        insert.addBindValue(parsed.title);
        insert.addBindValue(parsed.author);
        insert.addBindValue(parsed.imageUrl);
        if (!insert.exec()) {
            db.rollback();
            return 0;
        }
        showId = insert.lastInsertId().toLongLong();
    }

    struct PreviousEpisodeImage {
        QString imageUrl;
        QString imagePath;
    };
    QHash<QString, PreviousEpisodeImage> previousImages;
    {
        QSqlQuery existing(db);
        existing.prepare(QStringLiteral(
            "SELECT guid, image_url, image_path FROM episodes WHERE show_id = ?"));
        existing.addBindValue(showId);
        if (!existing.exec()) {
            m_error = existing.lastError().text();
            db.rollback();
            return 0;
        }
        while (existing.next()) {
            previousImages.insert(existing.value(0).toString(),
                                  PreviousEpisodeImage{existing.value(1).toString(),
                                                       existing.value(2).toString()});
        }
    }

    QSqlQuery upsert(db);
    if (!upsert.prepare(QStringLiteral(
        "INSERT INTO episodes (show_id, guid, title, description, audio_url, image_url,"
        " published_at, duration_secs)"
        " VALUES (?, ?, ?, COALESCE(?, ''), ?, COALESCE(?, ''), ?, ?)"
        " ON CONFLICT(show_id, guid) DO UPDATE SET"
        "  title = excluded.title,"
        "  description = COALESCE(excluded.description, ''),"
        "  audio_url = excluded.audio_url,"
        "  image_url = COALESCE(excluded.image_url, ''),"
        "  image_path = CASE WHEN COALESCE(excluded.image_url, '') = episodes.image_url"
        "                    THEN episodes.image_path ELSE '' END,"
        "  published_at = excluded.published_at,"
        "  duration_secs = CASE WHEN excluded.duration_secs > 0 THEN excluded.duration_secs"
        "                       ELSE episodes.duration_secs END"))) {
        m_error = upsert.lastError().text();
        db.rollback();
        return 0;
    }
    for (const auto &ep : parsed.episodes) {
        const auto previous = previousImages.constFind(ep.guid);
        if (previous != previousImages.cend() && previous->imageUrl != ep.imageUrl
            && !previous->imagePath.isEmpty()) {
            QFile::remove(previous->imagePath);
        }
        upsert.addBindValue(showId);
        upsert.addBindValue(ep.guid);
        upsert.addBindValue(ep.title);
        upsert.addBindValue(ep.description);
        upsert.addBindValue(ep.audioUrl);
        upsert.addBindValue(ep.imageUrl);
        upsert.addBindValue(ep.published);
        upsert.addBindValue(ep.durationSecs);
        if (!upsert.exec()) {
            m_error = upsert.lastError().text();
            db.rollback();
            return 0;
        }
        upsert.finish();
    }

    if (!imagePath.isEmpty() && imageUrl == parsed.imageUrl) {
        QSqlQuery keep(db);
        keep.prepare(QStringLiteral("UPDATE shows SET image_path = ? WHERE id = ?"));
        keep.addBindValue(imagePath);
        keep.addBindValue(showId);
        keep.exec();
    }

    if (!db.commit())
        return 0;
    return showId;
}

void Library::setShowImage(qint64 showId, const QString &imageUrl, const QString &imagePath)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("UPDATE shows SET image_url = ?, image_path = ? WHERE id = ?"));
    q.addBindValue(imageUrl);
    q.addBindValue(imagePath);
    q.addBindValue(showId);
    q.exec();
}

void Library::setEpisodeImage(qint64 episodeId, const QString &imageUrl, const QString &imagePath)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("UPDATE episodes SET image_url = ?, image_path = ? WHERE id = ?"));
    q.addBindValue(imageUrl);
    q.addBindValue(imagePath);
    q.addBindValue(episodeId);
    q.exec();
}

void Library::clearShowImagePath(qint64 showId)
{
    const QString path = showImage(showId);
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("UPDATE shows SET image_path = '' WHERE id = ?"));
    q.addBindValue(showId);
    q.exec();
    if (!path.isEmpty())
        QFile::remove(path);
}

void Library::clearEpisodeImagePath(qint64 episodeId)
{
    const QString path = episode(episodeId).imagePath;
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("UPDATE episodes SET image_path = '' WHERE id = ?"));
    q.addBindValue(episodeId);
    q.exec();
    if (!path.isEmpty())
        QFile::remove(path);
}

bool Library::removeShow(qint64 showId)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("DELETE FROM shows WHERE id = ?"));
    q.addBindValue(showId);
    return q.exec();
}

int Library::markPlayed(qint64 episodeId, bool played)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("UPDATE episodes SET played = ? WHERE id = ? AND played != ?"));
    q.addBindValue(played ? 1 : 0);
    q.addBindValue(episodeId);
    q.addBindValue(played ? 1 : 0);
    if (!q.exec())
        return 0;
    return q.numRowsAffected();
}

int Library::markAllPlayed(qint64 showId)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("UPDATE episodes SET played = 1 WHERE show_id = ? AND played = 0"));
    q.addBindValue(showId);
    if (!q.exec())
        return 0;
    return q.numRowsAffected();
}

int Library::markLibraryPlayed()
{
    QSqlQuery q(dbOf(m_connection));
    if (!q.exec(QStringLiteral("UPDATE episodes SET played = 1 WHERE played = 0")))
        return 0;
    return q.numRowsAffected();
}

int Library::markOlderPlayed(qint64 episodeId)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral(
        "UPDATE episodes SET played = 1 WHERE played = 0"
        " AND show_id = (SELECT show_id FROM episodes WHERE id = ?)"
        " AND published_at < (SELECT published_at FROM episodes WHERE id = ?)"));
    q.addBindValue(episodeId);
    q.addBindValue(episodeId);
    if (!q.exec())
        return 0;
    return q.numRowsAffected();
}

int Library::markNewerPlayed(qint64 episodeId)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral(
        "UPDATE episodes SET played = 1 WHERE played = 0"
        " AND show_id = (SELECT show_id FROM episodes WHERE id = ?)"
        " AND published_at > (SELECT published_at FROM episodes WHERE id = ?)"));
    q.addBindValue(episodeId);
    q.addBindValue(episodeId);
    if (!q.exec())
        return 0;
    return q.numRowsAffected();
}

void Library::setPosition(qint64 episodeId, int positionMs)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("UPDATE episodes SET position_ms = ? WHERE id = ?"));
    q.addBindValue(qMax(0, positionMs));
    q.addBindValue(episodeId);
    q.exec();
}

qint64 Library::adjacent(qint64 episodeId, bool older, bool unplayedOnly) const
{
    const EpisodeRow current = episode(episodeId);
    if (current.id == 0)
        return 0;

    QString sql = QStringLiteral(
        "SELECT id FROM episodes WHERE show_id = ? AND (");
    if (older) {
        sql += QStringLiteral(
            "published_at < ? OR (published_at = ? AND id < ?))");
    } else {
        sql += QStringLiteral(
            "published_at > ? OR (published_at = ? AND id > ?))");
    }
    if (unplayedOnly)
        sql += QStringLiteral(" AND played = 0");
    sql += older ? QStringLiteral(" ORDER BY published_at DESC, id DESC LIMIT 1")
                 : QStringLiteral(" ORDER BY published_at ASC, id ASC LIMIT 1");

    QSqlQuery q(dbOf(m_connection));
    q.prepare(sql);
    q.addBindValue(current.showId);
    q.addBindValue(current.published);
    q.addBindValue(current.published);
    q.addBindValue(current.id);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : 0;
}

QList<qint64> Library::episodeIdsViaHost(const QString &host) const
{
    QList<qint64> ids;
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral(
        "SELECT e.id FROM episodes e JOIN shows s ON s.id = e.show_id"
        " WHERE s.feed_url LIKE ? OR e.audio_url LIKE ?"));
    const QString like = QLatin1Char('%') + host + QLatin1Char('%');
    q.addBindValue(like);
    q.addBindValue(like);
    if (q.exec())
        while (q.next())
            ids.append(q.value(0).toLongLong());
    return ids;
}

QString Library::audioCacheUa() const
{
    return setting(QStringLiteral("audio_cache_ua"), QString());
}

void Library::setAudioCacheUa(const QString &generation)
{
    setSetting(QStringLiteral("audio_cache_ua"), generation);
}

QString Library::setting(const QString &key, const QString &fallback) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(key);
    q.exec();
    return q.next() ? q.value(0).toString() : fallback;
}

void Library::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral(
        "INSERT INTO settings (key, value) VALUES (?, ?)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

double Library::rate() const
{
    bool ok = false;
    const double value = setting(QStringLiteral("rate"), QStringLiteral("1")).toDouble(&ok);
    if (!ok || value < 0.5 || value > 3.0)
        return 1.0;
    return value;
}

void Library::setRate(double rate)
{
    setSetting(QStringLiteral("rate"), QString::number(rate, 'f', 2));
}

double Library::volume() const
{
    bool ok = false;
    const double value = setting(QStringLiteral("volume"), QStringLiteral("1")).toDouble(&ok);
    if (!ok || value < 0.0 || value > 1.0)
        return 1.0;
    return value;
}

void Library::setVolume(double volume)
{
    const double clamped = qBound(0.0, volume, 1.0);
    setSetting(QStringLiteral("volume"), QString::number(clamped, 'f', 3));
}

bool Library::mono() const
{
    return setting(QStringLiteral("playback.mono"), QStringLiteral("0")) == QStringLiteral("1");
}

void Library::setMono(bool on)
{
    setSetting(QStringLiteral("playback.mono"), on ? QStringLiteral("1") : QStringLiteral("0"));
}

double Library::volumeBeforeMute() const
{
    bool ok = false;
    const double value = setting(QStringLiteral("volume_before_mute"), QStringLiteral("0")).toDouble(&ok);
    return ok ? qBound(0.0, value, 1.0) : 0.0;
}

void Library::setVolumeBeforeMute(double volume)
{
    setSetting(QStringLiteral("volume_before_mute"), QString::number(qBound(0.0, volume, 1.0), 'f', 3));
}

qint64 Library::lastPlayedEpisodeId() const
{
    bool ok = false;
    const qint64 value = setting(QStringLiteral("player.lastEpisode"), QStringLiteral("0")).toLongLong(&ok);
    if (!ok || value < 0)
        return 0;
    return value;
}

void Library::setLastPlayedEpisodeId(qint64 episodeId)
{
    setSetting(QStringLiteral("player.lastEpisode"),
               QString::number(qMax<qint64>(0, episodeId)));
}

QString Library::shelfView() const
{
    return setting(QStringLiteral("shelf.view"), QStringLiteral("gallery")) == QStringLiteral("list")
        ? QStringLiteral("list")
        : QStringLiteral("gallery");
}

void Library::setShelfView(const QString &view)
{
    setSetting(QStringLiteral("shelf.view"),
               view == QStringLiteral("list") ? QStringLiteral("list") : QStringLiteral("gallery"));
}

int Library::shelfColumns() const
{
    bool ok = false;
    const int value = setting(QStringLiteral("shelf.columns"), QStringLiteral("5")).toInt(&ok);
    if (!ok)
        return 5;
    return qBound(1, value, 10);
}

void Library::setShelfColumns(int columns)
{
    setSetting(QStringLiteral("shelf.columns"), QString::number(qBound(1, columns, 10)));
}

int Library::shelfListSize() const
{
    // 0 is the original list cover. ShelfView keeps five sizes, 0 through 4.
    bool ok = false;
    const int value = setting(QStringLiteral("shelf.listSize"), QStringLiteral("0")).toInt(&ok);
    if (!ok)
        return 0;
    return qBound(0, value, 4);
}

void Library::setShelfListSize(int size)
{
    setSetting(QStringLiteral("shelf.listSize"), QString::number(qBound(0, size, 4)));
}

int Library::episodeListSize() const
{
    // Same five steps as ShelfView list covers (0 through 4).
    bool ok = false;
    const int value = setting(QStringLiteral("show.listSize"), QStringLiteral("0")).toInt(&ok);
    if (!ok)
        return 0;
    return qBound(0, value, 4);
}

void Library::setEpisodeListSize(int size)
{
    setSetting(QStringLiteral("show.listSize"), QString::number(qBound(0, size, 4)));
}

bool Library::shelfShowAll() const
{
    return setting(QStringLiteral("shelf.showAll"), QStringLiteral("1")) != QStringLiteral("0");
}

void Library::setShelfShowAll(bool showAll)
{
    setSetting(QStringLiteral("shelf.showAll"), showAll ? QStringLiteral("1") : QStringLiteral("0"));
}

bool Library::episodeShowAll() const
{
    return setting(QStringLiteral("show.showAll"), QStringLiteral("1")) != QStringLiteral("0");
}

void Library::setEpisodeShowAll(bool showAll)
{
    setSetting(QStringLiteral("show.showAll"), showAll ? QStringLiteral("1") : QStringLiteral("0"));
}

QStringList Library::feedUrls() const
{
    QStringList urls;
    QSqlQuery q(dbOf(m_connection));
    q.exec(QStringLiteral("SELECT feed_url FROM shows ORDER BY title COLLATE NOCASE"));
    while (q.next())
        urls.append(q.value(0).toString());
    return urls;
}

QList<qint64> Library::queueIds() const
{
    QList<qint64> ids;
    QSqlQuery q(dbOf(m_connection));
    q.exec(QStringLiteral(
        "SELECT q.episode_id FROM queue q JOIN episodes e ON e.id = q.episode_id"
        " ORDER BY q.position"));
    while (q.next())
        ids.append(q.value(0).toLongLong());
    return ids;
}

QList<EpisodeRow> Library::queue() const
{
    QList<EpisodeRow> rows;
    for (qint64 id : queueIds()) {
        EpisodeRow row = episode(id);
        if (row.id == 0)
            continue;
        row.showTitle = showTitle(row.showId);
        if (row.imagePath.isEmpty() || !QFileInfo::exists(row.imagePath))
            row.imagePath = showImage(row.showId);
        rows.append(row);
    }
    return rows;
}

bool Library::isQueued(qint64 episodeId) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("SELECT 1 FROM queue WHERE episode_id = ?"));
    q.addBindValue(episodeId);
    return q.exec() && q.next();
}

qint64 Library::queueHead() const
{
    const QList<qint64> ids = queueIds();
    return ids.isEmpty() ? 0 : ids.first();
}

qint64 Library::queueAfter(qint64 current) const
{
    const QList<qint64> ids = queueIds();
    const int at = ids.indexOf(current);
    if (at < 0)
        return ids.isEmpty() ? 0 : ids.first();
    return at + 1 < ids.size() ? ids.at(at + 1) : 0;
}

qint64 Library::finishQueued(qint64 finished)
{
    const qint64 next = queueAfter(finished);
    removeFromQueue(finished);
    return next;
}

void Library::addToQueue(qint64 episodeId, bool atTop)
{
    if (episodeId == 0 || episode(episodeId).id == 0)
        return;
    QList<qint64> ids = queueIds();
    ids.removeAll(episodeId);
    if (atTop)
        ids.prepend(episodeId);
    else
        ids.append(episodeId);
    setQueueOrder(ids);
}

bool Library::removeFromQueue(qint64 episodeId)
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("DELETE FROM queue WHERE episode_id = ?"));
    q.addBindValue(episodeId);
    return q.exec() && q.numRowsAffected() > 0;
}

void Library::setQueueOrder(const QList<qint64> &ids)
{
    auto db = dbOf(m_connection);
    db.transaction();
    QSqlQuery q(db);
    q.exec(QStringLiteral("DELETE FROM queue"));
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO queue (episode_id, position) VALUES (?, ?)"));
    int pos = 0;
    for (qint64 id : ids) {
        q.addBindValue(id);
        q.addBindValue(pos++);
        q.exec();
    }
    db.commit();
}

void Library::clearQueue()
{
    QSqlQuery q(dbOf(m_connection));
    q.exec(QStringLiteral("DELETE FROM queue"));
}

bool Library::hasShow(qint64 showId) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral("SELECT 1 FROM shows WHERE id = ?"));
    q.addBindValue(showId);
    return q.exec() && q.next();
}

double Library::nowPlayingArtScale() const
{
    // Fraction of the largest square that fits the Now Playing page (0.2 .. 1.0).
    bool ok = false;
    const double value = setting(QStringLiteral("nowPlaying.artScale"), QStringLiteral("1")).toDouble(&ok);
    if (!ok)
        return 1.0;
    return qBound(0.2, value, 1.0);
}

void Library::setNowPlayingArtScale(double scale)
{
    setSetting(QStringLiteral("nowPlaying.artScale"), QString::number(qBound(0.2, scale, 1.0), 'f', 2));
}

QString Library::exploreCountry() const
{
    const QString code = setting(QStringLiteral("explore.country"), QString()).trimmed().toLower();
    return code.size() == 2 ? code : QString();
}

void Library::setExploreCountry(const QString &code)
{
    const QString clean = code.trimmed().toLower();
    // Non-null empty string: a null QString binds as SQL NULL and the write fails.
    setSetting(QStringLiteral("explore.country"), clean.size() == 2 ? clean : QStringLiteral(""));
}
