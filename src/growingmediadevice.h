#pragma once

#include <QIODevice>
#include <QMutex>
#include "netaccess.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QWaitCondition>

class QFile;
class QNetworkReply;
class GrowingMediaDevice;

// Downloads an episode URL into a growing cache file under CacheLocation/media/.
// Reader (GrowingMediaDevice) only exposes bytes already flushed to disk; atEnd
// stays false until the download finishes so FFmpeg never sees a premature EOF.
class HttpFileBuffer : public QObject {
    Q_OBJECT

public:
    explicit HttpFileBuffer(qint64 episodeId, const QUrl &url, QObject *parent = nullptr);
    ~HttpFileBuffer() override;

    GrowingMediaDevice *device() const { return m_device; }
    QUrl formatHintUrl() const { return m_hintUrl; }
    bool isComplete() const;
    bool isFailed() const { return m_failed; }
    QString errorString() const { return m_error; }
    QString cachePath() const; // .part or finished path
    qint64 availableBytes() const;
    // HTTP Content-Length when known (>0); 0 if unknown/chunked; -1 before headers.
    qint64 totalSize() const;
    bool headersReceived() const;

    // If a finished local file already exists, returns its path and does not start a download.
    QString existingFinishedPath() const;

    void start();
    void abort();

    static QString mediaDir();
    static void pruneCache(qint64 keepEpisodeId, qint64 keepNextId = 0);
    // Deletes every cached episode file (finished, .part and UA sidecars).
    // Call only when nothing is playing. Returns the number of media files removed.
    static int purgeMediaCache();

    // Audio User-Agent to try first for this URL's host this session: the generic
    // player identity, or the app's own if that host refused the generic one.
    static QByteArray preferredUserAgent(const QUrl &url);

signals:
    void progressed(qint64 availableBytes);
    void completed(const QString &finishedPath);
    void failed(const QString &message);

private:
    friend class GrowingMediaDevice;

    void startRequest(qint64 fromOffset);
    void onReadyRead();
    void onReplyFinished();
    void scheduleRetry();
    void flushWriter(bool force);
    bool finalizeRename();
    void setAvailableLocked(qint64 bytes);
    void wakeReaders();
    QString partPath() const;
    QString uaSidecarPath() const;
    void writeUaSidecar();
    bool headersRejected(QNetworkReply *reply) const;
    void evaluateHeaders(QNetworkReply *reply);
    bool tryUserAgentFallback(bool httpRefusal, const QString &why);
    QString finishedPathForExt(const QString &ext) const;
    QString chooseExtension(const QString &contentType) const;
    static QString extensionFromUrl(const QUrl &url);

    qint64 m_episodeId = 0;
    QUrl m_url;
    QUrl m_hintUrl;
    QString m_ext = QStringLiteral(".mp3");
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_writer = nullptr;
    GrowingMediaDevice *m_device = nullptr;

    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    qint64 m_available = 0;
    qint64 m_contentLength = -1;
    qint64 m_unflushed = 0;
    bool m_complete = false;
    bool m_aborted = false;
    bool m_failed = false;
    bool m_acceptRanges = true;
    int m_retries = 0;
    QString m_error;

    // User-Agent consistency: every request for one .part uses m_ua; the
    // sidecar records which UA produced the bytes on disk so a resume never
    // splices two ad variants together.
    QByteArray m_ua;
    QByteArray m_sidecarUa;     // UA recorded for the bytes currently on disk
    bool m_reqHeadersSeen = false; // per request
    bool m_reqRejected = false;    // per request: 4xx/5xx or non-audio type
    bool m_reqAccepted = false;    // per request: audio headers accepted
    bool m_gotAudioThisRun = false; // any request of this buffer was accepted
    bool m_triedFallback = false;
    bool m_fallbackInFlight = false;
    bool m_genericRefusedByHost = false;
    bool m_truncateOnAccept = false; // next accepted response restarts the file at 0
};

class GrowingMediaDevice : public QIODevice {
    Q_OBJECT

public:
    explicit GrowingMediaDevice(HttpFileBuffer *buffer, QObject *parent = nullptr);
    ~GrowingMediaDevice() override;

    bool open(OpenMode mode) override;
    void close() override;

    bool isSequential() const override { return false; }
    qint64 size() const override;
    bool atEnd() const override;
    bool seek(qint64 pos) override;
    qint64 bytesAvailable() const override;

    // Called by HttpFileBuffer when more bytes are flushed to disk.
    void notifyMoreData();

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    HttpFileBuffer *m_buffer = nullptr;
    QFile *m_reader = nullptr;
};
