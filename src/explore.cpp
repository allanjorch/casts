#include "explore.h"

#include "backend.h"
#include "feed.h"
#include "library.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextDocumentFragment>
#include <QTimeZone>
#include <QTimer>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

namespace {
const char *kUserAgent = "podcast/0.1 (Omarchy)";

qint64 isoSecs(const QString &text)
{
    const QDateTime when = QDateTime::fromString(text, Qt::ISODate);
    return when.isValid() ? when.toSecsSinceEpoch() : 0;
}

// Chart art comes as 170x170; the same CDN path serves 600x600.
QString biggerArt(QString url)
{
    static const QRegularExpression size(QStringLiteral("/\\d+x\\d+bb\\."));
    url.replace(size, QStringLiteral("/600x600bb."));
    return url;
}

QString plainText(const QString &html)
{
    if (!html.contains(QLatin1Char('<')) && !html.contains(QLatin1Char('&')))
        return html.trimmed();
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

QString errorText(QNetworkReply *reply)
{
    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
        return QStringLiteral("You seem to be offline.");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("The podcast directory took too long to answer.");
    default:
        return QStringLiteral("Could not reach the podcast directory (%1).").arg(reply->errorString());
    }
}
}

// ---------------------------------------------------------------- model

ExploreModel::ExploreModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ExploreModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ExploreModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const ExploreResult &r = m_rows.at(index.row());
    switch (role) {
    case CollectionIdRole: return r.id;
    case TitleRole: return r.title;
    case AuthorRole: return r.author;
    case CoverRole: return r.cover;
    case GenreRole: return r.genre;
    case TrackCountRole: return r.trackCount;
    case ReleasedRole: return r.released;
    case FeedUrlRole: return r.feedUrl;
    case ViewUrlRole: return r.viewUrl;
    case SubscribedRole: return m_subscribed.value(index.row());
    case AddingRole: return m_adding.value(index.row());
    default: return {};
    }
}

QHash<int, QByteArray> ExploreModel::roleNames() const
{
    return {
        {CollectionIdRole, "collectionId"}, {TitleRole, "title"}, {AuthorRole, "author"},
        {CoverRole, "cover"}, {GenreRole, "genre"}, {TrackCountRole, "trackCount"},
        {ReleasedRole, "released"}, {FeedUrlRole, "feedUrl"}, {ViewUrlRole, "viewUrl"},
        {SubscribedRole, "subscribed"}, {AddingRole, "adding"},
    };
}

void ExploreModel::setRows(const QList<ExploreResult> &rows)
{
    beginResetModel();
    m_rows = rows;
    m_subscribed = QList<bool>(rows.size(), false);
    m_adding = QList<bool>(rows.size(), false);
    endResetModel();
}

int ExploreModel::indexOfId(qint64 id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id)
            return i;
    }
    return -1;
}

void ExploreModel::setFeedUrl(int i, const QString &feedUrl)
{
    if (i < 0 || i >= m_rows.size() || m_rows.at(i).feedUrl == feedUrl)
        return;
    m_rows[i].feedUrl = feedUrl;
    emit dataChanged(index(i), index(i), {FeedUrlRole});
}

void ExploreModel::setStates(const QSet<QString> &libraryFeeds, const QSet<qint64> &adding)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const bool sub = !m_rows.at(i).feedUrl.isEmpty()
            && libraryFeeds.contains(Explore::normalizeFeedUrl(m_rows.at(i).feedUrl));
        const bool add = !sub && adding.contains(m_rows.at(i).id);
        if (sub == m_subscribed.at(i) && add == m_adding.at(i))
            continue;
        m_subscribed[i] = sub;
        m_adding[i] = add;
        emit dataChanged(index(i), index(i), {SubscribedRole, AddingRole});
    }
}

// ---------------------------------------------------------------- explore

