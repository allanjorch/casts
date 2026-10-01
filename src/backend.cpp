#include "backend.h"

#include "feed.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QProcess>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {
const QString kPlayerService = QStringLiteral("com.github.allanjorch.podcast.Player");
const QString kPlayerPath = QStringLiteral("/com/github/allanjorch/podcast");
const QString kPlayerIface = QStringLiteral("com.github.allanjorch.podcast.Player");

const QList<double> kRates = {1.0, 1.2, 1.5, 1.8, 2.0, 0.8};

QString coverExtension(const QByteArray &bytes, const QString &contentType)
{
    if (bytes.startsWith("\x89PNG") || contentType.contains(QStringLiteral("png")))
        return QStringLiteral("png");
    if (bytes.startsWith("GIF") || contentType.contains(QStringLiteral("gif")))
        return QStringLiteral("gif");
    if (bytes.startsWith("RIFF") || contentType.contains(QStringLiteral("webp")))
        return QStringLiteral("webp");
    return QStringLiteral("jpg");
}

QString episodeCoverUrl(const EpisodeRow &row)
{
    if (!row.imagePath.isEmpty() && QFileInfo::exists(row.imagePath))
        return QUrl::fromLocalFile(row.imagePath).toString();
    return row.imageUrl;
}

QString showCoverUrl(const Library &library, qint64 showId)
{
    const QString path = library.showImage(showId);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    return QUrl::fromLocalFile(path).toString();
}

// Episode local cache, then remote URL, then show cover. Used by Now Playing / PlayerBar.
QString preferredPlayerArt(const Library &library, const EpisodeRow &row)
{
    const QString episodeArt = episodeCoverUrl(row);
    if (!episodeArt.isEmpty())
        return episodeArt;
    return showCoverUrl(library, row.showId);
}
}

Backend::Backend(Library &library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
    m_playerRate = m_library.rate();
    m_playerVolume = m_library.volume();
    reloadShows();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Backend::pollPlayer);
    timer->start(500);
    QTimer::singleShot(0, this, [this]() {
        restoreLastPlayed();
        refreshAll();
    });
}

void Backend::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

QList<ShowRow> Backend::visibleShows() const
{
    const auto rows = m_library.shows();
    if (m_library.shelfShowAll())
        return rows;
    QList<ShowRow> filtered;
    filtered.reserve(rows.size());
    for (const auto &row : rows) {
        if (row.unheard > 0)
            filtered.append(row);
    }
    return filtered;
}

QList<EpisodeRow> Backend::visibleEpisodes(qint64 showId) const
{
    const auto rows = m_library.episodes(showId);
    if (m_library.episodeShowAll())
        return rows;
    QList<EpisodeRow> filtered;
    filtered.reserve(rows.size());
    for (const auto &row : rows) {
        if (!row.played)
            filtered.append(row);
    }
    return filtered;
}

void Backend::reloadShows()
{
    m_shows.setRows(visibleShows());
    if (m_openShowId != 0) {
        bool found = false;
        const auto rows = m_library.shows();
        for (const auto &row : rows) {
            if (row.id == m_openShowId) {
                m_openShowTitle = row.title;
                m_openShowCover = showCoverUrl(m_library, row.id);
                m_openShowUnheard = row.unheard;
                found = true;
                break;
            }
        }
        if (!found) {
            m_openShowId = 0;
            m_openShowTitle.clear();
            m_openShowCover.clear();
            m_openShowUnheard = 0;
            m_openEpisodeId = 0;
            m_openEpisodeTitle.clear();
            m_openEpisodeDescription.clear();
            m_openEpisodeCover.clear();
            m_openEpisodePublished = 0;
            m_openEpisodeDuration = 0;
            m_openEpisodePlayed = false;
            m_openEpisodePositionMs = 0;
            m_episodes.setRows({});
            emit openEpisodeChanged();
        }
        emit openShowChanged();
        refreshOpenEpisode();
    }
}

void Backend::reloadEpisodes()
{
    if (m_openShowId == 0)
        return;
    m_episodes.setRows(visibleEpisodes(m_openShowId));
    reloadShows();
    refreshOpenEpisode();
}

void Backend::addFeed(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return;
    enqueue({trimmed});
}

