#include "backend.h"

#include "feed.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QProcess>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
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
}

Backend::Backend(Library &library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
    m_playerRate = m_library.rate();
    reloadShows();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Backend::pollPlayer);
    timer->start(500);
    QTimer::singleShot(0, this, [this]() { refreshAll(); });
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

void Backend::reloadShows()
{
    m_shows.setRows(m_library.shows());
    if (m_openShowId != 0) {
        bool found = false;
        const auto rows = m_library.shows();
        for (const auto &row : rows) {
            if (row.id == m_openShowId) {
                m_openShowTitle = row.title;
                m_openShowUnheard = row.unheard;
                found = true;
                break;
            }
        }
        if (!found) {
            m_openShowId = 0;
            m_openShowTitle.clear();
            m_openShowUnheard = 0;
            m_episodes.setRows({});
        }
        emit openShowChanged();
    }
}

void Backend::reloadEpisodes()
{
    if (m_openShowId == 0)
        return;
    m_episodes.setRows(m_library.episodes(m_openShowId));
    reloadShows();
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
    m_openShowId = showId;
    m_episodes.setRows(m_library.episodes(showId));
    m_openShowTitle = m_library.showTitle(showId);
    m_openShowUnheard = 0;
    for (const auto &row : m_library.shows()) {
        if (row.id == showId) {
            m_openShowUnheard = row.unheard;
            break;
        }
    }
    emit openShowChanged();
}

void Backend::closeShow()
{
    if (m_openShowId == 0)
        return;
    m_openShowId = 0;
    m_openShowTitle.clear();
    m_openShowUnheard = 0;
    m_episodes.setRows({});
    emit openShowChanged();
}

void Backend::removeOpenShow()
{
    if (m_openShowId == 0)
        return;
    const qint64 showId = m_openShowId;
    if (m_playerShowId == showId)
        stopPlayback();
    const QString title = m_openShowTitle;
    const QString image = m_library.showImage(showId);
    m_library.removeShow(showId);
    if (!image.isEmpty())
        QFile::remove(image);
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
        if (m_queue.isEmpty())
            setStatus(QStringLiteral("Updated %1.").arg(title));
        fetchNext();
    });
}

void Backend::downloadCover(qint64 showId, const QString &imageUrl)
{
    QNetworkRequest request{QUrl(imageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("podcast/0.1 (Omarchy)"));
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, showId, imageUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray bytes = reply->readAll();
        if (bytes.size() < 32)
            return;
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/covers");
        QDir().mkpath(dir);
        const QString ext = coverExtension(
            bytes, reply->header(QNetworkRequest::ContentTypeHeader).toString());
        const QString path = QStringLiteral("%1/%2.%3").arg(dir).arg(showId).arg(ext);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(bytes);
        file.close();
        m_library.setShowImage(showId, imageUrl, path);
        reloadShows();
        if (m_openShowId == showId)
            reloadEpisodes();
    });
}

bool Backend::ensurePlayer()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    if (bus && bus->isServiceRegistered(kPlayerService))
        return true;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 {QStringLiteral("--player")})) {
        setStatus(QStringLiteral("Could not start playback."));
        return false;
    }
    for (int i = 0; i < 40; ++i) {
        QThread::msleep(50);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (bus && bus->isServiceRegistered(kPlayerService))
            return true;
    }
    setStatus(QStringLiteral("Playback did not start."));
    return false;
}

void Backend::callPlayer(const QString &method, const QVariantList &args)
{
    if (!ensurePlayer())
        return;
    if (!m_player) {
        m_player = new QDBusInterface(kPlayerService, kPlayerPath, kPlayerIface,
                                      QDBusConnection::sessionBus(), this);
    }
    m_player->callWithArgumentList(QDBus::Block, method, args);
    pollPlayer();
}

void Backend::playEpisode(qint64 episodeId)
{
    if (episodeId == 0)
        return;
    if (m_playerEpisodeId == episodeId && m_playerStatus == QStringLiteral("playing")) {
        togglePlayback();
        return;
    }
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
    const QString art = state.value(QStringLiteral("art")).toString();
    const QString status = state.value(QStringLiteral("status"), QStringLiteral("stopped")).toString();
    const QString error = state.value(QStringLiteral("error")).toString();
    const bool played = state.value(QStringLiteral("played")).toBool();
    const double rate = state.value(QStringLiteral("rate"), m_library.rate()).toDouble();
    const double position = state.value(QStringLiteral("position")).toDouble();
    const double duration = state.value(QStringLiteral("duration")).toDouble();

    const bool structural = episodeId != m_playerEpisodeId || showId != m_playerShowId
        || title != m_playerTitle || showTitle != m_playerShowTitle || art != m_playerArt
        || status != m_playerStatus || error != m_playerError || played != m_playerPlayed
        || qAbs(rate - m_playerRate) > 0.001 || qAbs(duration - m_playerDuration) > 0.5;
    const bool playedFlipped = played != m_sawPlayed && episodeId != 0;
    m_sawPlayed = played;

    m_playerEpisodeId = episodeId;
    m_playerShowId = showId;
    m_playerTitle = title;
    m_playerShowTitle = showTitle;
    m_playerArt = art;
    m_playerStatus = status.isEmpty() ? QStringLiteral("stopped") : status;
    m_playerError = error;
    m_playerPlayed = played;
    m_playerRate = rate > 0 ? rate : 1;
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
