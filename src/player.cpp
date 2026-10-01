#include "player.h"

#include "library.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QPlaybackOptions>
#include <QProcess>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <chrono>

namespace {
const QString kUiService = QStringLiteral("com.github.allanjorch.podcast");
const QString kPlayerService = QStringLiteral("com.github.allanjorch.podcast.Player");
const QString kMprisService = QStringLiteral("org.mpris.MediaPlayer2.podcast");
constexpr int kMaxErrorRecoveries = 3;

bool uiIsOpen()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(kUiService);
}

bool isRecoverablePlaybackError(QMediaPlayer::Error error, const QString &message)
{
    if (error == QMediaPlayer::NetworkError || error == QMediaPlayer::ResourceError)
        return true;
    const QString lower = message.toLower();
    // Qt FFmpeg demuxer: "Demuxing failed" on dead HTTP sessions after sleep/blips.
    return lower.contains(QStringLiteral("demux"))
        || lower.contains(QStringLiteral("network"))
        || lower.contains(QStringLiteral("connection"))
        || lower.contains(QStringLiteral("timed out"))
        || lower.contains(QStringLiteral("timeout"))
        || lower.contains(QStringLiteral("server returned"))
        || lower.contains(QStringLiteral("input/output"))
        || lower.contains(QStringLiteral("i/o error"));
}

QString friendlyPlaybackError(QMediaPlayer::Error error, const QString &message, bool giveUp)
{
    if (isRecoverablePlaybackError(error, message)) {
        return giveUp ? QStringLiteral("Stream interrupted. Tap play to retry.")
                      : QStringLiteral("Stream interrupted. Reconnecting…");
    }
    if (error == QMediaPlayer::FormatError)
        return QStringLiteral("This episode’s audio format isn’t supported.");
    if (message.isEmpty())
        return QStringLiteral("Playback failed.");
    return message;
}
}

class PlayerService;

class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(QObject *parent, PlayerService *player)
        : QDBusAbstractAdaptor(parent)
        , m_player(player)
    {
    }

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool canSetFullscreen() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Podcasts"); }
    QString desktopEntry() const { return QStringLiteral("com.github.allanjorch.podcast"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("http"), QStringLiteral("https")}; }
    QStringList supportedMimeTypes() const
    {
        return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/mp4"), QStringLiteral("audio/ogg")};
    }

public slots:
    void Raise();
    void Quit();

private:
    PlayerService *m_player;
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(QVariantMap Metadata READ metadata)

public:
    explicit MprisPlayerAdaptor(QObject *parent, PlayerService *player)
        : QDBusAbstractAdaptor(parent)
        , m_player(player)
    {
    }

    QString playbackStatus() const;
    QString loopStatus() const { return QStringLiteral("None"); }
    double rate() const;
    void setRate(double value);
    double minimumRate() const { return 0.5; }
    double maximumRate() const { return 3.0; }
    double volume() const;
    void setVolume(double value);
    qlonglong position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canSeek() const { return true; }
    bool canControl() const { return true; }
    QVariantMap metadata() const;

    void notifySeeked(qlonglong positionUs) { emit Seeked(positionUs); }
    void notifyChanged();

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &track, qlonglong positionUs);
    void OpenUri(const QString &uri);

signals:
    void Seeked(qlonglong Position);

private:
    PlayerService *m_player;
};

class PlayerService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.github.allanjorch.podcast.Player")

public:
    explicit PlayerService(Library &library, QObject *parent = nullptr);

    QString playbackStatus() const;
    double rate() const { return m_rate; }
    double volume() const { return m_volume; }
    qint64 positionMs() const { return m_positionMs; }
    bool canGoNext() const { return m_library.adjacent(m_episode.id, true, false) != 0; }
    bool canGoPrevious() const { return m_library.adjacent(m_episode.id, false, false) != 0; }
    QVariantMap metadata() const;
    void setMpris(MprisPlayerAdaptor *mpris) { m_mpris = mpris; }
    void skip(bool older);
    void openUri(const QString &uri);

