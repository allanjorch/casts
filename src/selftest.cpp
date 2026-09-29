#include "feed.h"
#include "library.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTemporaryDir>

namespace {
int g_fails = 0;

void check(bool ok, const char *what)
{
    if (ok)
        return;
    ++g_fails;
    fprintf(stderr, "FAIL %s\n", what);
}
}

int runSelfTest(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("allanjorch"));
    app.setApplicationName(QStringLiteral("podcast-selftest"));

    check(parseDuration(QStringLiteral("90")) == 90, "duration seconds");
    check(parseDuration(QStringLiteral("1:02:03")) == 3723, "duration hms");
    check(parseDuration(QStringLiteral("12:34")) == 754, "duration ms");
    check(parsePublished(QStringLiteral("Wed, 10 Jun 2026 12:00:00 +0000")) > 0, "rfc2822");
    check(parsePublished(QStringLiteral("2026-01-02T00:00:00Z")) > 0, "iso");

    const QByteArray rss = R"(<?xml version="1.0"?>
<rss version="2.0" xmlns:itunes="http://www.itunes.com/dtds/podcast-1.0.dtd">
<channel>
<title>Sample Show</title>
<itunes:author>Ada</itunes:author>
<itunes:image href="https://example.com/cover.jpg"/>
<item>
  <title>Newest</title>
  <guid>n</guid>
  <pubDate>Wed, 10 Jun 2026 12:00:00 +0000</pubDate>
  <itunes:duration>1:02:03</itunes:duration>
  <enclosure url="https://example.com/n.mp3" type="audio/mpeg"/>
</item>
<item>
  <title>Middle</title>
  <guid>m</guid>
  <pubDate>Wed, 03 Jun 2026 12:00:00 +0000</pubDate>
  <itunes:duration>90</itunes:duration>
  <enclosure url="https://example.com/m.mp3" type="audio/mpeg"/>
</item>
<item>
  <title>Older</title>
  <guid>o</guid>
  <pubDate>Wed, 27 May 2026 12:00:00 +0000</pubDate>
  <enclosure url="https://example.com/o.m4a" type="audio/mp4"/>
</item>
<item>
  <title>Clip</title>
  <guid>v</guid>
  <pubDate>Fri, 01 May 2026 12:00:00 +0000</pubDate>
  <enclosure url="https://example.com/v.mp4" type="video/mp4"/>