Explore::Explore(Library &library, Backend &backend, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_backend(backend)
{
    // Automatic store country: the system time zone says where the user is
    // (Europe/Copenhagen -> Denmark) even with an en_US locale; then the
    // locale; then the US store. A saved explore.country overrides it.
    QLocale::Territory territory = QTimeZone::systemTimeZone().territory();
    if (territory == QLocale::AnyTerritory)
        territory = QLocale::system().territory();
    m_autoCountry = QLocale::territoryToCode(territory).toLower();
    if (territory == QLocale::AnyTerritory || m_autoCountry.size() != 2) {
        territory = QLocale::UnitedStates;
        m_autoCountry = QStringLiteral("us");
    }
    m_autoName = QLocale::territoryToString(territory);
    m_override = m_library.exploreCountry();
    applyCountry();

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_debounce, &QTimer::timeout, this, &Explore::startRequest);

    // A feed landed (or went): re-check Subscribed marks.
    connect(&m_backend, &Backend::showCountChanged, this, &Explore::refreshStates);
    // Subscribe that never landed (bad feed, offline): stop its spinner.
    connect(&m_backend, &Backend::busyChanged, this, [this]() {
        if (m_backend.busy() || m_adding.isEmpty())
            return;
        QTimer::singleShot(0, this, [this]() {
            if (m_backend.busy())
                return;
            m_adding.clear();
            refreshStates();
        });
    });
}

QString Explore::normalizeFeedUrl(const QString &url)
{
    QString s = url.trimmed().toLower();
    for (const char *prefix : {"https://", "http://", "feed://", "itpc://", "pcast://"}) {
        if (s.startsWith(QLatin1String(prefix))) {
            s = s.mid(int(qstrlen(prefix)));
            break;
        }
    }
    if (s.startsWith(QStringLiteral("www.")))
        s = s.mid(4);
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    return s;
}

QList<ExploreResult> Explore::parseSearch(const QByteArray &json)
{
    QList<ExploreResult> rows;
    const QJsonArray results = QJsonDocument::fromJson(json).object().value(QStringLiteral("results")).toArray();
    for (const auto &value : results) {
        const QJsonObject o = value.toObject();
        if (o.value(QStringLiteral("kind")).toString() != QStringLiteral("podcast")
            && o.value(QStringLiteral("wrapperType")).toString() != QStringLiteral("track"))
            continue;
        ExploreResult r;
        r.id = o.value(QStringLiteral("collectionId")).toVariant().toLongLong();
        r.title = o.value(QStringLiteral("collectionName")).toString();
        r.author = o.value(QStringLiteral("artistName")).toString();
        r.cover = o.value(QStringLiteral("artworkUrl600")).toString();
        if (r.cover.isEmpty())
            r.cover = biggerArt(o.value(QStringLiteral("artworkUrl100")).toString());
        r.genre = o.value(QStringLiteral("primaryGenreName")).toString();
        r.feedUrl = o.value(QStringLiteral("feedUrl")).toString();
        r.viewUrl = o.value(QStringLiteral("collectionViewUrl")).toString();
        r.trackCount = o.value(QStringLiteral("trackCount")).toInt();
        r.released = isoSecs(o.value(QStringLiteral("releaseDate")).toString());
        if (r.id != 0 && !r.title.isEmpty())
            rows.append(r);
    }
    return rows;
}

QList<ExploreResult> Explore::parseTopChart(const QByteArray &json)
{
    QList<ExploreResult> rows;
    const QJsonValue entries = QJsonDocument::fromJson(json).object()
        .value(QStringLiteral("feed")).toObject().value(QStringLiteral("entry"));
    // A one-entry chart is an object, not an array.
    QJsonArray list = entries.isArray() ? entries.toArray() : QJsonArray{entries};
    for (const auto &value : list) {
        const QJsonObject o = value.toObject();
        auto label = [&o](const char *key) {
            return o.value(QLatin1String(key)).toObject().value(QStringLiteral("label")).toString();
        };
        ExploreResult r;
        const QJsonObject idObj = o.value(QStringLiteral("id")).toObject();
        r.id = idObj.value(QStringLiteral("attributes")).toObject()
            .value(QStringLiteral("im:id")).toString().toLongLong();
        r.viewUrl = idObj.value(QStringLiteral("label")).toString();
        r.title = label("im:name");
        r.author = label("im:artist");
        r.summary = label("summary");
        const QJsonArray images = o.value(QStringLiteral("im:image")).toArray();
        if (!images.isEmpty())
            r.cover = biggerArt(images.last().toObject().value(QStringLiteral("label")).toString());
        r.genre = o.value(QStringLiteral("category")).toObject().value(QStringLiteral("attributes"))
            .toObject().value(QStringLiteral("label")).toString();
        r.released = isoSecs(label("im:releaseDate"));
        if (r.id != 0 && !r.title.isEmpty())
            rows.append(r);
    }
    return rows;
}