public slots:
    Q_SCRIPTABLE void Load(qlonglong episodeId);
    Q_SCRIPTABLE void LoadPaused(qlonglong episodeId);
    Q_SCRIPTABLE void Play();
    Q_SCRIPTABLE void Pause();
    Q_SCRIPTABLE void PlayPause();
    Q_SCRIPTABLE void Stop();
    Q_SCRIPTABLE void SeekTo(double seconds);
    Q_SCRIPTABLE void SetRate(double value);
    Q_SCRIPTABLE void SetVolume(double value);
    Q_SCRIPTABLE void RefreshArt();
    Q_SCRIPTABLE QVariantMap State();

signals:
    Q_SCRIPTABLE void StateChanged(QVariantMap state);

private:
    void loadEpisode(qlonglong episodeId, bool autoPlay);
    void savePosition();
    void publish();
    void noteSeek(qint64 positionMs);
    void tryResumeSeek(bool forceReposition = false);
    void armResumeWatch();
    void clearResumeWatch();
    void rebuildAudioOutput();
    void recoverAfterSleep(bool resumePlay);
    void armStallWatch();
    void checkStall();

public:
    void considerExit();

public slots:
    void handlePrepareForSleep(bool sleeping);

private:

    Library &m_library;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    MprisPlayerAdaptor *m_mpris = nullptr;
    EpisodeRow m_episode;
    QString m_showTitle;
    QString m_art;
    QString m_description;
    QString m_error;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    int m_resumeMs = 0;
    int m_resumeGuardMs = 0; // last intended resume; re-arm if a "landed" seek bounces to 0
    bool m_resumePending = false;
    QElapsedTimer m_resumeSince;
    QElapsedTimer m_resumeConfirmSince;
    QElapsedTimer m_resumeGuardingSince;
    QTimer *m_resumeTimer = nullptr;
    int m_resumeForceTick = 0;
    bool m_inResumeSeek = false;
    bool m_marked = false;
    double m_rate = 1;
    double m_volume = 1;
    qint64 m_lastSavedMs = 0;
    bool m_playAfterSleep = false;
    bool m_recovering = false;
    int m_errorRecoveries = 0;
    qint64 m_stallAnchorMs = -1;
    QElapsedTimer m_stallSince;
    QTimer *m_stallTimer = nullptr;
};

void MprisRootAdaptor::Raise()
{
    if (uiIsOpen()) {
        QDBusInterface ui(kUiService, QStringLiteral("/com/github/allanjorch/podcast/Ui"),
                          QStringLiteral("com.github.allanjorch.podcast.Ui"),
                          QDBusConnection::sessionBus());
        ui.call(QStringLiteral("Raise"));
        return;
    }
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
}

void MprisRootAdaptor::Quit()
{
    m_player->Stop();
    QCoreApplication::quit();
}