void Backend::importOpml(const QString &fileUrl)
{
    const QString path = QUrl(fileUrl).toLocalFile();
    QFile file(path.isEmpty() ? fileUrl : path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Could not open that file."));
        return;
    }
    QString error;
    const QStringList urls = parseOpml(file.readAll(), &error);
    if (urls.isEmpty()) {
        setStatus(error.isEmpty() ? QStringLiteral("That OPML file has no feeds.") : error);
        return;
    }
    enqueue(urls);
}

void Backend::refreshAll()
{
    const QStringList urls = m_library.feedUrls();
    if (urls.isEmpty())
        return;
    m_shelfRefresh = true;
    enqueue(urls);
}

void Backend::refreshOpenShow()
{
    if (m_openShowId == 0)
        return;
    const auto rows = m_library.shows();
    for (const auto &row : rows) {
        if (row.id == m_openShowId) {
            enqueue({row.feedUrl});
            return;
        }
    }
}

void Backend::openShow(qint64 showId)
{
    const bool episodeOpen = m_openEpisodeId != 0;
    m_openShowId = showId;
    m_episodes.setRows(visibleEpisodes(showId));
    m_openShowTitle = m_library.showTitle(showId);
    m_openShowCover = showCoverUrl(m_library, showId);
    m_openShowUnheard = 0;
    for (const auto &row : m_library.shows()) {
        if (row.id == showId) {
            m_openShowUnheard = row.unheard;
            break;
        }
    }
    if (episodeOpen)
        closeEpisode();
    emit openShowChanged();
}

void Backend::closeShow()
{
    if (m_openShowId == 0)
        return;
    if (m_openEpisodeId != 0)
        closeEpisode();
    m_openShowId = 0;
    m_openShowTitle.clear();
    m_openShowCover.clear();
    m_openShowUnheard = 0;
    m_episodes.setRows({});
    emit openShowChanged();
}

void Backend::refreshOpenEpisode()
{
    if (m_openEpisodeId == 0)
        return;
    const EpisodeRow row = m_library.episode(m_openEpisodeId);
    if (row.id == 0 || (m_openShowId != 0 && row.showId != m_openShowId)) {
        closeEpisode();
        return;
    }
    m_openEpisodeTitle = row.title;
    m_openEpisodeDescription = row.description;
    m_openEpisodeCover = episodeCoverUrl(row);
    m_openEpisodePublished = row.published;
    m_openEpisodeDuration = row.durationSecs;
    m_openEpisodePlayed = row.played;
    m_openEpisodePositionMs = row.positionMs;
    emit openEpisodeChanged();
}

void Backend::openEpisode(qint64 episodeId)
{
    if (episodeId == 0)
        return;
    const EpisodeRow row = m_library.episode(episodeId);
    if (row.id == 0)
        return;
    m_returnToShelfOnClose = false;
    if (m_openShowId == 0 || m_openShowId != row.showId) {
        // openShow clears any prior episode; set this one after.
        m_openShowId = row.showId;
        m_episodes.setRows(visibleEpisodes(row.showId));
        m_openShowTitle = m_library.showTitle(row.showId);
        m_openShowCover = showCoverUrl(m_library, row.showId);
        m_openShowUnheard = 0;
        for (const auto &show : m_library.shows()) {
            if (show.id == row.showId) {
                m_openShowUnheard = show.unheard;
                break;
            }
        }
        emit openShowChanged();
    }
    m_openEpisodeId = row.id;
    m_openEpisodeTitle = row.title;
    m_openEpisodeDescription = row.description;
    m_openEpisodeCover = episodeCoverUrl(row);
    m_openEpisodePublished = row.published;
    m_openEpisodeDuration = row.durationSecs;
    m_openEpisodePlayed = row.played;
    m_openEpisodePositionMs = row.positionMs;
    emit openEpisodeChanged();
    if (!row.imageUrl.isEmpty() && row.imagePath.isEmpty())
        downloadEpisodeCover(row.id, row.imageUrl);
}