QNetworkReply *Explore::get(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    return m_network.get(request);
}

QString Explore::searchKey(const QString &term) const
{
    return QStringLiteral("%1:%2:%3").arg(m_publisher ? QStringLiteral("artist") : QStringLiteral("search"),
                                          m_country, term.simplified().toLower());
}

void Explore::activate()
{
    if (m_query.trimmed().isEmpty() && m_shownKey != topKey())
        startRequest();
}

void Explore::setQuery(const QString &text)
{
    if (text == m_query && !m_publisher)
        return;
    m_query = text;
    m_publisher = false; // typing or clearing returns to the normal search
    emit queryChanged();
    const QString term = text.simplified();
    const QString key = term.isEmpty() ? topKey() : searchKey(term);
    if (m_cache.contains(key)) {
        // Instant: cached list, no timer, drop anything in flight.
        m_debounce->stop();
        ++m_generation;
        if (m_reply)
            m_reply->abort();
        setLoading(false);
        setError({});
        startRequest();
        return;
    }
    m_debounce->start();
}

void Explore::searchNow()
{
    m_debounce->stop();
    startRequest();
}

void Explore::searchPublisher(const QString &name)
{
    const QString term = name.simplified();
    if (term.isEmpty())
        return;
    closePreview();
    m_debounce->stop();
    m_query = term;
    m_publisher = true;
    emit queryChanged();
    startRequest();
}

void Explore::retry()
{
    setError({});
    startRequest();
}

void Explore::startRequest()
{
    m_debounce->stop();
    const QString term = m_query.simplified();
    if (term.isEmpty())
        requestTop();
    else
        requestSearch(term);
}

void Explore::show(const QList<ExploreResult> &rows, const QString &heading)
{
    m_model.setRows(rows);
    setHeading(heading);
    refreshStates();
}

void Explore::requestTop()
{
    const QString heading = QStringLiteral("Top podcasts in %1").arg(m_countryName);
    if (m_cache.contains(topKey())) {
        m_shownKey = topKey();
        show(m_cache.value(topKey()), heading);
        return;
    }
    const int gen = ++m_generation;
    if (m_reply)
        m_reply->abort();
    setError({});
    setLoading(true);
    QNetworkReply *reply = get(QStringLiteral("https://itunes.apple.com/%1/rss/toppodcasts/limit=50/json").arg(m_country));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, heading]() {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        setLoading(false);
        if (reply->error() != QNetworkReply::NoError) {
            setError(errorText(reply));
            return;
        }
        const QList<ExploreResult> rows = parseTopChart(reply->readAll());
        if (rows.isEmpty()) {
            setError(QStringLiteral("The top chart came back empty."));
            return;
        }
        m_cache.insert(topKey(), rows);
        m_shownKey = topKey();
        show(rows, heading);
        // One batch lookup fills in feed URLs, episode counts and dates.
        QList<qint64> ids;
        for (const auto &r : rows)
            ids.append(r.id);
        lookupFeeds(ids, topKey());
    });
}

void Explore::requestSearch(const QString &term)
{
    const QString key = searchKey(term);
    const QString heading = m_publisher ? QStringLiteral("Podcasts from “%1”").arg(term)
                                        : QStringLiteral("Results for “%1”").arg(term);
    if (m_cache.contains(key)) {
        m_shownKey = key;
        show(m_cache.value(key), heading);
        return;
    }
    const int gen = ++m_generation;
    if (m_reply)
        m_reply->abort();
    setError({});
    setLoading(true);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("media"), QStringLiteral("podcast"));
    q.addQueryItem(QStringLiteral("entity"), QStringLiteral("podcast"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    q.addQueryItem(QStringLiteral("country"), m_country);
    q.addQueryItem(QStringLiteral("term"), term);
    if (m_publisher)
        q.addQueryItem(QStringLiteral("attribute"), QStringLiteral("artistTerm"));
    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    url.setQuery(q);
    QNetworkReply *reply = get(url.toString(QUrl::FullyEncoded));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, key, heading]() {
        reply->deleteLater();
        if (gen != m_generation)
            return; // stale: a newer query owns the list
        setLoading(false);
        if (reply->error() != QNetworkReply::NoError) {
            setError(errorText(reply));
            return;
        }
        QList<ExploreResult> rows = parseSearch(reply->readAll());
        if (key.startsWith(QStringLiteral("artist:"))) {
            // Publisher search: newest latest-episode first, undated last.
            std::stable_sort(rows.begin(), rows.end(), [](const ExploreResult &a, const ExploreResult &b) {
                if ((a.released > 0) != (b.released > 0))
                    return a.released > 0;
                return a.released > b.released;
            });
        }
        m_cache.insert(key, rows);
        m_shownKey = key;
        show(rows, heading);
    });
}