QString MprisPlayerAdaptor::playbackStatus() const { return m_player->playbackStatus(); }
double MprisPlayerAdaptor::rate() const { return m_player->rate(); }
void MprisPlayerAdaptor::setRate(double value) { m_player->SetRate(value); }
double MprisPlayerAdaptor::volume() const { return m_player->volume(); }
void MprisPlayerAdaptor::setVolume(double value) { m_player->SetVolume(value); }
qlonglong MprisPlayerAdaptor::position() const { return m_player->positionMs() * 1000; }
bool MprisPlayerAdaptor::canGoNext() const { return m_player->canGoNext(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_player->canGoPrevious(); }
QVariantMap MprisPlayerAdaptor::metadata() const { return m_player->metadata(); }

void MprisPlayerAdaptor::notifyChanged()
{
    QVariantMap changed;
    changed.insert(QStringLiteral("PlaybackStatus"), playbackStatus());
    changed.insert(QStringLiteral("Metadata"), metadata());
    changed.insert(QStringLiteral("Rate"), rate());
    changed.insert(QStringLiteral("Volume"), volume());
    changed.insert(QStringLiteral("CanGoNext"), canGoNext());
    changed.insert(QStringLiteral("CanGoPrevious"), canGoPrevious());
    QDBusMessage message = QDBusMessage::createSignal(
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    message << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    QDBusConnection::sessionBus().send(message);
}

void PlayerService::skip(bool older)
{
    if (m_episode.id == 0)
        return;
    const qint64 id = m_library.adjacent(m_episode.id, older, false);
    if (id != 0)
        Load(id);
}

void PlayerService::openUri(const QString &uri)
{
    const qint64 id = m_library.episodeByAudioUrl(uri);
    if (id != 0)
        Load(id);
}

PlayerService::PlayerService(Library &library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_rate = m_library.rate();
    m_volume = m_library.volume();
    m_audio->setVolume(static_cast<float>(m_volume));
    {
        // Long podcast HTTP streams (e.g. Megaphone) need a generous read timeout;
        // default is short enough that a brief stall becomes "Demuxing failed".
        QPlaybackOptions opts = m_player->playbackOptions();
        opts.setNetworkTimeout(std::chrono::milliseconds(60000));
        opts.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::Playback);
        m_player->setPlaybackOptions(opts);
    }

    // After suspend/resume, Qt Multimedia + PipeWire often leave a "Playing"
    // stream that is corked and never advances. Rebuild on wake; watch for stalls.
    QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("PrepareForSleep"),
        this,
        SLOT(handlePrepareForSleep(bool)));
    m_stallTimer = new QTimer(this);
    m_stallTimer->setInterval(1000);
    connect(m_stallTimer, &QTimer::timeout, this, [this]() { checkStall(); });
    m_resumeTimer = new QTimer(this);
    m_resumeTimer->setInterval(500);
    connect(m_resumeTimer, &QTimer::timeout, this, [this]() {
        if (!m_resumePending || m_resumeMs <= 0) {
            clearResumeWatch();
            return;
        }
        tryResumeSeek(false);
        if (m_resumeSince.isValid() && m_resumeSince.elapsed() >= 20000) {
            m_library.setPosition(m_episode.id, m_resumeMs);
            m_lastSavedMs = m_resumeMs;
            m_positionMs = m_resumeMs;
            clearResumeWatch();
            publish();
        }
    });

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        // While a resume seek is pending, Qt often reports ~0 (or a one-shot optimistic
        // target) before the seek sticks. Ignore low readings, and only clear pending
        // after the position stays near the target — a single near-target pulse is not enough.
        const qint64 slop = 2500;
        if (m_resumePending && m_resumeMs > 0) {
            if (qAbs(position - m_resumeMs) <= slop) {
                if (!m_resumeConfirmSince.isValid())
                    m_resumeConfirmSince.restart();
                else if (m_resumeConfirmSince.elapsed() >= 1000) {
                    // Require a full second near the target; then keep a bounce guard.
                    m_resumeGuardMs = m_resumeMs;
                    m_resumeGuardingSince.restart();
                    clearResumeWatch();
                }
                m_positionMs = position;
            } else {
                m_resumeConfirmSince.invalidate();
                if (position + slop < m_resumeMs) {
                    m_positionMs = m_resumeMs;
                    if (!m_resumeSince.isValid() || m_resumeSince.elapsed() < 30000)
                        return;
                    m_library.setPosition(m_episode.id, m_resumeMs);
                    m_lastSavedMs = m_resumeMs;
                    m_resumeGuardMs = m_resumeMs;
                    m_resumeGuardingSince.restart();
                    clearResumeWatch();
                } else {
                    m_resumeGuardMs = 0;
                    m_resumeGuardingSince.invalidate();
                    clearResumeWatch();
                    m_positionMs = position;
                }
            }
            if (m_resumePending) {
                if (qAbs(m_positionMs - m_lastSavedMs) >= 5000)
                    savePosition();
                return;
            }
        } else if (m_resumeGuardMs > 5000
                   && m_resumeGuardingSince.isValid()
                   && m_resumeGuardingSince.elapsed() < 8000
                   && position + 10000 < m_resumeGuardMs
                   && position < 60000) {
            // Seek looked landed then bounced back near the start — try again.
            m_resumeMs = m_resumeGuardMs;
            m_resumePending = true;
            m_positionMs = m_resumeGuardMs;
            m_lastSavedMs = m_resumeGuardMs;
            armResumeWatch();
            return;
        } else {
            if (m_resumeGuardingSince.isValid() && m_resumeGuardingSince.elapsed() >= 8000) {
                m_resumeGuardMs = 0;
                m_resumeGuardingSince.invalidate();
            }
            m_positionMs = position;
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState
            && !m_resumePending && !m_recovering) {
            m_stallAnchorMs = position;
            m_stallSince.restart();
            if (!m_error.isEmpty()) {
                m_error.clear();
                m_errorRecoveries = 0;
                publish();
            }
        }
        if (!m_marked && m_episode.id != 0 && m_durationMs >= 15000
            && position >= m_durationMs * 95 / 100) {
            m_library.markPlayed(m_episode.id, true);
            m_episode.played = true;
            m_marked = true;
            publish();
        }
        if (qAbs(position - m_lastSavedMs) >= 5000)
            savePosition();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (duration > 0)
            m_durationMs = duration;
        tryResumeSeek(false);
        m_player->setPlaybackRate(m_rate);
        publish();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this]() {
        armStallWatch();
        publish();
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::BufferedMedia
            || status == QMediaPlayer::LoadedMedia) {
            tryResumeSeek(false);
        }
        if (status != QMediaPlayer::EndOfMedia || m_episode.id == 0)
            return;
        m_library.markPlayed(m_episode.id, true);
        m_library.setPosition(m_episode.id, static_cast<int>(m_durationMs));
        m_episode.played = true;
        const qint64 next = m_library.adjacent(m_episode.id, true, true);
        QTimer::singleShot(0, this, [this, next]() {
            if (next != 0)
                Load(next);
            else
                Stop();
        });
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &message) {
        if (m_episode.id == 0)
            return;
        const bool recoverable = isRecoverablePlaybackError(error, message);
        if (recoverable && m_errorRecoveries < kMaxErrorRecoveries) {
            ++m_errorRecoveries;
            m_error = friendlyPlaybackError(error, message, false);
            publish();
            // Back off so CDN/session and PipeWire can settle (sleep/network blips).
            const int delayMs = 600 + (m_errorRecoveries - 1) * 1200;
            const int attempt = m_errorRecoveries;
            QTimer::singleShot(delayMs, this, [this, attempt]() {
                if (m_episode.id == 0 || attempt != m_errorRecoveries)
                    return;
                recoverAfterSleep(true);
            });
            return;
        }
        m_error = friendlyPlaybackError(error, message, true);
        publish();
        if (!uiIsOpen())
            QTimer::singleShot(1200, this, [this]() { Stop(); });
    });
}

