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
    QString showImage(qint64 showId) const;
    qint64 episodeByAudioUrl(const QString &audioUrl) const;

    qint64 upsertShow(const QString &feedUrl, const ParsedShow &parsed);
    void setShowImage(qint64 showId, const QString &imageUrl, const QString &imagePath);
    void setEpisodeImage(qint64 episodeId, const QString &imageUrl, const QString &imagePath);
    bool removeShow(qint64 showId);

    int markPlayed(qint64 episodeId, bool played);
    int markAllPlayed(qint64 showId);
    int markOlderPlayed(qint64 episodeId);
    int markNewerPlayed(qint64 episodeId);

    void setPosition(qint64 episodeId, int positionMs);
    qint64 adjacent(qint64 episodeId, bool older, bool unplayedOnly) const;

    double rate() const;
    void setRate(double rate);
    double volume() const;
    void setVolume(double volume);
    QString shelfView() const;
    void setShelfView(const QString &view);
    int shelfColumns() const;
    void setShelfColumns(int columns);
    int shelfListSize() const;
    void setShelfListSize(int size);
    int episodeListSize() const;
    void setEpisodeListSize(int size);

    QStringList feedUrls() const;

private:
    bool migrate();
    QString setting(const QString &key, const QString &fallback) const;
    void setSetting(const QString &key, const QString &value);

    QString m_path;
    QString m_connection;
    QString m_error;
    bool m_open = false;
};