void Explore::lookupFeeds(const QList<qint64> &ids, const QString &cacheKey)
{
    if (ids.isEmpty())
        return;
    QStringList parts;
    for (qint64 id : ids)
        parts << QString::number(id);
    QNetworkReply *reply = get(QStringLiteral("https://itunes.apple.com/lookup?entity=podcast&country=%1&id=%2")
                                   .arg(m_country, parts.join(QLatin1Char(','))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cacheKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return; // feed URLs then come from a per-item lookup on subscribe
        QHash<qint64, ExploreResult> found;
        for (const auto &r : parseSearch(reply->readAll()))
            found.insert(r.id, r);
        QList<ExploreResult> rows = m_cache.value(cacheKey);
        for (auto &r : rows) {
            const auto it = found.constFind(r.id);
            if (it == found.constEnd())
                continue;
            r.feedUrl = it->feedUrl;
            r.trackCount = it->trackCount;
            if (it->released)
                r.released = it->released;
            if (!it->cover.isEmpty())
                r.cover = it->cover;
            if (r.genre.isEmpty())
                r.genre = it->genre;
        }
        m_cache.insert(cacheKey, rows);
        if (m_shownKey == cacheKey) {
            // Same ids and order: update feed URLs in place (no reset, covers stay).
            for (const auto &r : rows)
                m_model.setFeedUrl(m_model.indexOfId(r.id), r.feedUrl);
            refreshStates();
        }
    });
}

void Explore::lookupOne(qint64 id, std::function<void(const QString &)> done)
{
    QNetworkReply *reply = get(QStringLiteral("https://itunes.apple.com/lookup?entity=podcast&country=%1&id=%2")
                                   .arg(m_country).arg(id));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, done]() {
        reply->deleteLater();
        QString feed;
        if (reply->error() == QNetworkReply::NoError) {
            for (const auto &r : parseSearch(reply->readAll())) {
                if (r.id == id)
                    feed = r.feedUrl;
            }
        }
        if (!feed.isEmpty()) {
            for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
                for (auto &r : it.value()) {
                    if (r.id == id)
                        r.feedUrl = feed;
                }
            }
            m_model.setFeedUrl(m_model.indexOfId(id), feed);
        }
        done(feed);
    });
}

void Explore::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void Explore::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void Explore::setHeading(const QString &heading)
{
    if (m_heading == heading)
        return;
    m_heading = heading;
    emit headingChanged();
}

void Explore::refreshStates()
{
    QSet<QString> feeds;
    for (const QString &url : m_library.feedUrls())
        feeds.insert(normalizeFeedUrl(url));
    // Landed subscriptions stop counting as "adding".
    for (const auto &r : m_model.rows()) {
        if (!r.feedUrl.isEmpty() && feeds.contains(normalizeFeedUrl(r.feedUrl)))
            m_adding.remove(r.id);
    }
    m_model.setStates(feeds, m_adding);
    if (m_previewOpen) {
        const QString feed = m_preview.value(QStringLiteral("feedUrl")).toString();
        const bool sub = !feed.isEmpty() && feeds.contains(normalizeFeedUrl(feed));
        const bool adding = !sub && m_adding.contains(m_previewId);
        if (sub != m_preview.value(QStringLiteral("subscribed")).toBool()
            || adding != m_preview.value(QStringLiteral("adding")).toBool()) {
            m_preview.insert(QStringLiteral("subscribed"), sub);
            m_preview.insert(QStringLiteral("adding"), adding);
            emit previewChanged();
        }
    }
}

void Explore::addFeed(qint64 id, const QString &feedUrl)
{
    if (feedUrl.isEmpty()) {
        m_adding.remove(id);
        refreshStates();
        setError(QStringLiteral("That podcast has no public feed."));
        return;
    }
    m_backend.addFeed(feedUrl);
}