QString PlayerService::playbackStatus() const
{
    if (m_episode.id == 0)
        return QStringLiteral("Stopped");
    switch (m_player->playbackState()) {
    case QMediaPlayer::PlayingState:
        return QStringLiteral("Playing");
    case QMediaPlayer::PausedState:
        return QStringLiteral("Paused");
    default:
        return QStringLiteral("Stopped");
    }
}

QVariantMap PlayerService::metadata() const
{
    QVariantMap meta;
    if (m_episode.id == 0)
        return meta;
    meta.insert(QStringLiteral("mpris:trackid"),
                QVariant::fromValue(QDBusObjectPath(
                    QStringLiteral("/com/github/allanjorch/podcast/episode/%1").arg(m_episode.id))));
    if (m_durationMs > 0)
        meta.insert(QStringLiteral("mpris:length"), QVariant::fromValue<qlonglong>(m_durationMs * 1000));
    if (!m_art.isEmpty())
        meta.insert(QStringLiteral("mpris:artUrl"), m_art);
    meta.insert(QStringLiteral("xesam:title"), m_episode.title);
    meta.insert(QStringLiteral("xesam:artist"), QStringList{m_showTitle});
    meta.insert(QStringLiteral("xesam:url"), m_episode.audioUrl);
    return meta;
}