void Backend::closeEpisode()
{
    if (m_openEpisodeId == 0)
        return;
    const bool returnToShelf = m_returnToShelfOnClose;
    m_returnToShelfOnClose = false;
    m_openEpisodeId = 0;
    m_openEpisodeTitle.clear();
    m_openEpisodeDescription.clear();
    m_openEpisodeCover.clear();
    m_openEpisodePublished = 0;
    m_openEpisodeDuration = 0;
    m_openEpisodePlayed = false;
    m_openEpisodePositionMs = 0;
    if (returnToShelf && m_openShowId != 0) {
        m_openShowId = 0;
        m_openShowTitle.clear();
        m_openShowCover.clear();
        m_openShowUnheard = 0;
        m_episodes.setRows({});
        emit openEpisodeChanged();
        emit openShowChanged();
        return;
    }
    emit openEpisodeChanged();
}

void Backend::openPlayingEpisode()
{
    if (m_playerEpisodeId == 0)
        return;
    openEpisode(m_playerEpisodeId);
    m_returnToShelfOnClose = true;
}

void Backend::removeOpenShow()
{
    if (m_openShowId == 0)
        return;
    const qint64 showId = m_openShowId;
    if (m_playerShowId == showId)
        stopPlayback();
    const qint64 lastId = m_library.lastPlayedEpisodeId();
    if (lastId != 0) {
        const EpisodeRow last = m_library.episode(lastId);
        if (last.id == 0 || last.showId == showId)
            m_library.setLastPlayedEpisodeId(0);
    }
    const QString title = m_openShowTitle;
    const QString image = m_library.showImage(showId);
    QStringList episodeImages;
    for (const auto &row : m_library.episodes(showId)) {
        if (!row.imagePath.isEmpty())
            episodeImages.append(row.imagePath);
    }
    m_library.removeShow(showId);
    if (!image.isEmpty())
        QFile::remove(image);
    for (const QString &path : episodeImages)
        QFile::remove(path);
    closeShow();
    reloadShows();
    setStatus(QStringLiteral("Removed %1.").arg(title));
}

void Backend::enqueue(const QStringList &urls)
{
    for (const QString &url : urls) {
        bool queued = false;
        for (const auto &job : m_queue) {
            if (job.url == url) {
                queued = true;
                break;
            }
        }
        if (!queued)
            m_queue.append({url});
    }
    fetchNext();
}

void Backend::fetchNext()
{
    if (m_active)
        return;
    if (m_queue.isEmpty()) {
        setBusy(false);
        m_shelfRefresh = false;
        return;
    }
    const Job job = m_queue.takeFirst();
    setBusy(true);
    const int left = m_queue.size() + 1;
    setStatus(left > 1 ? QStringLiteral("Updating feeds, %1 left…").arg(left)
                       : QStringLiteral("Updating %1…").arg(job.url));

    QNetworkRequest request{QUrl(job.url)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("podcast/0.1 (Omarchy; +https://github.com/allanjorch)"));
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.get(request);
    m_active = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, job]() {
        reply->deleteLater();
        m_active = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(reply->errorString());
            fetchNext();
            return;
        }
        QString error;
        const auto parsed = parseFeed(reply->readAll(), reply->url(), &error);
        if (!parsed) {
            setStatus(error.isEmpty() ? QStringLiteral("Could not read that feed.") : error);
            fetchNext();
            return;
        }
        const qint64 showId = m_library.upsertShow(job.url, *parsed);
        if (showId == 0) {
            setStatus(QStringLiteral("Could not save that show."));
            fetchNext();
            return;
        }
        if (!parsed->imageUrl.isEmpty())
            downloadCover(showId, parsed->imageUrl);
        reloadShows();
        if (m_openShowId == showId)
            openShow(showId);
        const QString title = parsed->title;
        if (m_queue.isEmpty()) {
            setStatus(m_shelfRefresh ? QStringLiteral("All podcasts updated.")
                                     : QStringLiteral("Updated %1.").arg(title));
        }
        fetchNext();
    });
}

