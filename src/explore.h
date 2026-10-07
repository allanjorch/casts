#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include "netaccess.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class Backend;
class Library;
class QNetworkReply;
class QTimer;

struct ExploreResult {
    qint64 id = 0;           // iTunes collectionId
    QString title;
    QString author;
    QString cover;           // remote artwork URL (600px when known)
    QString genre;
    QString feedUrl;         // may be empty until looked up
    QString viewUrl;         // podcasts.apple.com page
    QString summary;         // chart summary, when the source has one
    int trackCount = 0;
    qint64 released = 0;     // unix seconds of the latest episode
};

class ExploreModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        CollectionIdRole = Qt::UserRole + 1,
        TitleRole,
        AuthorRole,
        CoverRole,
        GenreRole,
        TrackCountRole,
        ReleasedRole,
        FeedUrlRole,
        ViewUrlRole,
        SubscribedRole,
        AddingRole,
    };

    explicit ExploreModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QList<ExploreResult> &rows);
    const QList<ExploreResult> &rows() const { return m_rows; }
    ExploreResult row(int i) const { return i >= 0 && i < m_rows.size() ? m_rows.at(i) : ExploreResult{}; }
    int indexOfId(qint64 id) const;
    void setFeedUrl(int i, const QString &feedUrl);
    // Subscribed = feed already in the library; adding = Subscribe pressed, not landed yet.
    void setStates(const QSet<QString> &libraryFeeds, const QSet<qint64> &adding);

private:
    QList<ExploreResult> m_rows;
    QList<bool> m_subscribed;
    QList<bool> m_adding;
};

// Online podcast directory (iTunes Search API + Apple top charts).
class Explore : public QObject {
    Q_OBJECT
    Q_PROPERTY(ExploreModel *results READ results CONSTANT)
    Q_PROPERTY(QString query READ query NOTIFY queryChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString heading READ heading NOTIFY headingChanged)
    Q_PROPERTY(bool previewOpen READ previewOpen NOTIFY previewChanged)
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY previewChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    // Store region: effective code/name, the chosen override ("" = automatic),
    // the detected automatic name, and the curated storefront list.
    Q_PROPERTY(QString countryCode READ countryCode NOTIFY regionChanged)
    Q_PROPERTY(QString countryName READ countryName NOTIFY regionChanged)
    Q_PROPERTY(QString regionOverride READ regionOverride NOTIFY regionChanged)
    Q_PROPERTY(QString autoCountryName READ autoCountryName CONSTANT)
    Q_PROPERTY(QVariantList regions READ regions CONSTANT)

public:
    Explore(Library &library, Backend &backend, QObject *parent = nullptr);

    ExploreModel *results() { return &m_model; }
    QString query() const { return m_query; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QString heading() const { return m_heading; }
    bool previewOpen() const { return m_previewOpen; }
    bool previewLoading() const { return m_previewLoading; }
    QVariantMap preview() const { return m_preview; }
    QString countryCode() const { return m_country; }
    QString countryName() const { return m_countryName; }
    QString regionOverride() const { return m_override; }
    QString autoCountryName() const { return m_autoName; }
    QVariantList regions() const;

    // "" = automatic (time zone). Persists, then reloads the chart / search.
    Q_INVOKABLE void setRegion(const QString &code);
    static QStringList storefronts();

    // Page opened: show the top chart (cached after the first time).
    Q_INVOKABLE void activate();
    // Debounced (~300 ms) search; cached terms apply instantly.
    Q_INVOKABLE void setQuery(const QString &text);
    // Enter in the field: skip the debounce.
    Q_INVOKABLE void searchNow();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void subscribe(int row);
    Q_INVOKABLE void openPreview(int row);
    Q_INVOKABLE void closePreview();
    Q_INVOKABLE void subscribePreview();
    Q_INVOKABLE void copyFeedUrl(int row);
    Q_INVOKABLE void openInBrowser(int row);

    static QString normalizeFeedUrl(const QString &url);
    // Parsers, public for the self-test.
    static QList<ExploreResult> parseSearch(const QByteArray &json);
    static QList<ExploreResult> parseTopChart(const QByteArray &json);

signals:
    void queryChanged();
    void loadingChanged();
    void errorChanged();
    void headingChanged();
    void previewChanged();
    void regionChanged();

private:
    void startRequest();
    void requestTop();
    void requestSearch(const QString &term);
    void lookupFeeds(const QList<qint64> &ids, const QString &cacheKey);
    void lookupOne(qint64 id, std::function<void(const QString &feedUrl)> done);
    void show(const QList<ExploreResult> &rows, const QString &heading);
    void setLoading(bool loading);
    void setError(const QString &error);
    void setHeading(const QString &heading);
    void refreshStates();
    void addFeed(qint64 id, const QString &feedUrl);
    void loadPreviewFeed(const QString &feedUrl, int generation);
    QString searchKey(const QString &term) const;
    QNetworkReply *get(const QString &url);

    Library &m_library;
    Backend &m_backend;
    ExploreModel m_model;
    AppNetworkAccessManager m_network; // HTTP/1.1 only, see netaccess.h
    QTimer *m_debounce = nullptr;
    QPointer<QNetworkReply> m_reply;     // the one in-flight list request
    QPointer<QNetworkReply> m_previewReply;
    int m_generation = 0;
    int m_previewGeneration = 0;
    QString m_country;
    QString m_countryName;
    QString m_autoCountry;
    QString m_autoName;
    QString m_override;
    void applyCountry();
    QString topKey() const { return QStringLiteral("top:") + m_country; }
    QString m_query;
    QString m_shownKey;
    bool m_loading = false;
    QString m_error;
    QString m_heading;
    QHash<QString, QList<ExploreResult>> m_cache;
    QHash<QString, QVariantMap> m_previewCache; // by normalized feed URL
    QSet<qint64> m_adding;
    bool m_previewOpen = false;
    bool m_previewLoading = false;
    qint64 m_previewId = 0;
    QVariantMap m_preview;
};