QVariantMap PlayerService::State()
{
    QVariantMap state;
    QString status = QStringLiteral("stopped");
    if (m_episode.id != 0) {
        // Loaded episode is either playing or paused (never "stopped") so the
        // PlayerBar stays visible with a ready-to-play affordance after restore.
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            status = QStringLiteral("playing");
        else
            status = QStringLiteral("paused");
    }
    state.insert(QStringLiteral("episodeId"), m_episode.id);
    state.insert(QStringLiteral("showId"), m_episode.showId);
    state.insert(QStringLiteral("title"), m_episode.title);
    state.insert(QStringLiteral("showTitle"), m_showTitle);
    state.insert(QStringLiteral("art"), m_art);
    state.insert(QStringLiteral("position"), m_positionMs / 1000.0);
    state.insert(QStringLiteral("duration"), m_durationMs / 1000.0);
    state.insert(QStringLiteral("rate"), m_rate);
    state.insert(QStringLiteral("volume"), m_volume);
    state.insert(QStringLiteral("status"), status);
    state.insert(QStringLiteral("played"), m_episode.played);
    state.insert(QStringLiteral("error"), m_error);
    state.insert(QStringLiteral("description"), m_description);
    return state;
}


QString artForEpisode(Library &library, const EpisodeRow &episode)
{
    // Prefer episode art (local cache, then remote URL) over the show cover.
    if (!episode.imagePath.isEmpty() && QFileInfo::exists(episode.imagePath))
        return QUrl::fromLocalFile(episode.imagePath).toString();
    if (!episode.imageUrl.isEmpty())
        return episode.imageUrl;
    const QString showImage = library.showImage(episode.showId);
    if (showImage.isEmpty() || !QFileInfo::exists(showImage))
        return {};
    return QUrl::fromLocalFile(showImage).toString();
}

void PlayerService::Load(qlonglong episodeId)
{
    loadEpisode(episodeId, true);
}

void PlayerService::LoadPaused(qlonglong episodeId)
{
    loadEpisode(episodeId, false);
}

void PlayerService::loadEpisode(qlonglong episodeId, bool autoPlay)
{
    if (m_episode.id == episodeId && episodeId != 0
        && m_player->playbackState() != QMediaPlayer::StoppedState) {
        // Same episode already loaded: if we are still near the start but the
        // library has saved progress, resume there instead of playing from 0.
        const EpisodeRow saved = m_library.episode(episodeId);
        int resume = saved.positionMs;
        const int knownDuration = saved.durationSecs * 1000;
        if (knownDuration > 0 && resume >= knownDuration - 2000)
            resume = 0;
        if (saved.played && knownDuration > 0 && resume > knownDuration * 9 / 10)
            resume = 0;
        if (resume > 5000 && m_player->position() + 2500 < resume) {
            m_resumeMs = resume;
            m_resumeGuardMs = resume;
            m_resumePending = true;
            m_positionMs = resume;
            m_lastSavedMs = resume;
            armResumeWatch();
            // Same episode is already buffered — SeekTo-style setPosition works here.
            tryResumeSeek(false);
        }
        m_library.setLastPlayedEpisodeId(episodeId);
        if (autoPlay)
            m_player->play();
        else
            m_player->pause();
        publish();
        return;
    }
    const EpisodeRow episode = m_library.episode(episodeId);
    if (episode.id == 0)
        return;
    savePosition();
    m_error.clear();
    m_errorRecoveries = 0;
    m_episode = episode;
    m_showTitle = m_library.showTitle(episode.showId);
    m_description = episode.description;
    m_art = artForEpisode(m_library, episode);
    m_marked = episode.played;
    int resume = episode.positionMs;
    const int knownDuration = episode.durationSecs * 1000;
    if (knownDuration > 0 && resume >= knownDuration - 2000)
        resume = 0;
    if (episode.played && knownDuration > 0 && resume > knownDuration * 9 / 10)
        resume = 0;
    m_resumeMs = resume;
    m_resumeGuardMs = resume > 0 ? resume : 0;
    m_resumePending = resume > 0;
    m_positionMs = resume;
    m_durationMs = knownDuration;
    m_lastSavedMs = resume;
    m_rate = m_library.rate();
    m_player->setPlaybackRate(m_rate);
    m_player->setSource(QUrl(episode.audioUrl));
    if (m_resumePending)
        armResumeWatch();
    m_library.setLastPlayedEpisodeId(episode.id);
    if (autoPlay)
        m_player->play();
    else
        m_player->pause();
    publish();
}

