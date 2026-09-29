#include "library.h"

#include <QDir>
#include <QFileInfo>
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
            " audio_url TEXT NOT NULL,"
            " published_at INTEGER NOT NULL DEFAULT 0,"
            " duration_secs INTEGER NOT NULL DEFAULT 0,"
            " played INTEGER NOT NULL DEFAULT 0,"
            " position_ms INTEGER NOT NULL DEFAULT 0,"
            " UNIQUE(show_id, guid))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS settings ("
            " key TEXT PRIMARY KEY,"
            " value TEXT NOT NULL)"),
    };
    for (const auto &sql : statements) {
        if (!q.exec(sql)) {
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
        "SELECT id, show_id, guid, title, audio_url, published_at, duration_secs, played, position_ms"
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
        row.audioUrl = q.value(4).toString();
        row.published = q.value(5).toLongLong();
        row.durationSecs = q.value(6).toInt();
        row.played = q.value(7).toInt() != 0;
        row.positionMs = q.value(8).toInt();
        rows.append(row);
    }
    return rows;
}

EpisodeRow Library::episode(qint64 id) const
{
    QSqlQuery q(dbOf(m_connection));
    q.prepare(QStringLiteral(
        "SELECT id, show_id, guid, title, audio_url, published_at, duration_secs, played, position_ms"
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
    row.audioUrl = q.value(4).toString();
    row.published = q.value(5).toLongLong();
    row.durationSecs = q.value(6).toInt();
    row.played = q.value(7).toInt() != 0;
    row.positionMs = q.value(8).toInt();
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
        if (imageUrl != parsed.imageUrl)
            imagePath.clear();
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

    QSqlQuery upsert(db);
    upsert.prepare(QStringLiteral(
        "INSERT INTO episodes (show_id, guid, title, audio_url, published_at, duration_secs)"
        " VALUES (?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(show_id, guid) DO UPDATE SET"
        "  title = excluded.title,"
        "  audio_url = excluded.audio_url,"
        "  published_at = excluded.published_at,"
        "  duration_secs = CASE WHEN excluded.duration_secs > 0 THEN excluded.duration_secs"
        "                       ELSE episodes.duration_secs END"));
    for (const auto &ep : parsed.episodes) {
        upsert.addBindValue(showId);
        upsert.addBindValue(ep.guid);
        upsert.addBindValue(ep.title);
        upsert.addBindValue(ep.audioUrl);
        upsert.addBindValue(ep.published);
        upsert.addBindValue(ep.durationSecs);
        if (!upsert.exec()) {
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

QStringList Library::feedUrls() const
{
    QStringList urls;
    QSqlQuery q(dbOf(m_connection));
    q.exec(QStringLiteral("SELECT feed_url FROM shows ORDER BY title COLLATE NOCASE"));
    while (q.next())
        urls.append(q.value(0).toString());
    return urls;
}
