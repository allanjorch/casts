#include "feed.h"

#include <QDateTime>
#include <QUrl>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace {
bool looksLikeAudio(const QString &type, const QString &url)
{
    if (type.startsWith(QStringLiteral("audio/"), Qt::CaseInsensitive))
        return true;
    if (type.startsWith(QStringLiteral("video/"), Qt::CaseInsensitive))
        return false;
    const QString path = QUrl(url).path().toLower();
    return path.endsWith(QStringLiteral(".mp3"))
        || path.endsWith(QStringLiteral(".m4a"))
        || path.endsWith(QStringLiteral(".aac"))
        || path.endsWith(QStringLiteral(".ogg"))
        || path.endsWith(QStringLiteral(".opus"))
        || path.endsWith(QStringLiteral(".wav"))
        || path.endsWith(QStringLiteral(".flac"));
}

void considerAudio(ParsedEpisode &episode, const QString &url, const QString &type, const QUrl &base)
{
    if (url.isEmpty() || !episode.audioUrl.isEmpty())
        return;
    const QUrl resolved = base.resolved(QUrl(url));
    const QString absolute = resolved.isValid() ? resolved.toString() : url;
    if (!looksLikeAudio(type, absolute))
        return;
    episode.audioUrl = absolute;
}

QString resolveUrl(const QString &url, const QUrl &base)
{
    if (url.isEmpty())
        return {};
    const QUrl resolved = base.resolved(QUrl(url.trimmed()));
    return resolved.isValid() ? resolved.toString() : url.trimmed();
}