</item>
</channel>
</rss>)";

    QString error;
    const auto parsed = parseFeed(rss, QUrl(QStringLiteral("https://example.com/feed.xml")), &error);
    check(parsed.has_value(), "parse rss");
    if (parsed) {
        check(parsed->title == QStringLiteral("Sample Show"), "show title");
        check(parsed->author == QStringLiteral("Ada"), "author");
        check(parsed->imageUrl == QStringLiteral("https://example.com/cover.jpg"), "cover");
        check(parsed->episodes.size() == 3, "audio episodes only");
        if (parsed->episodes.size() == 3) {
            check(parsed->episodes.at(0).title == QStringLiteral("Newest"), "newest title");
            check(parsed->episodes.at(0).durationSecs == 3723, "newest duration");
            check(parsed->episodes.at(0).published > parsed->episodes.at(1).published, "date order");
            check(parsed->episodes.at(2).audioUrl.endsWith(QStringLiteral("o.m4a")), "m4a kept");
        }
    }

    const auto html = parseFeed("<!DOCTYPE html><html></html>", QUrl(), &error);
    check(!html.has_value(), "reject html");

    const auto atom = parseFeed(R"(<feed xmlns="http://www.w3.org/2005/Atom">
<title>Atom Show</title>
<entry>
<title>One</title>
<id>one</id>
<published>2026-01-02T00:00:00Z</published>
<link rel="enclosure" type="audio/mpeg" href="https://example.com/one.mp3"/>
</entry>
</feed>)",
                                QUrl(QStringLiteral("https://example.com/atom")), &error);
    check(atom.has_value() && atom->episodes.size() == 1, "atom enclosure");

    const QStringList opml = parseOpml(R"(<opml version="2.0"><body>
<outline text="A" xmlUrl="https://example.com/a.xml"/>
<outline text="Folder">
  <outline text="B" xmlUrl="https://example.com/b.xml"/>
</outline>
<outline text="A again" xmlUrl="https://example.com/a.xml"/>
</body></opml>)",
                                        &error);
    check(opml.size() == 2, "opml dedupes and nests");

    QTemporaryDir dir;
    check(dir.isValid(), "temp dir");
    Library library(dir.filePath(QStringLiteral("library.db")));
    check(library.isOpen(), "open library");
    if (parsed && library.isOpen()) {
        const qint64 showId = library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
        check(showId != 0, "insert show");
        const auto again = library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
        check(again == showId, "refresh keeps the show");
        const auto episodes = library.episodes(showId);
        check(episodes.size() == 3, "stored episodes");
        if (episodes.size() == 3) {
            const qint64 newest = episodes.at(0).id;
            const qint64 middle = episodes.at(1).id;
            const qint64 older = episodes.at(2).id;
            check(library.markOlderPlayed(middle) == 1, "mark older");
            check(!library.episode(newest).played, "newest stays unplayed");
            check(!library.episode(middle).played, "chosen episode stays unplayed");
            check(library.episode(older).played, "older is played");
            check(library.markNewerPlayed(middle) == 1, "mark newer");
            check(library.episode(newest).played, "newer is played");
            check(library.markPlayed(middle, true) == 1, "mark one");
            check(library.markAllPlayed(showId) == 0, "mark all finds nothing left");
            check(library.markPlayed(middle, false) == 1, "mark unplayed");
            check(library.markAllPlayed(showId) == 1, "mark all");
            check(library.episode(middle).played, "all played");
            check(library.adjacent(middle, true, false) == older, "adjacent older");
            check(library.adjacent(middle, false, false) == newest, "adjacent newer");
        }

        ParsedShow caughtUp = *parsed;
        caughtUp.title = QStringLiteral("Caught Up");
        for (auto &episode : caughtUp.episodes)
            episode.guid += QStringLiteral("-b");
        const qint64 caughtId = library.upsertShow(QStringLiteral("https://example.com/caught.xml"), caughtUp);
        library.markAllPlayed(caughtId);

        ParsedShow fresh = *parsed;
        fresh.title = QStringLiteral("Fresh");
        fresh.episodes[0].published = parsed->episodes.at(0).published + 86400;
        for (auto &episode : fresh.episodes)
            episode.guid += QStringLiteral("-c");
        library.upsertShow(QStringLiteral("https://example.com/fresh.xml"), fresh);

        library.markPlayed(library.episodes(showId).at(0).id, false);
        const auto shelf = library.shows();
        check(shelf.size() == 3, "three shows");
        if (shelf.size() == 3) {
            check(shelf.at(0).title == QStringLiteral("Fresh"), "unheard newest first");
            check(shelf.at(0).unheard > 0, "fresh is unheard");
            check(shelf.at(2).unheard == 0, "caught up is last");
        }
        check(library.shelfView() == QStringLiteral("gallery"), "shelf starts as gallery");
        check(library.shelfColumns() == 5, "shelf starts at five columns");
        library.setShelfView(QStringLiteral("list"));
        library.setShelfColumns(1);
        check(library.shelfView() == QStringLiteral("list"), "shelf list");
        check(library.shelfColumns() == 2, "columns stay at least two");
        library.setShelfColumns(20);
        check(library.shelfColumns() == 8, "columns stay at most eight");
        check(library.shelfListSize() == 0, "list cover starts at the smallest");
        library.setShelfListSize(-3);
        check(library.shelfListSize() == 0, "list cover stays at least the smallest");
        library.setShelfListSize(9);
        check(library.shelfListSize() == 4, "list cover stays at most the largest");
    }

    if (g_fails == 0) {
        printf("ok\n");
        return 0;
    }
    fprintf(stderr, "%d failed\n", g_fails);
    return 1;
}
