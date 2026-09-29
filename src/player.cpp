#include "player.h"

#include "library.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QProcess>
#include <QDBusMessage>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

namespace {
const QString kUiService = QStringLiteral("com.github.allanjorch.podcast");
const QString kPlayerService = QStringLiteral("com.github.allanjorch.podcast.Player");
const QString kMprisService = QStringLiteral("org.mpris.MediaPlayer2.podcast");

bool uiIsOpen()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(kUiService);
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
    double volume() const { return m_audio->volume(); }
    void setVolume(double value) { m_audio->setVolume(static_cast<float>(value)); }
    qint64 positionMs() const { return m_positionMs; }
    bool canGoNext() const { return m_library.adjacent(m_episode.id, true, false) != 0; }
    bool canGoPrevious() const { return m_library.adjacent(m_episode.id, false, false) != 0; }
    QVariantMap metadata() const;
    void setMpris(MprisPlayerAdaptor *mpris) { m_mpris = mpris; }
    void skip(bool older);
    void openUri(const QString &uri);

public slots:
    Q_SCRIPTABLE void Load(qlonglong episodeId);
    Q_SCRIPTABLE void Play();
    Q_SCRIPTABLE void Pause();
    Q_SCRIPTABLE void PlayPause();
    Q_SCRIPTABLE void Stop();
    Q_SCRIPTABLE void SeekTo(double seconds);
    Q_SCRIPTABLE void SetRate(double value);
    Q_SCRIPTABLE QVariantMap State();

signals:
    Q_SCRIPTABLE void StateChanged(QVariantMap state);

private:
    void savePosition();
    void publish();
    void noteSeek(qint64 positionMs);

public:
    void considerExit();

private:

    Library &m_library;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    MprisPlayerAdaptor *m_mpris = nullptr;
    EpisodeRow m_episode;
    QString m_showTitle;
    QString m_art;
    QString m_error;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    int m_resumeMs = 0;
    bool m_resumePending = false;
    bool m_marked = false;
    double m_rate = 1;
    qint64 m_lastSavedMs = 0;
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
void MprisPlayerAdaptor::setVolume(double value) { m_player->setVolume(value); }
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

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        m_positionMs = position;
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
        if (m_resumePending && m_resumeMs > 0 && duration > 0) {
            m_player->setPosition(qMin<qint64>(m_resumeMs, duration));
            m_resumePending = false;
            noteSeek(m_player->position());
        }
        m_player->setPlaybackRate(m_rate);
        publish();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this]() { publish(); });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (m_resumePending && m_resumeMs > 0
            && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
            m_player->setPosition(m_resumeMs);
            m_player->setPlaybackRate(m_rate);
            m_resumePending = false;
            noteSeek(m_resumeMs);
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
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        m_error = message.isEmpty() ? QStringLiteral("Playback failed.") : message;
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
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            status = QStringLiteral("playing");
        else if (m_player->playbackState() == QMediaPlayer::PausedState)
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
    state.insert(QStringLiteral("status"), status);
    state.insert(QStringLiteral("played"), m_episode.played);
    state.insert(QStringLiteral("error"), m_error);
    return state;
}

void PlayerService::Load(qlonglong episodeId)
{
    if (m_episode.id == episodeId && episodeId != 0
        && m_player->playbackState() != QMediaPlayer::StoppedState) {
        m_player->play();
        publish();
        return;
    }
    const EpisodeRow episode = m_library.episode(episodeId);
    if (episode.id == 0)
        return;
    savePosition();
    m_error.clear();
    m_episode = episode;
    m_showTitle = m_library.showTitle(episode.showId);
    const QString image = m_library.showImage(episode.showId);
    m_art = image.isEmpty() ? QString() : QUrl::fromLocalFile(image).toString();
    m_marked = episode.played;
    int resume = episode.positionMs;
    const int knownDuration = episode.durationSecs * 1000;
    if (knownDuration > 0 && resume >= knownDuration - 2000)
        resume = 0;
    if (episode.played && knownDuration > 0 && resume > knownDuration * 9 / 10)
        resume = 0;
    m_resumeMs = resume;
    m_resumePending = resume > 0;
    m_positionMs = resume;
    m_durationMs = knownDuration;
    m_lastSavedMs = resume;
    m_rate = m_library.rate();
    m_player->setPlaybackRate(m_rate);
    m_player->setSource(QUrl(episode.audioUrl));
    m_player->play();
    publish();
}

void PlayerService::Play()
{
    if (m_episode.id == 0)
        return;
    m_player->play();
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
    m_player->stop();
    m_episode = {};
    m_showTitle.clear();
    m_art.clear();
    m_positionMs = 0;
    m_durationMs = 0;
    m_resumePending = false;
    publish();
    considerExit();
}

void PlayerService::SeekTo(double seconds)
{
    if (m_episode.id == 0)
        return;
    const qint64 ms = qMax<qint64>(0, static_cast<qint64>(seconds * 1000));
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

void PlayerService::savePosition()
{
    if (m_episode.id == 0)
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