void Backend::downloadCover(qint64 showId, const QString &imageUrl)
{
    if (showId == 0 || imageUrl.isEmpty())
        return;
    // Skip when the library already points at a file on disk (refresh must not re-hit the CDN).
    // upsertShow clears image_path when the remote URL changes, so a new cover still downloads.
    const QString existing = m_library.showImage(showId);
    if (!existing.isEmpty() && QFileInfo::exists(existing))
        return;
    if (m_showCoverDownloads.contains(showId))
        return;
    m_showCoverDownloads.insert(showId);
    QNetworkRequest request{QUrl(imageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("podcast/0.1 (Omarchy)"));
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, showId, imageUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_showCoverDownloads.remove(showId);
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.size() < 32) {
            m_showCoverDownloads.remove(showId);
            return;
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/covers");
        QDir().mkpath(dir);
        const QString ext = coverExtension(
            bytes, reply->header(QNetworkRequest::ContentTypeHeader).toString());
        const QString path = QStringLiteral("%1/%2.%3").arg(dir).arg(showId).arg(ext);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            m_showCoverDownloads.remove(showId);
            return;
        }
        file.write(bytes);
        file.close();
        m_library.setShowImage(showId, imageUrl, path);
        m_showCoverDownloads.remove(showId);
        reloadShows();
        if (m_openShowId == showId)
            reloadEpisodes();
    });
}

void Backend::downloadEpisodeCover(qint64 episodeId, const QString &imageUrl)
{
    if (episodeId == 0 || imageUrl.isEmpty())
        return;
    if (m_episodeCoverDownloads.contains(episodeId))
        return;
    m_episodeCoverDownloads.insert(episodeId);
    QNetworkRequest request{QUrl(imageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("podcast/0.1 (Omarchy)"));
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, episodeId, imageUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_episodeCoverDownloads.remove(episodeId);
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.size() < 32) {
            m_episodeCoverDownloads.remove(episodeId);
            return;
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/episode-covers");
        QDir().mkpath(dir);
        const QString ext = coverExtension(
            bytes, reply->header(QNetworkRequest::ContentTypeHeader).toString());
        const QString path = QStringLiteral("%1/%2.%3").arg(dir).arg(episodeId).arg(ext);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            m_episodeCoverDownloads.remove(episodeId);
            return;
        }
        file.write(bytes);
        file.close();
        m_library.setEpisodeImage(episodeId, imageUrl, path);
        m_episodeCoverDownloads.remove(episodeId);
        if (m_openShowId != 0)
            reloadEpisodes();
        else if (m_openEpisodeId == episodeId)
            refreshOpenEpisode();
        // Swap Now Playing / bar art to the episode cover once it lands.
        if (m_playerEpisodeId == episodeId) {
            m_playerArt = QUrl::fromLocalFile(path).toString();
            emit playerStateChanged();
            // Keep the detached player / MPRIS art in sync with the local cache.
            if (QDBusConnection::sessionBus().interface()
                    ->isServiceRegistered(kPlayerService)) {
                if (!m_player) {
                    m_player = new QDBusInterface(kPlayerService, kPlayerPath, kPlayerIface,
                                                  QDBusConnection::sessionBus(), this);
                }
                m_player->call(QDBus::Block, QStringLiteral("RefreshArt"));
            }
        }
    });
}

bool Backend::playerReady() const
{
    auto *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(kPlayerService);
}

void Backend::startPlayerService()
{
    if (m_startingPlayer)
        return;
    if (playerReady()) {
        flushPendingPlayerCalls();
        return;
    }
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 {QStringLiteral("--player")})) {
        m_pendingPlayerCalls.clear();
        setStatus(QStringLiteral("Could not start playback."));
        return;
    }
    m_startingPlayer = true;

    // Wait for the player on the bus without processEvents — nesting that from a
    // QML click handler can flush deleteLater while a ListView delegate's signal
    // is still running (cover download → reloadEpisodes → model reset → qFatal).
    if (!m_playerWatcher) {
        m_playerWatcher = new QDBusServiceWatcher(
            kPlayerService, QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration, this);
        connect(m_playerWatcher, &QDBusServiceWatcher::serviceRegistered,
                this, &Backend::onPlayerServiceRegistered);
    }
    if (!m_playerStartTimer) {
        m_playerStartTimer = new QTimer(this);
        m_playerStartTimer->setSingleShot(true);
        connect(m_playerStartTimer, &QTimer::timeout, this, &Backend::onPlayerStartTimeout);
    }
    m_playerStartTimer->start(2000);

    // Registration can land before the watcher is connected; recheck once.
    if (playerReady()) {
        m_playerStartTimer->stop();
        m_startingPlayer = false;
        flushPendingPlayerCalls();
    }
}