void Explore::subscribe(int row)
{
    const ExploreResult r = m_model.row(row);
    if (r.id == 0 || m_adding.contains(r.id))
        return;
    if (!r.feedUrl.isEmpty() && m_model.data(m_model.index(row), ExploreModel::SubscribedRole).toBool())
        return;
    m_adding.insert(r.id);
    refreshStates(); // spinner now
    if (!r.feedUrl.isEmpty()) {
        addFeed(r.id, r.feedUrl);
        return;
    }
    lookupOne(r.id, [this, id = r.id](const QString &feed) { addFeed(id, feed); });
}

void Explore::subscribePreview()
{
    const int row = m_model.indexOfId(m_previewId);
    if (row >= 0)
        subscribe(row);
}

void Explore::copyFeedUrl(int row)
{
    const ExploreResult r = m_model.row(row);
    auto copy = [this](const QString &feed) {
        if (feed.isEmpty()) {
            setError(QStringLiteral("That podcast has no public feed."));
            return;
        }
        QGuiApplication::clipboard()->setText(feed);
    };
    if (!r.feedUrl.isEmpty())
        copy(r.feedUrl);
    else if (r.id != 0)
        lookupOne(r.id, copy);
}

void Explore::openInBrowser(int row)
{
    const ExploreResult r = m_model.row(row);
    if (!r.viewUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(r.viewUrl));
}

void Explore::openPreview(int row)
{
    const ExploreResult r = m_model.row(row);
    if (r.id == 0)
        return;
    const int gen = ++m_previewGeneration;
    if (m_previewReply)
        m_previewReply->abort();
    m_previewId = r.id;
    // Directory data first, instantly; the feed fills in description + episodes.
    m_preview = {
        {QStringLiteral("title"), r.title},
        {QStringLiteral("author"), r.author},
        {QStringLiteral("cover"), r.cover},
        {QStringLiteral("genre"), r.genre},
        {QStringLiteral("trackCount"), r.trackCount},
        {QStringLiteral("released"), r.released},
        {QStringLiteral("feedUrl"), r.feedUrl},
        {QStringLiteral("viewUrl"), r.viewUrl},
        {QStringLiteral("description"), plainText(r.summary)},
        {QStringLiteral("episodes"), QVariantList{}},
        {QStringLiteral("row"), row},
    };
    m_previewOpen = true;
    m_previewLoading = true;
    refreshStates();
    emit previewChanged();
    if (!r.feedUrl.isEmpty())
        loadPreviewFeed(r.feedUrl, gen);
    else
        lookupOne(r.id, [this, gen](const QString &feed) {
            if (gen != m_previewGeneration)
                return;
            m_preview.insert(QStringLiteral("feedUrl"), feed);
            refreshStates();
            if (feed.isEmpty()) {
                m_previewLoading = false;
                emit previewChanged();
                return;
            }
            loadPreviewFeed(feed, gen);
        });
}

void Explore::loadPreviewFeed(const QString &feedUrl, int gen)
{
    const QString key = normalizeFeedUrl(feedUrl);
    auto apply = [this](const QVariantMap &parsed) {
        if (!parsed.value(QStringLiteral("description")).toString().isEmpty())
            m_preview.insert(QStringLiteral("description"), parsed.value(QStringLiteral("description")));
        m_preview.insert(QStringLiteral("episodes"), parsed.value(QStringLiteral("episodes")));
        m_preview.insert(QStringLiteral("feedEpisodes"), parsed.value(QStringLiteral("feedEpisodes")));
        m_previewLoading = false;
        emit previewChanged();
    };
    if (m_previewCache.contains(key)) {
        apply(m_previewCache.value(key));
        return;
    }
    QNetworkReply *reply = get(feedUrl);
    m_previewReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, key, feedUrl, apply]() {
        reply->deleteLater();
        if (gen != m_previewGeneration)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            m_previewLoading = false;
            m_preview.insert(QStringLiteral("feedError"), QStringLiteral("Could not load the feed."));
            emit previewChanged();
            return;
        }
        QString error;
        const auto show = parseFeed(reply->readAll(), QUrl(feedUrl), &error);
        QVariantMap parsed;
        if (show) {
            parsed.insert(QStringLiteral("description"), plainText(show->description));
            QList<ParsedEpisode> episodes = show->episodes;
            std::sort(episodes.begin(), episodes.end(), [](const ParsedEpisode &a, const ParsedEpisode &b) {
                return a.published > b.published;
            });
            QVariantList latest;
            for (int i = 0; i < episodes.size() && i < 6; ++i) {
                latest.append(QVariantMap{
                    {QStringLiteral("title"), episodes.at(i).title},
                    {QStringLiteral("published"), episodes.at(i).published},
                    {QStringLiteral("duration"), episodes.at(i).durationSecs},
                });
            }
            parsed.insert(QStringLiteral("episodes"), latest);
            parsed.insert(QStringLiteral("feedEpisodes"), int(show->episodes.size()));
            m_previewCache.insert(key, parsed);
        } else {
            m_preview.insert(QStringLiteral("feedError"), error.isEmpty() ? QStringLiteral("Could not read the feed.") : error);
        }
        apply(parsed);
    });
}

