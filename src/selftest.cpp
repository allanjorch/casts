#include "covercache.h"
#include "feed.h"
#include "library.h"

#include <QUrl>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
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
<rss version="2.0" xmlns:itunes="http://www.itunes.com/dtds/podcast-1.0.dtd"
     xmlns:content="http://purl.org/rss/1.0/modules/content/"
     xmlns:media="http://search.yahoo.com/mrss/">
<channel>
<title>Sample Show</title>
<itunes:author>Ada</itunes:author>
<itunes:image href="https://example.com/cover.jpg"/>
<item>
  <title>Newest</title>
  <guid>n</guid>
  <pubDate>Wed, 10 Jun 2026 12:00:00 +0000</pubDate>
  <itunes:duration>1:02:03</itunes:duration>
  <description>Short blurb</description>
  <content:encoded><![CDATA[<p>Full <b>episode</b> notes.</p>]]></content:encoded>
  <itunes:image href="https://example.com/ep-n.jpg"/>
  <enclosure url="https://example.com/n.mp3" type="audio/mpeg"/>
</item>
<item>
  <title>Middle</title>
  <guid>m</guid>
  <pubDate>Wed, 03 Jun 2026 12:00:00 +0000</pubDate>
  <itunes:duration>90</itunes:duration>
  <itunes:summary>Middle summary only</itunes:summary>
  <media:thumbnail url="https://example.com/ep-m.jpg"/>
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
            check(parsed->episodes.at(0).description.contains(QStringLiteral("Full episode notes")),
                  "content:encoded preferred");
            check(parsed->episodes.at(0).imageUrl == QStringLiteral("https://example.com/ep-n.jpg"),
                  "itunes episode image");
            check(parsed->episodes.at(1).description == QStringLiteral("Middle summary only"),
                  "itunes summary");
            check(parsed->episodes.at(1).imageUrl == QStringLiteral("https://example.com/ep-m.jpg"),
                  "media thumbnail");
            check(parsed->episodes.at(2).description.isEmpty(), "missing description stays empty");
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
        {
            const QString coverPath = dir.filePath(QStringLiteral("cover.jpg"));
            QFile cover(coverPath);
            check(cover.open(QIODevice::WriteOnly) && cover.write("fake-cover-bytes-for-cache-test") > 0,
                  "write fake cover");
            cover.close();
            library.setShowImage(showId, parsed->imageUrl, coverPath);
            check(library.showImage(showId) == coverPath, "show image path stored");
            library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
            check(library.showImage(showId) == coverPath, "same cover URL keeps image_path");
            ParsedShow moved = *parsed;
            moved.imageUrl = QStringLiteral("https://example.com/cover-v2.jpg");
            library.upsertShow(QStringLiteral("https://example.com/feed.xml"), moved);
            check(library.showImage(showId).isEmpty(), "new cover URL clears image_path");
            check(!QFileInfo::exists(coverPath), "stale cover file removed");
            // Restore original URL for later episode checks.
            library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
        }
        const auto episodes = library.episodes(showId);
        check(episodes.size() == 3, "stored episodes");
        if (episodes.size() == 3) {
            check(episodes.at(0).description.contains(QStringLiteral("Full episode notes")),
                  "stored description");
            check(episodes.at(0).imageUrl == QStringLiteral("https://example.com/ep-n.jpg"),
                  "stored episode image");
            {
                const qint64 epId = episodes.at(0).id;
                const QString epCover = dir.filePath(QStringLiteral("ep-n.jpg"));
                QFile epFile(epCover);
                check(epFile.open(QIODevice::WriteOnly) && epFile.write("episode-cover-bytes") > 0,
                      "write episode cover");
                epFile.close();
                library.setEpisodeImage(epId, episodes.at(0).imageUrl, epCover);
                check(library.episode(epId).imagePath == epCover, "episode image path stored");
                library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
                check(library.episode(epId).imagePath == epCover,
                      "same episode image URL keeps image_path");
                check(QFileInfo::exists(epCover), "episode cover file kept");
                ParsedShow movedEp = *parsed;
                movedEp.episodes[0].imageUrl = QStringLiteral("https://example.com/ep-n-v2.jpg");
                library.upsertShow(QStringLiteral("https://example.com/feed.xml"), movedEp);
                check(library.episode(epId).imagePath.isEmpty(),
                      "new episode image URL clears image_path");
                check(!QFileInfo::exists(epCover), "stale episode cover file removed");
                library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
            }
            {
                // Force-reload contract: drop the file and image_path, keep the URL,
                // and a same-URL upsert must not put the cached path back.
                const QString coverPath = dir.filePath(QStringLiteral("cover-force.jpg"));
                QFile cover(coverPath);
                check(cover.open(QIODevice::WriteOnly) && cover.write("force-show-cover") > 0,
                      "write show cover to force-clear");
                cover.close();
                library.setShowImage(showId, parsed->imageUrl, coverPath);
                check(library.showImageUrl(showId) == parsed->imageUrl, "show image url stored");
                library.clearShowImagePath(showId);
                check(library.showImage(showId).isEmpty(), "force-clear drops show image_path");
                check(library.showImageUrl(showId) == parsed->imageUrl, "force-clear keeps show image url");
                check(!QFileInfo::exists(coverPath), "force-clear removes show cover file");
                library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
                check(library.showImage(showId).isEmpty(), "same cover URL stays empty after force-clear");

                const qint64 epId = episodes.at(0).id;
                const QString epCover = dir.filePath(QStringLiteral("ep-force.jpg"));
                QFile epFile(epCover);
                check(epFile.open(QIODevice::WriteOnly) && epFile.write("force-episode-cover") > 0,
                      "write episode cover to force-clear");
                epFile.close();
                const QString epUrl = library.episode(epId).imageUrl;
                library.setEpisodeImage(epId, epUrl, epCover);
                library.clearEpisodeImagePath(epId);
                check(library.episode(epId).imagePath.isEmpty(), "force-clear drops episode image_path");
                check(library.episode(epId).imageUrl == epUrl, "force-clear keeps episode image url");
                check(!QFileInfo::exists(epCover), "force-clear removes episode cover file");
                library.upsertShow(QStringLiteral("https://example.com/feed.xml"), *parsed);
                check(library.episode(epId).imagePath.isEmpty(),
                      "same episode image URL stays empty after force-clear");
            }
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
            library.setPosition(middle, 123456);
            check(library.episode(middle).positionMs == 123456, "persist position_ms");
            library.setPosition(middle, 0);
            check(library.episode(middle).positionMs == 0, "clear position_ms");
            // Resume clamp: near-end progress restarts; mid progress is kept for Load.
            library.setPosition(newest, 3723 * 1000 - 1000);
            check(library.episode(newest).positionMs == 3723 * 1000 - 1000, "near-end position stored");
            library.setPosition(newest, 900000);
            check(library.episode(newest).positionMs == 900000, "mid position stored for resume");
            library.setLastPlayedEpisodeId(middle);
            check(library.lastPlayedEpisodeId() == middle, "persist lastPlayed episode id");
            library.setLastPlayedEpisodeId(0);
            check(library.lastPlayedEpisodeId() == 0, "clear lastPlayed episode id");
            library.setLastPlayedEpisodeId(newest);
            check(library.lastPlayedEpisodeId() == newest, "set lastPlayed for restore");
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

        if (!library.episodes(showId).isEmpty())
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
        library.setShelfColumns(0);
        check(library.shelfView() == QStringLiteral("list"), "shelf list");
        check(library.shelfColumns() == 1, "columns stay at least one");
        library.setShelfColumns(1);
        check(library.shelfColumns() == 1, "one column is allowed");
        library.setShelfColumns(20);
        check(library.shelfColumns() == 10, "columns stay at most ten");
        library.setShelfColumns(10);
        check(library.shelfColumns() == 10, "ten columns is allowed");
        check(library.shelfListSize() == 0, "list cover starts at the smallest");
        library.setShelfListSize(-3);
        check(library.shelfListSize() == 0, "list cover stays at least the smallest");
        library.setShelfListSize(9);
        check(library.shelfListSize() == 4, "list cover stays at most the largest");
        check(library.episodeListSize() == 0, "episode list cover starts at the smallest");
        library.setEpisodeListSize(-3);
        check(library.episodeListSize() == 0, "episode list cover stays at least the smallest");
        library.setEpisodeListSize(9);
        check(library.episodeListSize() == 4, "episode list cover stays at most the largest");
        check(library.shelfShowAll(), "shelf eye starts open");
        check(library.episodeShowAll(), "episode eye starts open");
        library.setShelfShowAll(false);
        library.setEpisodeShowAll(false);
        check(!library.shelfShowAll(), "shelf eye closes");
        check(!library.episodeShowAll(), "episode eye closes");
        library.setShelfShowAll(true);
        library.setEpisodeShowAll(true);
        check(library.shelfShowAll(), "shelf eye reopens");
        check(library.episodeShowAll(), "episode eye reopens");
        int unplayed = 0;
        for (const auto &show : library.shows()) {
            for (const auto &episode : library.episodes(show.id)) {
                if (!episode.played)
                    ++unplayed;
            }
        }
        check(unplayed > 0, "library still has unplayed episodes");
        check(library.markLibraryPlayed() == unplayed, "mark every show and episode played");
        for (const auto &show : library.shows()) {
            check(show.unheard == 0, "show is caught up after library mark");
            for (const auto &episode : library.episodes(show.id))
                check(episode.played, "episode is played after library mark");
        }
        check(library.markLibraryPlayed() == 0, "mark library again finds nothing left");
    }

    {
        QTemporaryDir qdir;
        Library qlib(qdir.filePath(QStringLiteral("queue.db")));
        QString qerr;
        const auto parsed = parseFeed(rss, QUrl(QStringLiteral("https://example.com/q.xml")), &qerr);
        if (parsed) {
            const qint64 showId = qlib.upsertShow(QStringLiteral("https://example.com/q.xml"), *parsed);
            const auto eps = qlib.episodes(showId);
            check(eps.size() >= 3, "queue fixture episodes");
            if (eps.size() >= 3) {
                const qint64 a = eps.at(0).id, b = eps.at(1).id, c = eps.at(2).id;
                check(qlib.queueIds().isEmpty(), "queue starts empty");
                qlib.addToQueue(a, false);
                qlib.addToQueue(b, false);
                qlib.addToQueue(c, true);
                check(qlib.queueIds() == QList<qint64>({c, a, b}), "queue add / play next order");
                check(qlib.isQueued(a) && qlib.queueHead() == c, "queue head and membership");
                qlib.addToQueue(a, false);
                check(qlib.queueIds() == QList<qint64>({c, b, a}), "re-add moves to end, no duplicate");
                qlib.setQueueOrder({a, c, b});
                check(qlib.queueIds() == QList<qint64>({a, c, b}), "queue reorder persists");
                check(qlib.removeFromQueue(c) && !qlib.isQueued(c), "queue remove");
                check(!qlib.removeFromQueue(c), "queue remove twice is a no-op");
                // Lifetime: queue is now {a, b}.
                qlib.markPlayed(a, true);
                check(qlib.isQueued(a), "manual mark played keeps it queued");
                check(qlib.queueAfter(a) == b, "next after a queued item is the one after it");
                check(qlib.queueAfter(b) == 0, "nothing after the last queued item");
                check(qlib.queueAfter(c) == a, "unqueued current goes to the queue top");
                qlib.addToQueue(c, false); // {a, b, c}
                check(qlib.finishQueued(b) == c, "finishing b plays c (picked before removal)");
                check(qlib.queueIds() == QList<qint64>({a, c}), "finish removes only the finished one");
                check(qlib.finishQueued(b) == a, "finishing an unqueued episode plays the top");
                check(qlib.queueIds() == QList<qint64>({a, c}), "finishing an unqueued episode removes nothing");
                check(qlib.finishQueued(c) == 0 && qlib.queueIds() == QList<qint64>({a}),
                      "finishing the last item falls back (0) and removes it");
                qlib.addToQueue(b, false); // {a, b}
                const auto rows = qlib.queue();
                check(rows.size() == 2 && rows.at(0).showTitle == QStringLiteral("Sample Show"), "queue rows carry show title");
                qlib.removeShow(showId);
                check(qlib.queueIds().isEmpty(), "removing a show drops its queued episodes");
                qlib.clearQueue();
            }
        }
    }

    {
        QTemporaryDir covers;
        const QString path = covers.filePath(QStringLiteral("episode art.jpg"));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write("x") > 0, "write cover for url");
        file.close();
        const QString src = localCoverSource(path);
        check(src.startsWith(QStringLiteral("image://covers/")), "cover url scheme");
        // Same slash-stripping QQuick applies when it asks the image provider.
        QString id = QUrl(src).toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
        id = QUrl::fromPercentEncoding(id.toUtf8());
        if (!id.startsWith(QLatin1Char('/')) && QFileInfo::exists(QLatin1Char('/') + id))
            id.prepend(QLatin1Char('/'));
        check(id == path, "cover url round trip");
        check(localCoverSource(QString()).isEmpty(), "missing cover has no url");
    }

    if (g_fails == 0) {
        printf("ok\n");
        return 0;
    }
    fprintf(stderr, "%d failed\n", g_fails);
    return 1;
}