QString plainText(const QString &value)
{
    QString text = value.trimmed();
    if (text.isEmpty())
        return {};
    if (text.contains(QLatin1Char('<'))) {
        static const QRegularExpression breakTag(
            QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression closePara(
            QStringLiteral("</p\\s*>"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression anyTag(QStringLiteral("<[^>]+>"));
        text.replace(breakTag, QStringLiteral("\n"));
        text.replace(closePara, QStringLiteral("\n"));
        text.replace(anyTag, QString());
    }
    text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&#39;"), QLatin1String("'"));
    text.replace(QRegularExpression(QStringLiteral("[\t\r ]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}


void preferDescription(ParsedEpisode &episode, const QString &value)
{
    const QString plain = plainText(value);
    if (plain.isEmpty())
        return;
    if (episode.description.isEmpty() || plain.size() > episode.description.size())
        episode.description = plain;
}

void considerImage(ParsedEpisode &episode, const QString &url, const QUrl &base)
{
    if (url.isEmpty() || !episode.imageUrl.isEmpty())
        return;
    episode.imageUrl = resolveUrl(url, base);
}
}

qint64 parsePublished(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return 0;
    QDateTime dt = QDateTime::fromString(trimmed, Qt::RFC2822Date);
    if (!dt.isValid())
        dt = QDateTime::fromString(trimmed, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!dt.isValid())
        return 0;
    return dt.toSecsSinceEpoch();
}

int parseDuration(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return 0;
    const QStringList parts = trimmed.split(QLatin1Char(':'));
    bool ok = false;
    if (parts.size() == 1) {
        const int seconds = parts.at(0).toInt(&ok);
        return ok ? seconds : 0;
    }
    if (parts.size() == 2)
        return parts.at(0).toInt() * 60 + parts.at(1).toInt();
    if (parts.size() == 3)
        return parts.at(0).toInt() * 3600 + parts.at(1).toInt() * 60 + parts.at(2).toInt();
    return 0;
}

std::optional<ParsedShow> parseFeed(const QByteArray &xml, const QUrl &base, QString *error)
{
    const QByteArray trimmed = xml.trimmed();
    if (trimmed.startsWith("<!DOCTYPE") || trimmed.startsWith("<html")
        || trimmed.startsWith("<HTML") || trimmed.startsWith("<!doctype")) {
        if (error)
            *error = QStringLiteral("That page is not a podcast feed. Paste the feed address itself.");
        return std::nullopt;
    }

    QXmlStreamReader reader(xml);
    ParsedShow show;
    ParsedEpisode current;
    QStringList stack;
    QString text;
    bool sawFeed = false;

    auto inEpisode = [&stack]() {
        return stack.contains(QStringLiteral("item")) || stack.contains(QStringLiteral("entry"));
    };

    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = reader.name().toString();
            stack.append(name);
            text.clear();
            sawFeed = sawFeed || name == QStringLiteral("rss") || name == QStringLiteral("feed")
                || name == QStringLiteral("channel");
            const auto attrs = reader.attributes();
            const bool itunes = reader.namespaceUri().toString().contains(QStringLiteral("itunes"))
                || reader.prefix() == QStringLiteral("itunes");
            const bool media = reader.namespaceUri().toString().contains(QStringLiteral("mrss"))
                || reader.prefix() == QStringLiteral("media");

            if (inEpisode()) {
                if (itunes && name == QStringLiteral("image"))
                    considerImage(current, attrs.value(QStringLiteral("href")).toString(), base);
                if (media && name == QStringLiteral("thumbnail"))
                    considerImage(current, attrs.value(QStringLiteral("url")).toString(), base);
                if (name == QStringLiteral("enclosure"))
                    considerAudio(current, attrs.value(QStringLiteral("url")).toString(),
                                  attrs.value(QStringLiteral("type")).toString(), base);
                else if (media && name == QStringLiteral("content")) {
                    const QString type = attrs.value(QStringLiteral("type")).toString();
                    const QString medium = attrs.value(QStringLiteral("medium")).toString();
                    const QString url = attrs.value(QStringLiteral("url")).toString();
                    if (medium == QStringLiteral("image")
                        || type.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive))
                        considerImage(current, url, base);
                    else
                        considerAudio(current, url, type, base);
                } else if (name == QStringLiteral("link")
                         && attrs.value(QStringLiteral("rel")) == QStringLiteral("enclosure"))
                    considerAudio(current, attrs.value(QStringLiteral("href")).toString(),
                                  attrs.value(QStringLiteral("type")).toString(), base);
            } else if (itunes && name == QStringLiteral("image") && show.imageUrl.isEmpty()) {
                show.imageUrl = resolveUrl(attrs.value(QStringLiteral("href")).toString(), base);
            }
        } else if (token == QXmlStreamReader::Characters) {
            text += reader.text();
        } else if (token == QXmlStreamReader::EndElement) {
            const QString name = reader.name().toString();
            const QString value = text.trimmed();
            const bool episode = inEpisode();
            const bool contentNs = reader.namespaceUri().toString().contains(QStringLiteral("content"))
                || reader.prefix() == QStringLiteral("content");
            const bool itunesEnd = reader.namespaceUri().toString().contains(QStringLiteral("itunes"))
                || reader.prefix() == QStringLiteral("itunes");
            if (episode && name == QStringLiteral("title") && current.title.isEmpty())
                current.title = value;
            else if (episode && (name == QStringLiteral("guid") || name == QStringLiteral("id"))
                     && current.guid.isEmpty())
                current.guid = value;
            else if (episode && current.published == 0
                     && (name == QStringLiteral("pubDate") || name == QStringLiteral("published")
                         || name == QStringLiteral("updated")))
                current.published = parsePublished(value);
            else if (episode && name == QStringLiteral("duration") && current.durationSecs == 0)
                current.durationSecs = parseDuration(value);
            else if (episode && contentNs && name == QStringLiteral("encoded"))
                preferDescription(current, value);
            else if (episode && name == QStringLiteral("description"))
                preferDescription(current, value);
            else if (episode && name == QStringLiteral("summary")
                     && (itunesEnd || !contentNs))
                preferDescription(current, value);
            else if (episode && name == QStringLiteral("content")
                     && reader.prefix() != QStringLiteral("media")
                     && !reader.namespaceUri().toString().contains(QStringLiteral("mrss")))
                preferDescription(current, value);
            else if (episode && name == QStringLiteral("url") && stack.contains(QStringLiteral("image")))
                considerImage(current, value, base);
            else if (!episode && name == QStringLiteral("title") && show.title.isEmpty())
                show.title = value;
            else if (!episode && name == QStringLiteral("author") && show.author.isEmpty())
                show.author = value;
            else if (!episode && show.description.isEmpty() && !stack.contains(QStringLiteral("image"))
                     && (name == QStringLiteral("description") || name == QStringLiteral("summary")
                         || name == QStringLiteral("subtitle")))
                show.description = value;
            else if (!episode && name == QStringLiteral("url") && stack.contains(QStringLiteral("image"))
                     && show.imageUrl.isEmpty())
                show.imageUrl = resolveUrl(value, base);

            if (name == QStringLiteral("item") || name == QStringLiteral("entry")) {
                if (!current.audioUrl.isEmpty()) {
                    if (current.guid.isEmpty())
                        current.guid = current.audioUrl;
                    if (current.title.isEmpty())
                        current.title = QStringLiteral("Untitled");
                    show.episodes.append(current);
                }
                current = {};
            }
            if (!stack.isEmpty())
                stack.removeLast();
            text.clear();
        }
    }

    if (reader.hasError() && show.episodes.isEmpty() && show.title.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not read that feed.");
        return std::nullopt;
    }
    if (!sawFeed || show.title.isEmpty()) {
        if (error)
            *error = QStringLiteral("That address is not a podcast feed.");
        return std::nullopt;
    }
    if (show.imageUrl.isEmpty()) {
        for (const auto &episode : show.episodes) {
            if (!episode.imageUrl.isEmpty()) {
                show.imageUrl = episode.imageUrl;
                break;
            }
        }
    }
    return show;
}

QStringList parseOpml(const QByteArray &xml, QString *error)
{
    QXmlStreamReader reader(xml);
    QStringList urls;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QStringLiteral("outline"))
            continue;
        const auto attrs = reader.attributes();
        QString url = attrs.value(QStringLiteral("xmlUrl")).toString().trimmed();
        if (url.isEmpty())
            url = attrs.value(QStringLiteral("xmlurl")).toString().trimmed();
        if (!url.isEmpty() && !urls.contains(url))
            urls.append(url);
    }
    if (reader.hasError() && urls.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not read that OPML file.");
        return {};
    }
    if (urls.isEmpty() && error)
        *error = QStringLiteral("That OPML file has no feeds.");
    return urls;
}