void Backend::onPlayerServiceRegistered(const QString &service)
{
    if (service != kPlayerService)
        return;
    if (m_playerStartTimer)
        m_playerStartTimer->stop();
    m_startingPlayer = false;
    flushPendingPlayerCalls();
}

void Backend::onPlayerStartTimeout()
{
    if (playerReady()) {
        m_startingPlayer = false;
        flushPendingPlayerCalls();
        return;
    }
    m_startingPlayer = false;
    m_pendingPlayerCalls.clear();
    setStatus(QStringLiteral("Playback did not start."));
}

void Backend::flushPendingPlayerCalls()
{
    if (!playerReady())
        return;
    const auto pending = m_pendingPlayerCalls;
    m_pendingPlayerCalls.clear();
    for (const auto &call : pending)
        invokePlayer(call.method, call.args);
}

void Backend::invokePlayer(const QString &method, const QVariantList &args)
{
    if (!m_player) {
        m_player = new QDBusInterface(kPlayerService, kPlayerPath, kPlayerIface,
                                      QDBusConnection::sessionBus(), this);
    }
    m_player->callWithArgumentList(QDBus::Block, method, args);
    pollPlayer();
}

void Backend::callPlayer(const QString &method, const QVariantList &args)
{
    if (playerReady()) {
        m_startingPlayer = false;
        if (m_playerStartTimer)
            m_playerStartTimer->stop();
        // Run anything queued while the service was coming up, then this call.
        flushPendingPlayerCalls();
        invokePlayer(method, args);
        return;
    }
    m_pendingPlayerCalls.append({method, args});
    startPlayerService();
}

void Backend::playEpisode(qint64 episodeId)
{
    if (episodeId == 0)
        return;
    if (m_playerEpisodeId == episodeId && m_playerStatus == QStringLiteral("playing")) {
        togglePlayback();
        return;
    }
    const EpisodeRow row = m_library.episode(episodeId);
    if (!row.imageUrl.isEmpty() && row.imagePath.isEmpty())
        downloadEpisodeCover(row.id, row.imageUrl);
    callPlayer(QStringLiteral("Load"), {QVariant::fromValue<qlonglong>(episodeId)});
}

void Backend::togglePlayback()
{
    callPlayer(QStringLiteral("PlayPause"));
}

void Backend::stopPlayback()
{
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(kPlayerService))
        callPlayer(QStringLiteral("Stop"));
    else
        applyPlayerState({});
}

void Backend::seekTo(double seconds)
{
    callPlayer(QStringLiteral("SeekTo"), {seconds});
}

void Backend::setVolume(double volume)
{
    const double clamped = qBound(0.0, volume, 1.0);
    if (qAbs(clamped - m_playerVolume) < 0.0005)
        return;
    m_playerVolume = clamped;
    m_library.setVolume(clamped);
    emit playerStateChanged();
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(kPlayerService))
        callPlayer(QStringLiteral("SetVolume"), {clamped});
}

void Backend::cycleRate()
{
    double next = kRates.first();
    for (int i = 0; i < kRates.size(); ++i) {
        if (qAbs(m_playerRate - kRates.at(i)) < 0.05) {
            next = kRates.at((i + 1) % kRates.size());
            break;
        }
    }
    m_library.setRate(next);
    m_playerRate = next;
    emit playerStateChanged();
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(kPlayerService))
        callPlayer(QStringLiteral("SetRate"), {next});
}

QString Backend::shelfView() const
{
    return m_library.shelfView();
}

int Backend::shelfColumns() const
{
    return m_library.shelfColumns();
}

void Backend::setShelfView(const QString &view)
{
    const QString next = view == QStringLiteral("list") ? QStringLiteral("list") : QStringLiteral("gallery");
    if (next == m_library.shelfView())
        return;
    m_library.setShelfView(next);
    emit shelfLayoutChanged();
}

