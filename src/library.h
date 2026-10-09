#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct ShowRow {
    qint64 id = 0;
    QString feedUrl;
    QString title;
    QString author;
    QString imagePath;
    int unheard = 0;
    qint64 newest = 0;
    qint64 youngestUnheard = 0;
};

struct EpisodeRow {
    qint64 id = 0;
    qint64 showId = 0;
    QString guid;
    QString title;
    QString description;
    QString audioUrl;
    QString imageUrl;
    QString imagePath;
    qint64 published = 0;
    int durationSecs = 0;
    bool played = false;
    int positionMs = 0;
    QString showTitle; // filled by queue() only
};

struct ParsedEpisode {
    QString guid;
    QString title;
    QString description;
    QString audioUrl;
    QString imageUrl;
    qint64 published = 0;
    int durationSecs = 0;
};

struct ParsedShow {
    QString title;
    QString description; // channel description (Explore preview)
    QString author;
    QString imageUrl;
    QList<ParsedEpisode> episodes;
};

class Library {
public:
    static QString defaultPath();

    explicit Library(const QString &path = QString());
    ~Library();

    bool isOpen() const { return m_open; }
    QString error() const { return m_error; }
    QString path() const { return m_path; }

    QList<ShowRow> shows() const;
    QList<EpisodeRow> episodes(qint64 showId) const;
    EpisodeRow episode(qint64 id) const;
    QString showTitle(qint64 showId) const;
    bool hasShow(qint64 showId) const;
    QString showImage(qint64 showId) const;
    QString showImageUrl(qint64 showId) const;
    qint64 episodeByAudioUrl(const QString &audioUrl) const;

    qint64 upsertShow(const QString &feedUrl, const ParsedShow &parsed);
    void setShowImage(qint64 showId, const QString &imageUrl, const QString &imagePath);
    void setEpisodeImage(qint64 episodeId, const QString &imageUrl, const QString &imagePath);
    // Delete the cached file and clear image_path. The remote image URL stays,
    // so a later download can run. A normal refresh does not call these.
    void clearShowImagePath(qint64 showId);
    void clearEpisodeImagePath(qint64 episodeId);
    bool removeShow(qint64 showId);

    int markPlayed(qint64 episodeId, bool played);
    int markAllPlayed(qint64 showId);
    int markLibraryPlayed();
    int markOlderPlayed(qint64 episodeId);
    int markNewerPlayed(qint64 episodeId);

    void setPosition(qint64 episodeId, int positionMs);
    qint64 adjacent(qint64 episodeId, bool older, bool unplayedOnly) const;

    double rate() const;
    void setRate(double rate);
    double volume() const;
    void setVolume(double volume);
    // Level to restore when unmuting (0 = none saved).
    // Playback downmix to mono (settings key playback.mono).
    bool mono() const;
    void setMono(bool on);
    double volumeBeforeMute() const;
    void setVolumeBeforeMute(double volume);
    qint64 lastPlayedEpisodeId() const;
    void setLastPlayedEpisodeId(qint64 episodeId);
    QString shelfView() const;
    void setShelfView(const QString &view);
    int shelfColumns() const;
    void setShelfColumns(int columns);
    int shelfListSize() const;
    void setShelfListSize(int size);
    int episodeListSize() const;
    void setEpisodeListSize(int size);
    bool shelfShowAll() const;
    void setShelfShowAll(bool showAll);
    bool episodeShowAll() const;
    double nowPlayingArtScale() const;
    // Explore store country (two-letter code); empty = automatic.
    QString exploreCountry() const;
    void setExploreCountry(const QString &code);
    void setNowPlayingArtScale(double scale);
    void setEpisodeShowAll(bool showAll);
    // Which audio User-Agent generation the media cache was downloaded with.
    QString audioCacheUa() const;
    void setAudioCacheUa(const QString &generation);

    QStringList feedUrls() const;

    // Up-next queue (table `queue`, ordered by position).
    QList<EpisodeRow> queue() const;
    QList<qint64> queueIds() const;
    bool isQueued(qint64 episodeId) const;
    qint64 queueHead() const;
    // What plays after `current`: the queue item after it when it is queued
    // (0 if it is last), otherwise the top of the queue.
    qint64 queueAfter(qint64 current) const;
    // End of episode: pick what follows `finished` by position, then drop it.
    // The only automatic removal; starting or manually marking leaves the queue alone.
    qint64 finishQueued(qint64 finished);
    void addToQueue(qint64 episodeId, bool atTop);
    bool removeFromQueue(qint64 episodeId);
    void setQueueOrder(const QList<qint64> &ids);
    void clearQueue();

private:
    bool migrate();
    QString setting(const QString &key, const QString &fallback) const;
    void setSetting(const QString &key, const QString &value);

    QString m_path;
    QString m_connection;
    QString m_error;
    bool m_open = false;
};
