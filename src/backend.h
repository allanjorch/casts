#pragma once

#include "library.h"
#include "models.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class QDBusInterface;
class QDBusServiceWatcher;
class QNetworkReply;
class QTimer;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(ShowModel *shows READ shows CONSTANT)
    Q_PROPERTY(EpisodeModel *episodes READ episodes CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastRefreshLabel READ lastRefreshLabel NOTIFY lastRefreshChanged)
    Q_PROPERTY(qint64 openShowId READ openShowId NOTIFY openShowChanged)
    Q_PROPERTY(QString openShowTitle READ openShowTitle NOTIFY openShowChanged)
    Q_PROPERTY(QString openShowCover READ openShowCover NOTIFY openShowChanged)
    Q_PROPERTY(int openShowUnheard READ openShowUnheard NOTIFY openShowChanged)
    Q_PROPERTY(qint64 openEpisodeId READ openEpisodeId NOTIFY openEpisodeChanged)
    Q_PROPERTY(QString openEpisodeTitle READ openEpisodeTitle NOTIFY openEpisodeChanged)
    Q_PROPERTY(QString openEpisodeDescription READ openEpisodeDescription NOTIFY openEpisodeChanged)
    Q_PROPERTY(QString openEpisodeCover READ openEpisodeCover NOTIFY openEpisodeChanged)
    Q_PROPERTY(qint64 openEpisodePublished READ openEpisodePublished NOTIFY openEpisodeChanged)
    Q_PROPERTY(int openEpisodeDuration READ openEpisodeDuration NOTIFY openEpisodeChanged)
    Q_PROPERTY(bool openEpisodePlayed READ openEpisodePlayed NOTIFY openEpisodeChanged)
    Q_PROPERTY(int openEpisodePositionMs READ openEpisodePositionMs NOTIFY openEpisodeChanged)

    Q_PROPERTY(qint64 playerEpisodeId READ playerEpisodeId NOTIFY playerStateChanged)
    Q_PROPERTY(qint64 playerShowId READ playerShowId NOTIFY playerStateChanged)
    Q_PROPERTY(QString playerTitle READ playerTitle NOTIFY playerStateChanged)
    Q_PROPERTY(QString playerShowTitle READ playerShowTitle NOTIFY playerStateChanged)
    Q_PROPERTY(QString playerArt READ playerArt NOTIFY playerStateChanged)
    Q_PROPERTY(QString playerStatus READ playerStatus NOTIFY playerStateChanged)
    Q_PROPERTY(QString playerError READ playerError NOTIFY playerStateChanged)
    Q_PROPERTY(bool playerPlayed READ playerPlayed NOTIFY playerStateChanged)
    Q_PROPERTY(double playerRate READ playerRate NOTIFY playerStateChanged)
    Q_PROPERTY(double playerVolume READ playerVolume NOTIFY playerStateChanged)
    Q_PROPERTY(QString playerDescription READ playerDescription NOTIFY playerStateChanged)
    Q_PROPERTY(double playerPosition READ playerPosition NOTIFY playerPositionChanged)
    Q_PROPERTY(double playerDuration READ playerDuration NOTIFY playerStateChanged)
    Q_PROPERTY(QString shelfView READ shelfView NOTIFY shelfLayoutChanged)
    Q_PROPERTY(int shelfColumns READ shelfColumns NOTIFY shelfLayoutChanged)
    Q_PROPERTY(int shelfListSize READ shelfListSize NOTIFY shelfLayoutChanged)
    Q_PROPERTY(int episodeListSize READ episodeListSize NOTIFY shelfLayoutChanged)
    Q_PROPERTY(bool shelfShowAll READ shelfShowAll NOTIFY filterChanged)
    Q_PROPERTY(bool episodeShowAll READ episodeShowAll NOTIFY filterChanged)