void Backend::setShelfColumns(int columns)
{
    const int next = qBound(2, columns, 8);
    if (next == m_library.shelfColumns())
        return;
    m_library.setShelfColumns(next);
    emit shelfLayoutChanged();
}

int Backend::shelfListSize() const
{
    return m_library.shelfListSize();
}

void Backend::setShelfListSize(int size)
{
    const int next = qBound(0, size, 4);
    if (next == m_library.shelfListSize())
        return;
    m_library.setShelfListSize(next);
    emit shelfLayoutChanged();
}

int Backend::episodeListSize() const
{
    return m_library.episodeListSize();
}

void Backend::setEpisodeListSize(int size)
{
    const int next = qBound(0, size, 4);
    if (next == m_library.episodeListSize())
        return;
    m_library.setEpisodeListSize(next);
    emit shelfLayoutChanged();
}

bool Backend::shelfShowAll() const
{
    return m_library.shelfShowAll();
}

bool Backend::episodeShowAll() const
{
    return m_library.episodeShowAll();
}

void Backend::setShelfShowAll(bool showAll)
{
    if (showAll == m_library.shelfShowAll())
        return;
    m_library.setShelfShowAll(showAll);
    reloadShows();
    emit filterChanged();
}

void Backend::setEpisodeShowAll(bool showAll)
{
    if (showAll == m_library.episodeShowAll())
        return;
    m_library.setEpisodeShowAll(showAll);
    if (m_openShowId != 0)
        m_episodes.setRows(visibleEpisodes(m_openShowId));
    emit filterChanged();
}

void Backend::markPlayed(qint64 episodeId, bool played)
{
    const int count = m_library.markPlayed(episodeId, played);
    reloadEpisodes();
    reloadShows();
    if (count == 0)
        return;
    setStatus(played ? QStringLiteral("Marked as played.")
                     : QStringLiteral("Marked as unplayed."));
}

void Backend::markAllPlayed(qint64 showId)
{
    if (showId == 0)
        showId = m_openShowId;
    const int count = m_library.markAllPlayed(showId);
    reloadEpisodes();
    reloadShows();
    setStatus(countMessage(count, QStringLiteral("episode")));
}

void Backend::markOlderPlayed(qint64 episodeId)
{
    const int count = m_library.markOlderPlayed(episodeId);
    reloadEpisodes();
    reloadShows();
    setStatus(count == 0 ? QStringLiteral("Nothing older than this episode.")
                         : count == 1 ? QStringLiteral("Marked 1 older episode as played.")
                                      : QStringLiteral("Marked %1 older episodes as played.").arg(count));
}

void Backend::markNewerPlayed(qint64 episodeId)
{
    const int count = m_library.markNewerPlayed(episodeId);
    reloadEpisodes();
    reloadShows();
    setStatus(count == 0 ? QStringLiteral("Nothing newer than this episode.")
                         : count == 1 ? QStringLiteral("Marked 1 newer episode as played.")
                                      : QStringLiteral("Marked %1 newer episodes as played.").arg(count));
}

QString Backend::countMessage(int count, const QString &what) const
{
    if (count == 0)
        return QStringLiteral("Nothing left to mark as played.");
    if (count == 1)
        return QStringLiteral("Marked 1 %1 as played.").arg(what);
    return QStringLiteral("Marked %1 %2s as played.").arg(count).arg(what);
}

void Backend::restoreLastPlayed()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    if (bus && bus->isServiceRegistered(kPlayerService)) {
        pollPlayer();
        if (m_playerEpisodeId != 0)
            return;
    }
    const qint64 id = m_library.lastPlayedEpisodeId();
    if (id == 0)
        return;
    const EpisodeRow row = m_library.episode(id);
    if (row.id == 0) {
        m_library.setLastPlayedEpisodeId(0);
        return;
    }
    if (!row.imageUrl.isEmpty() && row.imagePath.isEmpty())
        downloadEpisodeCover(row.id, row.imageUrl);
    callPlayer(QStringLiteral("LoadPaused"), {QVariant::fromValue<qlonglong>(id)});
}