void PlayerService::Play()
{
    if (m_episode.id == 0)
        return;
    // Dead demux/network pipeline (raw "Demuxing failed" left in the bar) needs a reload.
    if (!m_error.isEmpty()
        || m_player->mediaStatus() == QMediaPlayer::InvalidMedia
        || m_player->error() != QMediaPlayer::NoError) {
        m_error.clear();
        recoverAfterSleep(true);
        return;
    }
    // A corked post-sleep pipeline reports Playing but never advances; rebuild first.
    if (m_player->playbackState() == QMediaPlayer::PlayingState
        && m_stallSince.isValid() && m_stallSince.elapsed() >= 1500
        && !m_resumePending) {
        recoverAfterSleep(true);
        return;
    }
    m_player->play();
    armStallWatch();
    publish();
}

void PlayerService::Pause()
{
    if (m_episode.id == 0)
        return;
    m_player->pause();
    savePosition();
    publish();
}

void PlayerService::PlayPause()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        Pause();
    else
        Play();
}

void PlayerService::Stop()
{
    savePosition();
    m_library.setLastPlayedEpisodeId(0);
    // Clear episode before stop() so a synchronous positionChanged(0) cannot
    // autosave over the library row we just wrote.
    m_episode = {};
    m_showTitle.clear();
    m_art.clear();
    m_description.clear();
    m_positionMs = 0;
    m_durationMs = 0;
    m_resumeMs = 0;
    m_resumeGuardMs = 0;
    m_resumeGuardingSince.invalidate();
    clearResumeWatch();
    m_player->stop();
    publish();
    considerExit();
}

void PlayerService::SeekTo(double seconds)
{
    if (m_episode.id == 0)
        return;
    const qint64 ms = qMax<qint64>(0, static_cast<qint64>(seconds * 1000));
    m_resumeMs = 0;
    m_resumeGuardMs = 0;
    m_resumeGuardingSince.invalidate();
    clearResumeWatch();
    m_player->setPosition(ms);
    m_positionMs = ms;
    savePosition();
    noteSeek(ms);
    publish();
}

void PlayerService::SetRate(double value)
{
    if (value < 0.5 || value > 3.0)
        return;
    m_rate = value;
    m_library.setRate(value);
    m_player->setPlaybackRate(value);
    publish();
}

void PlayerService::SetVolume(double value)
{
    // QAudioOutput clamps to [0, 1]; soft gain above 100% is not available.
    const double clamped = qBound(0.0, value, 1.0);
    if (qAbs(clamped - m_volume) < 0.0005 && qAbs(clamped - m_audio->volume()) < 0.0005)
        return;
    m_volume = clamped;
    m_library.setVolume(clamped);
    m_audio->setVolume(static_cast<float>(clamped));
    publish();
}

void PlayerService::RefreshArt()
{
    if (m_episode.id == 0)
        return;
    const EpisodeRow episode = m_library.episode(m_episode.id);
    if (episode.id == 0)
        return;
    m_episode.imageUrl = episode.imageUrl;
    m_episode.imagePath = episode.imagePath;
    const QString next = artForEpisode(m_library, episode);
    if (next == m_art)
        return;
    m_art = next;
    publish();
}


void PlayerService::rebuildAudioOutput()
{
    auto *next = new QAudioOutput(this);
    next->setVolume(static_cast<float>(m_volume));
    m_player->setAudioOutput(next);
    if (m_audio)
        m_audio->deleteLater();
    m_audio = next;
}

