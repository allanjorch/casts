#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlNetworkAccessManagerFactory>

// User-Agent for EPISODE AUDIO only (HttpFileBuffer: play, prefetch, resume).
//
// Some hosts (e.g. Flightcast, which serves The Diary of a CEO) stitch local
// ads into the file when the request comes from a recognised podcast app in
// certain countries. A generic media player identity (the one ffmpeg, mpv and
// VLC send) gets the plain file. If a host refuses that identity, the
// downloader retries once with kAudioAppUserAgent (may carry ads). Feeds,
// artwork and Explore keep the app's own identity.
inline constexpr const char kAudioGenericUserAgent[] = "Lavf/61.1.100";
inline constexpr const char kAudioAppUserAgent[] =
    "omaear/1.0 (+https://github.com/allanjorch/omaear)";

// Every network request in both processes goes through this manager.
//
// HTTP/2 is turned off on purpose. Qt's HTTP/2 path does not race IPv6 against
// IPv4 the way HTTP/1.1 does, so on a network with broken IPv6 the first request
// to a dual-stack host stalls ~5 s, and HTTP/2 connection setup is slower
// for covers here even when IPv6 is not involved. Redirect hops keep the
// attribute, since Qt copies the original request when it follows them.
class AppNetworkAccessManager : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        QNetworkRequest req(request);
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        return QNetworkAccessManager::createRequest(op, req, outgoingData);
    }
};

// Qt Quick (remote Image sources such as Explore covers or episode art that is
// not cached yet, XMLHttpRequest) uses this so QML loads take the same path.
class AppNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        return new AppNetworkAccessManager(parent);
    }
};