void Backend::pollPlayer()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(kPlayerService)) {
        if (m_playerEpisodeId != 0 || m_playerStatus != QStringLiteral("stopped"))
            applyPlayerState({});
        return;
    }
    if (!m_player) {
        m_player = new QDBusInterface(kPlayerService, kPlayerPath, kPlayerIface,
                                      QDBusConnection::sessionBus(), this);
    }
    const QDBusReply<QVariantMap> reply = m_player->call(QStringLiteral("State"));
    if (!reply.isValid())
        return;
    applyPlayerState(reply.value());
}

void Backend::applyPlayerState(const QVariantMap &state)
{
    const qint64 episodeId = state.value(QStringLiteral("episodeId")).toLongLong();
    const qint64 showId = state.value(QStringLiteral("showId")).toLongLong();
    const QString title = state.value(QStringLiteral("title")).toString();
    const QString showTitle = state.value(QStringLiteral("showTitle")).toString();
    QString art = state.value(QStringLiteral("art")).toString();
    // Prefer library episode art (cache or URL) over whatever the player last published,
    // so Now Playing / PlayerBar never stay stuck on show cover after a download lands.
    if (episodeId != 0) {
        const EpisodeRow row = m_library.episode(episodeId);
        if (row.id != 0) {
            const QString preferred = preferredPlayerArt(m_library, row);
            if (!preferred.isEmpty())
                art = preferred;
        }
    }
    const QString status = state.value(QStringLiteral("status"), QStringLiteral("stopped")).toString();
    const QString error = state.value(QStringLiteral("error")).toString();
    const bool played = state.value(QStringLiteral("played")).toBool();
    const double rate = state.value(QStringLiteral("rate"), m_library.rate()).toDouble();
    const double volume = state.contains(QStringLiteral("volume"))
        ? state.value(QStringLiteral("volume")).toDouble()
        : m_library.volume();
    const QString description = state.value(QStringLiteral("description")).toString();
    const double position = state.value(QStringLiteral("position")).toDouble();
    const double duration = state.value(QStringLiteral("duration")).toDouble();

    const bool structural = episodeId != m_playerEpisodeId || showId != m_playerShowId
        || title != m_playerTitle || showTitle != m_playerShowTitle || art != m_playerArt
        || status != m_playerStatus || error != m_playerError || played != m_playerPlayed
        || description != m_playerDescription
        || qAbs(rate - m_playerRate) > 0.001 || qAbs(volume - m_playerVolume) > 0.001
        || qAbs(duration - m_playerDuration) > 0.5;
    const bool playedFlipped = played != m_sawPlayed && episodeId != 0;
    m_sawPlayed = played;

    m_playerEpisodeId = episodeId;
    m_playerShowId = showId;
    m_playerTitle = title;
    m_playerShowTitle = showTitle;
    m_playerArt = art;
    m_playerDescription = description;
    m_playerStatus = status.isEmpty() ? QStringLiteral("stopped") : status;
    m_playerError = error;
    m_playerPlayed = played;
    m_playerRate = rate > 0 ? rate : 1;
    m_playerVolume = qBound(0.0, volume, 1.0);
    m_playerDuration = duration;
    if (qAbs(position - m_playerPosition) > 0.05) {
        m_playerPosition = position;
        emit playerPositionChanged();
    }
    if (structural)
        emit playerStateChanged();
    if (playedFlipped) {
        reloadEpisodes();
        reloadShows();
    }
}

QVariantMap Backend::windowGeometry() const
{
    QSettings settings;
    QVariantMap map;
    if (!settings.contains(QStringLiteral("window/width")))
        return map;
    map.insert(QStringLiteral("valid"), true);
    map.insert(QStringLiteral("x"), settings.value(QStringLiteral("window/x")).toInt());
    map.insert(QStringLiteral("y"), settings.value(QStringLiteral("window/y")).toInt());
    map.insert(QStringLiteral("width"), settings.value(QStringLiteral("window/width")).toInt());
    map.insert(QStringLiteral("height"), settings.value(QStringLiteral("window/height")).toInt());
    map.insert(QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized")).toBool());
    return map;
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/x"), x);
    settings.setValue(QStringLiteral("window/y"), y);
    settings.setValue(QStringLiteral("window/width"), width);
    settings.setValue(QStringLiteral("window/height"), height);
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::raiseWindow()
{
    emit raised();
}