void PlayerService::recoverAfterSleep(bool resumePlay)
{
    if (m_episode.id == 0 || m_recovering)
        return;
    m_recovering = true;
    const qint64 episodeId = m_episode.id;
    const qint64 keepMs = m_positionMs > 0 ? m_positionMs
        : (m_resumeMs > 0 ? static_cast<qint64>(m_resumeMs) : 0);
    const QUrl url(m_episode.audioUrl);
    savePosition();
    // Keep a user-facing reconnect note only when we already had a stream error.
    if (!m_error.isEmpty())
        m_error = QStringLiteral("Stream interrupted. Reconnecting…");
    m_player->stop();
    rebuildAudioOutput();
    m_resumeMs = static_cast<int>(keepMs);
    m_resumePending = keepMs > 0;
    m_positionMs = keepMs;
    m_lastSavedMs = keepMs;
    m_player->setSource(QUrl());
    m_player->setSource(url);
    m_player->setPlaybackRate(m_rate);
    if (m_resumePending)
        armResumeWatch();
    if (resumePlay)
        m_player->play();
    else
        m_player->pause();
    tryResumeSeek(true);
    armStallWatch();
    publish();
    // Allow another recovery if this one did not unstick.
    QTimer::singleShot(4000, this, [this, episodeId]() {
        if (m_episode.id == episodeId)
            m_recovering = false;
    });
}

void PlayerService::handlePrepareForSleep(bool sleeping)
{
    if (sleeping) {
        if (m_episode.id == 0)
            return;
        m_playAfterSleep = m_player->playbackState() == QMediaPlayer::PlayingState;
        if (m_playAfterSleep)
            m_player->pause();
        savePosition();
        publish();
        return;
    }
    // Waking: PipeWire/Qt streams are often dead even if we paused cleanly.
    if (m_episode.id == 0)
        return;
    const bool resume = m_playAfterSleep;
    m_playAfterSleep = false;
    // Defer slightly so audio devices finish coming back.
    QTimer::singleShot(800, this, [this, resume]() {
        recoverAfterSleep(resume);
    });
}

void PlayerService::armStallWatch()
{
    if (!m_stallTimer)
        return;
    if (m_episode.id != 0
        && m_player->playbackState() == QMediaPlayer::PlayingState
        && !m_resumePending) {
        m_stallAnchorMs = m_positionMs;
        m_stallSince.restart();
        if (!m_stallTimer->isActive())
            m_stallTimer->start();
    } else {
        m_stallTimer->stop();
        m_stallAnchorMs = -1;
        m_stallSince.invalidate();
    }
}

void PlayerService::checkStall()
{
    if (m_recovering || m_resumePending || m_episode.id == 0)
        return;
    if (m_player->playbackState() != QMediaPlayer::PlayingState) {
        armStallWatch();
        return;
    }
    if (!m_stallSince.isValid()) {
        m_stallAnchorMs = m_positionMs;
        m_stallSince.restart();
        return;
    }
    // Position frozen while Claiming Playing for a few seconds → rebuild.
    if (m_stallSince.elapsed() >= 2500
        && qAbs(m_positionMs - m_stallAnchorMs) < 400) {
        recoverAfterSleep(true);
    }
}

void PlayerService::armResumeWatch()
{
    m_resumeConfirmSince.invalidate();
    m_resumeForceTick = 0;
    m_resumeSince.restart();
    if (m_resumeTimer && !m_resumeTimer->isActive())
        m_resumeTimer->start();
}

void PlayerService::clearResumeWatch()
{
    m_resumePending = false;
    m_resumeConfirmSince.invalidate();
    if (m_resumeTimer)
        m_resumeTimer->stop();
}