void Explore::closePreview()
{
    if (!m_previewOpen)
        return;
    ++m_previewGeneration;
    if (m_previewReply)
        m_previewReply->abort();
    m_previewOpen = false;
    m_previewLoading = false;
    m_previewId = 0;
    emit previewChanged();
}

// Curated Apple Podcasts storefronts with a top chart (two-letter codes).
QStringList Explore::storefronts()
{
    return {
        QStringLiteral("ae"), QStringLiteral("ar"), QStringLiteral("at"), QStringLiteral("au"),
        QStringLiteral("be"), QStringLiteral("bg"), QStringLiteral("br"), QStringLiteral("ca"),
        QStringLiteral("ch"), QStringLiteral("cl"), QStringLiteral("co"), QStringLiteral("cz"),
        QStringLiteral("de"), QStringLiteral("dk"), QStringLiteral("ee"), QStringLiteral("eg"),
        QStringLiteral("es"), QStringLiteral("fi"), QStringLiteral("fr"), QStringLiteral("gb"),
        QStringLiteral("gr"), QStringLiteral("hk"), QStringLiteral("hr"), QStringLiteral("hu"),
        QStringLiteral("id"), QStringLiteral("ie"), QStringLiteral("il"), QStringLiteral("in"),
        QStringLiteral("is"), QStringLiteral("it"), QStringLiteral("jp"), QStringLiteral("ke"),
        QStringLiteral("kr"), QStringLiteral("lt"), QStringLiteral("lu"), QStringLiteral("lv"),
        QStringLiteral("mx"), QStringLiteral("my"), QStringLiteral("ng"), QStringLiteral("nl"),
        QStringLiteral("no"), QStringLiteral("nz"), QStringLiteral("pe"), QStringLiteral("ph"),
        QStringLiteral("pk"), QStringLiteral("pl"), QStringLiteral("pt"), QStringLiteral("ro"),
        QStringLiteral("sa"), QStringLiteral("se"), QStringLiteral("sg"), QStringLiteral("si"),
        QStringLiteral("sk"), QStringLiteral("th"), QStringLiteral("tr"), QStringLiteral("tw"),
        QStringLiteral("ua"), QStringLiteral("us"), QStringLiteral("vn"), QStringLiteral("za"),
    };
}

QVariantList Explore::regions() const
{
    QList<QPair<QString, QString>> list;
    for (const QString &code : storefronts()) {
        const QLocale::Territory t = QLocale::codeToTerritory(code);
        if (t == QLocale::AnyTerritory)
            continue;
        list.append({QLocale::territoryToString(t), code});
    }
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    QVariantList out;
    for (const auto &entry : list)
        out.append(QVariantMap{{QStringLiteral("code"), entry.second}, {QStringLiteral("name"), entry.first}});
    return out;
}

void Explore::applyCountry()
{
    if (m_override.isEmpty()) {
        m_country = m_autoCountry;
        m_countryName = m_autoName;
        return;
    }
    m_country = m_override;
    const QLocale::Territory t = QLocale::codeToTerritory(m_override);
    m_countryName = t == QLocale::AnyTerritory ? m_override.toUpper() : QLocale::territoryToString(t);
}

void Explore::setRegion(const QString &code)
{
    QString clean = code.trimmed().toLower();
    if (clean.size() != 2)
        clean.clear();
    if (clean == m_override)
        return;
    m_override = clean;
    m_library.setExploreCountry(clean);
    const QString before = m_country;
    applyCountry();
    emit regionChanged();
    if (m_country == before)
        return;
    // Chart and search caches are keyed by country; reload what is on screen.
    m_debounce->stop();
    ++m_generation;
    if (m_reply)
        m_reply->abort();
    setLoading(false);
    setError({});
    m_shownKey.clear();
    startRequest();
}