public:
    explicit Backend(Library &library, QObject *parent = nullptr);

    ShowModel *shows() { return &m_shows; }
    EpisodeModel *episodes() { return &m_episodes; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    QString lastRefreshLabel() const;
    qint64 openShowId() const { return m_openShowId; }
    QString openShowTitle() const { return m_openShowTitle; }
    QString openShowCover() const { return m_openShowCover; }
    int openShowUnheard() const { return m_openShowUnheard; }
    qint64 openEpisodeId() const { return m_openEpisodeId; }
    QString openEpisodeTitle() const { return m_openEpisodeTitle; }
    QString openEpisodeDescription() const { return m_openEpisodeDescription; }
    QString openEpisodeCover() const { return m_openEpisodeCover; }
    qint64 openEpisodePublished() const { return m_openEpisodePublished; }
    int openEpisodeDuration() const { return m_openEpisodeDuration; }
    bool openEpisodePlayed() const { return m_openEpisodePlayed; }
    int openEpisodePositionMs() const { return m_openEpisodePositionMs; }

    qint64 playerEpisodeId() const { return m_playerEpisodeId; }
    qint64 playerShowId() const { return m_playerShowId; }
    QString playerTitle() const { return m_playerTitle; }
    QString playerShowTitle() const { return m_playerShowTitle; }
    QString playerArt() const { return m_playerArt; }
    QString playerStatus() const { return m_playerStatus; }
    QString playerError() const { return m_playerError; }
    bool playerPlayed() const { return m_playerPlayed; }
    double playerRate() const { return m_playerRate; }
    double playerVolume() const { return m_playerVolume; }
    QString playerDescription() const { return m_playerDescription; }
    double playerPosition() const { return m_playerPosition; }
    double playerDuration() const { return m_playerDuration; }
    QString shelfView() const;
    int shelfColumns() const;
    int shelfListSize() const;
    int episodeListSize() const;
    bool shelfShowAll() const;
    bool episodeShowAll() const;

    Q_INVOKABLE void addFeed(const QString &url);
    Q_INVOKABLE void importOpml(const QString &fileUrl);
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void refreshOpenShow();
    Q_INVOKABLE void openShow(qint64 showId);
    Q_INVOKABLE void closeShow();
    Q_INVOKABLE void openEpisode(qint64 episodeId);
    Q_INVOKABLE void openPlayingEpisode();
    Q_INVOKABLE void closeEpisode();
    Q_INVOKABLE void removeOpenShow();

    Q_INVOKABLE void playEpisode(qint64 episodeId);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void cycleRate();
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void setShelfView(const QString &view);
    Q_INVOKABLE void setShelfColumns(int columns);
    Q_INVOKABLE void setShelfListSize(int size);
    Q_INVOKABLE void setEpisodeListSize(int size);
    Q_INVOKABLE void setShelfShowAll(bool showAll);
    Q_INVOKABLE void setEpisodeShowAll(bool showAll);
    Q_INVOKABLE void markPlayed(qint64 episodeId, bool played);
    Q_INVOKABLE void markAllPlayed(qint64 showId);
    Q_INVOKABLE void markOlderPlayed(qint64 episodeId);
    Q_INVOKABLE void markNewerPlayed(qint64 episodeId);

    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    void raiseWindow();

signals:
    void statusChanged();
    void busyChanged();
    void lastRefreshChanged();
    void openShowChanged();
    void openEpisodeChanged();
    void playerStateChanged();
    void playerPositionChanged();
    void shelfLayoutChanged();
    void filterChanged();
    void raised();

private:
    struct Job {
        QString url;
    };

    void setStatus(const QString &status);
    void setBusy(bool busy);
    void maybeAutoRefreshOnLaunch();
    void noteSuccessfulShelfRefresh();
    void reloadShows();
    void reloadEpisodes();
    QList<ShowRow> visibleShows() const;
    QList<EpisodeRow> visibleEpisodes(qint64 showId) const;
    void enqueue(const QStringList &urls);
    void fetchNext();
    void downloadCover(qint64 showId, const QString &imageUrl);
    void downloadEpisodeCover(qint64 episodeId, const QString &imageUrl);
    void refreshOpenEpisode();
    bool playerReady() const;
    void startPlayerService();
    void onPlayerServiceRegistered(const QString &service);
    void onPlayerStartTimeout();
    void flushPendingPlayerCalls();
    void invokePlayer(const QString &method, const QVariantList &args);
    void pollPlayer();
    void applyPlayerState(const QVariantMap &state);
    void callPlayer(const QString &method, const QVariantList &args = {});
    void restoreLastPlayed();
    QString countMessage(int count, const QString &what) const;

    struct PendingPlayerCall {
        QString method;
        QVariantList args;
    };

    Library &m_library;
    ShowModel m_shows;
    EpisodeModel m_episodes;
    QNetworkAccessManager m_network;
    QList<Job> m_queue;
    QPointer<QNetworkReply> m_active;
    bool m_shelfRefresh = false;
    bool m_shelfRefreshHadError = false;
    QString m_status;
    bool m_busy = false;
    qint64 m_openShowId = 0;
    QString m_openShowTitle;
    QString m_openShowCover;
    int m_openShowUnheard = 0;
    qint64 m_openEpisodeId = 0;
    QString m_openEpisodeTitle;
    QString m_openEpisodeDescription;
    QString m_openEpisodeCover;
    qint64 m_openEpisodePublished = 0;
    int m_openEpisodeDuration = 0;
    bool m_openEpisodePlayed = false;
    int m_openEpisodePositionMs = 0;

    qint64 m_playerEpisodeId = 0;
    qint64 m_playerShowId = 0;
    QString m_playerTitle;
    QString m_playerShowTitle;
    QString m_playerArt;
    QString m_playerStatus = QStringLiteral("stopped");
    QString m_playerError;
    bool m_playerPlayed = false;
    double m_playerRate = 1;
    double m_playerVolume = 1;
    QString m_playerDescription;
    double m_playerPosition = 0;
    double m_playerDuration = 0;
    bool m_sawPlayed = false;
    bool m_returnToShelfOnClose = false;
    QDBusInterface *m_player = nullptr;
    QDBusServiceWatcher *m_playerWatcher = nullptr;
    QTimer *m_playerStartTimer = nullptr;
    bool m_startingPlayer = false;
    QList<PendingPlayerCall> m_pendingPlayerCalls;
    QSet<qint64> m_showCoverDownloads;
    QSet<qint64> m_episodeCoverDownloads;
};