void PlayerService::tryResumeSeek(bool forceReposition)
{
    Q_UNUSED(forceReposition);
    if (m_inResumeSeek || !m_resumePending || m_resumeMs <= 0 || m_episode.id == 0)
        return;
    const auto status = m_player->mediaStatus();
    const qint64 duration = m_player->duration();
    // Avoid seeking during LoadingMedia with no duration (aborts some HTTP MP3s).
    // Once duration is known or the pipeline reports Loaded/Buffered, SeekTo-style
    // setPosition works.
    if (duration <= 0
        && status != QMediaPlayer::BufferedMedia
        && status != QMediaPlayer::LoadedMedia)
        return;
    const qint64 target = duration > 0 ? qMin<qint64>(m_resumeMs, duration) : m_resumeMs;
    // setPosition can synchronously re-enter mediaStatusChanged; guard against that
    // recursion (it previously stack-overflowed through publish → sqlite).
    m_inResumeSeek = true;
    m_player->setPosition(target);
    m_player->setPlaybackRate(m_rate);
    m_positionMs = target;
    noteSeek(target);
    m_inResumeSeek = false;
}

void PlayerService::savePosition()
{
    if (m_episode.id == 0)
        return;
    // Never replace a larger saved resume target with early playback from 0.
    // Only SeekTo (user scrub) clears m_resumeMs to allow saving a lower position.
    if (m_resumeMs > 0 && m_positionMs + 2500 < m_resumeMs)
        return;
    if (m_resumeGuardMs > 0 && m_positionMs + 2500 < m_resumeGuardMs
        && m_resumeGuardingSince.isValid() && m_resumeGuardingSince.elapsed() < 8000)
        return;
    m_library.setPosition(m_episode.id, static_cast<int>(m_positionMs));
    m_lastSavedMs = m_positionMs;
}

void PlayerService::publish()
{
    emit StateChanged(State());
    if (m_mpris)
        m_mpris->notifyChanged();
}

void PlayerService::noteSeek(qint64 positionMs)
{
    if (m_mpris)
        m_mpris->notifySeeked(positionMs * 1000);
}

void PlayerService::considerExit()
{
    if (m_episode.id != 0 || uiIsOpen())
        return;
    QTimer::singleShot(300, qApp, &QCoreApplication::quit);
}

void MprisPlayerAdaptor::Next()
{
    m_player->skip(true);
}

void MprisPlayerAdaptor::Previous()
{
    m_player->skip(false);
}
void MprisPlayerAdaptor::Pause() { m_player->Pause(); }
void MprisPlayerAdaptor::PlayPause() { m_player->PlayPause(); }
void MprisPlayerAdaptor::Stop() { m_player->Stop(); }
void MprisPlayerAdaptor::Play() { m_player->Play(); }
void MprisPlayerAdaptor::Seek(qlonglong offsetUs)
{
    m_player->SeekTo((m_player->positionMs() + offsetUs / 1000) / 1000.0);
}
void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &, qlonglong positionUs)
{
    m_player->SeekTo(positionUs / 1000000.0);
}
void MprisPlayerAdaptor::OpenUri(const QString &uri)
{
    m_player->openUri(uri);
}

#include "player.moc"

int runPlayer(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("allanjorch"));
    app.setApplicationName(QStringLiteral("podcast"));
    app.setApplicationDisplayName(QStringLiteral("Podcasts"));
    app.setQuitOnLastWindowClosed(false);

    Library library;
    if (!library.isOpen())
        return 1;

    PlayerService service(library);
    auto *object = new QObject(&service);
    new MprisRootAdaptor(object, &service);
    auto *playerAdaptor = new MprisPlayerAdaptor(object, &service);
    service.setMpris(playerAdaptor);

    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), object,
                       QDBusConnection::ExportAdaptors);
    bus.registerObject(QStringLiteral("/com/github/allanjorch/podcast"), &service,
                       QDBusConnection::ExportScriptableSlots
                           | QDBusConnection::ExportScriptableSignals);
    if (!bus.registerService(kPlayerService))
        return 0;
    if (!bus.registerService(kMprisService))
        return 0;
    QTimer::singleShot(400, &service, &PlayerService::considerExit);
    return app.exec();
}
