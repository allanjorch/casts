#include "growingmediadevice.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr qint64 kFlushEvery = 256 * 1024;       // flush cadence for reader visibility
constexpr qint64 kMaxCacheBytes = 1500LL * 1024 * 1024; // ~1.5 GiB soft cap
constexpr qint64 kPartMaxAgeMs = 24LL * 60 * 60 * 1000; // 1 day
constexpr int kMaxRetries = 12;
constexpr int kRetryBaseMs = 800;
}

HttpFileBuffer::HttpFileBuffer(qint64 episodeId, const QUrl &url, QObject *parent)
    : QObject(parent)
    , m_episodeId(episodeId)
    , m_url(url)
    , m_nam(new AppNetworkAccessManager(this)) // HTTP/1.1 only, see netaccess.h
{
    m_ext = extensionFromUrl(url);
    m_hintUrl = QUrl(QStringLiteral("file:episode%1%2").arg(episodeId).arg(m_ext));
    m_device = new GrowingMediaDevice(this, this);
}

HttpFileBuffer::~HttpFileBuffer()
{
    abort();
}

QString HttpFileBuffer::mediaDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/media");
    QDir().mkpath(dir);
    return dir;
}

QString HttpFileBuffer::extensionFromUrl(const QUrl &url)
{
    const QString path = url.path().toLower();
    if (path.endsWith(QStringLiteral(".m4a")) || path.endsWith(QStringLiteral(".mp4")))
        return QStringLiteral(".m4a");
    if (path.endsWith(QStringLiteral(".aac")))
        return QStringLiteral(".aac");
    if (path.endsWith(QStringLiteral(".ogg")) || path.endsWith(QStringLiteral(".oga")))
        return QStringLiteral(".ogg");
    if (path.endsWith(QStringLiteral(".opus")))
        return QStringLiteral(".opus");
    if (path.endsWith(QStringLiteral(".mp3")))
        return QStringLiteral(".mp3");
    return QStringLiteral(".mp3");
}

QString HttpFileBuffer::chooseExtension(const QString &contentType) const
{
    const QString ct = contentType.toLower();
    if (ct.contains(QStringLiteral("mp4")) || ct.contains(QStringLiteral("aac"))
        || ct.contains(QStringLiteral("m4a")))
        return QStringLiteral(".m4a");
    if (ct.contains(QStringLiteral("ogg")) || ct.contains(QStringLiteral("opus")))
        return QStringLiteral(".ogg");
    if (ct.contains(QStringLiteral("mpeg")) || ct.contains(QStringLiteral("mp3")))
        return QStringLiteral(".mp3");
    return extensionFromUrl(m_url);
}

QString HttpFileBuffer::partPath() const
{
    return mediaDir() + QStringLiteral("/%1.part").arg(m_episodeId);
}

QString HttpFileBuffer::finishedPathForExt(const QString &ext) const
{
    return mediaDir() + QStringLiteral("/%1%2").arg(m_episodeId).arg(ext);
}

QString HttpFileBuffer::cachePath() const
{
    QMutexLocker lock(&m_mutex);
    if (m_complete)
        return finishedPathForExt(m_ext);
    return partPath();
}

QString HttpFileBuffer::existingFinishedPath() const
{
    // Prefer exact ext from URL; also accept sibling finished names.
    const QStringList candidates = {
        finishedPathForExt(m_ext),
        finishedPathForExt(QStringLiteral(".mp3")),
        finishedPathForExt(QStringLiteral(".m4a")),
        finishedPathForExt(QStringLiteral(".aac")),
        finishedPathForExt(QStringLiteral(".ogg")),
        finishedPathForExt(QStringLiteral(".opus")),
        mediaDir() + QStringLiteral("/%1").arg(m_episodeId),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path) && QFileInfo(path).size() > 0)
            return path;
    }
    return {};
}

bool HttpFileBuffer::isComplete() const
{
    QMutexLocker lock(&m_mutex);
    return m_complete;
}

qint64 HttpFileBuffer::availableBytes() const
{
    QMutexLocker lock(&m_mutex);
    return m_available;
}

qint64 HttpFileBuffer::totalSize() const
{
    QMutexLocker lock(&m_mutex);
    if (m_complete)
        return m_available;
    // m_contentLength > 0: known HTTP length; 0: headers seen but unknown; -1: not yet.
    return m_contentLength;
}

bool HttpFileBuffer::headersReceived() const
{
    QMutexLocker lock(&m_mutex);
    return m_contentLength >= 0;
}

void HttpFileBuffer::setAvailableLocked(qint64 bytes)
{
    m_available = bytes;
}

void HttpFileBuffer::wakeReaders()
{
    m_cond.wakeAll();
}

void HttpFileBuffer::start()
{
    if (m_aborted)
        return;

    pruneCache(m_episodeId);

    const QString finished = existingFinishedPath();
    if (!finished.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        m_ext = QStringLiteral(".") + QFileInfo(finished).suffix();
        if (m_ext == QStringLiteral("."))
            m_ext = QStringLiteral(".mp3");
        m_hintUrl = QUrl(QStringLiteral("file:episode%1%2").arg(m_episodeId).arg(m_ext));
        m_available = QFileInfo(finished).size();
        m_complete = true;
        lock.unlock();
        qInfo("[podcast-cache] episode %lld already local: %s (%lld bytes)",
              static_cast<long long>(m_episodeId), qPrintable(finished),
              static_cast<long long>(QFileInfo(finished).size()));
        emit completed(finished);
        return;
    }

    const QString part = partPath();
    qint64 existing = 0;
    if (QFileInfo::exists(part))
        existing = QFileInfo(part).size();

    m_writer = new QFile(part, this);
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if (existing > 0)
        mode |= QIODevice::Append;
    else
        mode |= QIODevice::Truncate;

    if (!m_writer->open(mode)) {
        m_failed = true;
        m_error = QStringLiteral("Cannot open cache file: %1").arg(m_writer->errorString());
        qWarning("[podcast-cache] %s", qPrintable(m_error));
        emit failed(m_error);
        return;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_available = existing;
        m_unflushed = 0;
    }

    qInfo("[podcast-cache] download start episode %lld → %s (resume from %lld)",
          static_cast<long long>(m_episodeId), qPrintable(part),
          static_cast<long long>(existing));
    startRequest(existing);
}

void HttpFileBuffer::startRequest(qint64 fromOffset)
{
    if (m_aborted || m_failed)
        return;

    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QNetworkRequest req(m_url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("podcast/1.0 (+https://github.com/allanjorch/casts)"));
    // Prefer identity so .part bytes match the final file without on-the-fly decode surprises.
    req.setRawHeader("Accept-Encoding", "identity");
    if (fromOffset > 0) {
        req.setRawHeader("Range",
                         QByteArray("bytes=") + QByteArray::number(fromOffset) + QByteArray("-"));
        qInfo("[podcast-cache] Range retry episode %lld from offset %lld",
              static_cast<long long>(m_episodeId), static_cast<long long>(fromOffset));
    }

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &HttpFileBuffer::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &HttpFileBuffer::onReplyFinished);
}

void HttpFileBuffer::onReadyRead()
{
    if (!m_reply || !m_writer || m_aborted)
        return;

    // Capture headers once; if we asked for Range but got 200, rewrite from scratch
    // before any bytes are appended onto a partial .part.
    if (m_contentLength < 0) {
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool askedRange = m_reply->request().hasRawHeader("Range");
        if (askedRange && status == 200 && m_writer->size() > 0) {
            qWarning("[podcast-cache] server ignored Range for episode %lld; rewriting",
                     static_cast<long long>(m_episodeId));
            m_writer->resize(0);
            m_writer->seek(0);
            {
                QMutexLocker lock(&m_mutex);
                setAvailableLocked(0);
                m_unflushed = 0;
                wakeReaders();
            }
        }

        const QVariant cl = m_reply->header(QNetworkRequest::ContentLengthHeader);
        if (cl.isValid()) {
            const qint64 len = cl.toLongLong();
            QMutexLocker lock(&m_mutex);
            if (status == 206)
                m_contentLength = m_available + len;
            else if (m_available == 0)
                m_contentLength = len;
        }
        const QByteArray ar = m_reply->rawHeader("Accept-Ranges").toLower();
        if (!ar.isEmpty())
            m_acceptRanges = ar.contains("bytes");
        const QString ct = m_reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (!ct.isEmpty()) {
            const QString ext = chooseExtension(ct);
            if (ext != m_ext) {
                m_ext = ext;
                m_hintUrl = QUrl(QStringLiteral("file:episode%1%2").arg(m_episodeId).arg(m_ext));
            }
        }
        // Mark headers seen even when Content-Length is absent.
        if (m_contentLength < 0)
            m_contentLength = 0;
    }

    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty())
        return;

    const qint64 written = m_writer->write(chunk);
    if (written != chunk.size()) {
        m_failed = true;
        m_error = QStringLiteral("Cache write failed: %1").arg(m_writer->errorString());
        qWarning("[podcast-cache] %s", qPrintable(m_error));
        m_reply->abort();
        emit failed(m_error);
        return;
    }

    m_unflushed += written;
    if (m_unflushed >= kFlushEvery)
        flushWriter(false);
}

void HttpFileBuffer::flushWriter(bool force)
{
    if (!m_writer)
        return;
    if (!force && m_unflushed < kFlushEvery)
        return;
    m_writer->flush();
    const qint64 size = m_writer->size();
    {
        QMutexLocker lock(&m_mutex);
        setAvailableLocked(size);
        m_unflushed = 0;
        wakeReaders();
    }
    if (m_device)
        m_device->notifyMoreData();
    emit progressed(size);
}

void HttpFileBuffer::onReplyFinished()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto err = reply->error();

    // Drain any remaining bytes before deciding success/fail.
    if (err == QNetworkReply::NoError && m_writer && !m_aborted) {
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty()) {
            m_writer->write(rest);
            m_unflushed += rest.size();
        }
        flushWriter(true);
    }

    reply->deleteLater();

    if (m_aborted)
        return;

    if (err == QNetworkReply::OperationCanceledError)
        return;

    // 200 after a Range request means server ignored Range — restart from 0 if we had a partial.
    if (err == QNetworkReply::NoError
        && (status == 200 || status == 206 || status == 0)) {
        if (finalizeRename())
            return;

        m_failed = true;
        m_error = QStringLiteral("Could not finalize cache file");
        emit failed(m_error);
        return;
    }

    // Network blip: retry with Range from last flushed offset.
    if (m_retries < kMaxRetries && !m_failed) {
        scheduleRetry();
        return;
    }

    m_failed = true;
    m_error = reply->errorString().isEmpty()
        ? QStringLiteral("Download failed (HTTP %1)").arg(status)
        : reply->errorString();
    qWarning("[podcast-cache] episode %lld failed: %s",
             static_cast<long long>(m_episodeId), qPrintable(m_error));
    {
        QMutexLocker lock(&m_mutex);
        wakeReaders(); // unblock any waiting readers
    }
    emit failed(m_error);
}

void HttpFileBuffer::scheduleRetry()
{
    ++m_retries;
    flushWriter(true);
    qint64 from = 0;
    {
        QMutexLocker lock(&m_mutex);
        from = m_available;
    }
    const int delay = kRetryBaseMs * m_retries;
    qInfo("[podcast-cache] retry %d for episode %lld in %d ms (from %lld)",
          m_retries, static_cast<long long>(m_episodeId), delay,
          static_cast<long long>(from));
    QTimer::singleShot(delay, this, [this, from]() {
        if (m_aborted || m_complete || m_failed)
            return;
        startRequest(from);
    });
}

bool HttpFileBuffer::finalizeRename()
{
    flushWriter(true);
    if (!m_writer)
        return false;

    const QString part = partPath();
    const QString dest = finishedPathForExt(m_ext);
    m_writer->close();

    // Remove any stale finished file with a different extension.
    for (const char *ext : {".mp3", ".m4a", ".aac", ".ogg", ".opus", ""}) {
        const QString other = (ext[0] == '\0')
            ? mediaDir() + QStringLiteral("/%1").arg(m_episodeId)
            : finishedPathForExt(QLatin1String(ext));
        if (other != dest && QFileInfo::exists(other))
            QFile::remove(other);
    }
    if (QFileInfo::exists(dest))
        QFile::remove(dest);

    if (!QFile::rename(part, dest)) {
        // Cross-filesystem fallback.
        if (!QFile::copy(part, dest)) {
            qWarning("[podcast-cache] rename failed for episode %lld",
                     static_cast<long long>(m_episodeId));
            // Keep .part usable; mark complete against part path by copying availability.
            QMutexLocker lock(&m_mutex);
            m_complete = true;
            wakeReaders();
            return false;
        }
        QFile::remove(part);
    }

    delete m_writer;
    m_writer = nullptr;

    {
        QMutexLocker lock(&m_mutex);
        m_complete = true;
        m_available = QFileInfo(dest).size();
        wakeReaders();
    }
    if (m_device)
        m_device->notifyMoreData();

    qInfo("[podcast-cache] complete episode %lld → %s (%lld bytes)",
          static_cast<long long>(m_episodeId), qPrintable(dest),
          static_cast<long long>(QFileInfo(dest).size()));
    emit completed(dest);
    return true;
}

void HttpFileBuffer::abort()
{
    m_aborted = true;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    flushWriter(true);
    if (m_writer) {
        m_writer->close();
        delete m_writer;
        m_writer = nullptr;
    }
    {
        QMutexLocker lock(&m_mutex);
        wakeReaders();
    }
}

void HttpFileBuffer::pruneCache(qint64 keepEpisodeId, qint64 keepNextId)
{
    const QString dirPath = mediaDir();
    QDir dir(dirPath);
    if (!dir.exists())
        return;

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 total = 0;
    QList<QFileInfo> finished;

    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        bool ok = false;
        // "123.part" or "123.mp3"
        const QString idPart = name.section(QLatin1Char('.'), 0, 0);
        const qint64 id = idPart.toLongLong(&ok);
        if (!ok)
            continue;

        if (name.endsWith(QStringLiteral(".part"))) {
            if (id != keepEpisodeId && id != keepNextId
                && (nowMs - fi.lastModified().toMSecsSinceEpoch()) > kPartMaxAgeMs) {
                qInfo("[podcast-cache] prune stale .part %s", qPrintable(fi.absoluteFilePath()));
                QFile::remove(fi.absoluteFilePath());
                continue;
            }
        }

        if (id == keepEpisodeId || id == keepNextId) {
            total += fi.size();
            continue;
        }

        if (!name.endsWith(QStringLiteral(".part")))
            finished.append(fi);
        total += fi.size();
    }

    if (total <= kMaxCacheBytes)
        return;

    std::sort(finished.begin(), finished.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });
    for (const QFileInfo &fi : finished) {
        if (total <= kMaxCacheBytes)
            break;
        qInfo("[podcast-cache] prune finished %s (cap)", qPrintable(fi.absoluteFilePath()));
        total -= fi.size();
        QFile::remove(fi.absoluteFilePath());
    }
}

// ---------------------------------------------------------------------------

GrowingMediaDevice::GrowingMediaDevice(HttpFileBuffer *buffer, QObject *parent)
    : QIODevice(parent)
    , m_buffer(buffer)
{
}

GrowingMediaDevice::~GrowingMediaDevice()
{
    GrowingMediaDevice::close();
}

bool GrowingMediaDevice::open(OpenMode mode)
{
    if (isOpen())
        return true;
    if (!(mode & QIODevice::ReadOnly) || !m_buffer)
        return false;

    const QString path = m_buffer->isComplete()
        ? m_buffer->existingFinishedPath()
        : m_buffer->partPath();
    // During download, prefer .part even if empty (just created).
    QString openPath = path;
    if (openPath.isEmpty() || !QFileInfo::exists(openPath))
        openPath = m_buffer->partPath();

    m_reader = new QFile(openPath, this);
    if (!m_reader->open(QIODevice::ReadOnly)) {
        delete m_reader;
        m_reader = nullptr;
        return false;
    }
    return QIODevice::open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

void GrowingMediaDevice::close()
{
    if (m_reader) {
        m_reader->close();
        delete m_reader;
        m_reader = nullptr;
    }
    QIODevice::close();
}

void GrowingMediaDevice::notifyMoreData()
{
    // Event-driven backends (in addition to blocking wait in readData).
    if (isOpen())
        emit readyRead();
}

qint64 GrowingMediaDevice::size() const
{
    if (!m_buffer)
        return 0;
    // CRITICAL: must NOT report only the bytes downloaded so far. FFmpeg/QMediaPlayer
    // treat QIODevice::size() as the full media length. With a ~96–256 KiB head buffer
    // on a 112 kb/s Joe Rogan MP3 that looks like a ~1–15s file; playback then hits
    // "EOF", stall-recovery rebinds, and the opening clip loops forever.
    QMutexLocker lock(&m_buffer->m_mutex);
    if (m_buffer->m_complete || m_buffer->m_failed || m_buffer->m_aborted)
        return m_buffer->m_available;
    if (m_buffer->m_contentLength > 0)
        return m_buffer->m_contentLength;
    // Headers not in yet, or chunked/unknown length: advertise a large upper bound so
    // the demuxer keeps reading. atEnd() stays false and readData blocks when caught up.
    constexpr qint64 kUnknownSizeFloor = 1LL << 30; // 1 GiB
    return qMax(m_buffer->m_available + (64LL << 20), kUnknownSizeFloor);
}

bool GrowingMediaDevice::atEnd() const
{
    if (!m_buffer)
        return true;
    QMutexLocker lock(&m_buffer->m_mutex);
    // Critical: never report EOF while download is still in progress.
    if (!m_buffer->m_complete && !m_buffer->m_failed && !m_buffer->m_aborted)
        return false;
    return pos() >= m_buffer->m_available;
}

bool GrowingMediaDevice::seek(qint64 pos)
{
    if (!m_reader || !m_buffer)
        return false;
    if (pos < 0)
        return false;
    // Allow seek only within bytes already on disk.
    if (pos > m_buffer->availableBytes())
        return false;
    if (!m_reader->seek(pos))
        return false;
    return QIODevice::seek(pos);
}

qint64 GrowingMediaDevice::bytesAvailable() const
{
    if (!m_buffer)
        return 0;
    const qint64 avail = m_buffer->availableBytes() - pos();
    return qMax<qint64>(0, avail) + QIODevice::bytesAvailable();
}

qint64 GrowingMediaDevice::readData(char *data, qint64 maxlen)
{
    if (!m_reader || !m_buffer || maxlen <= 0)
        return -1;

    for (;;) {
        qint64 available = 0;
        bool complete = false;
        bool failed = false;
        bool aborted = false;
        {
            QMutexLocker lock(&m_buffer->m_mutex);
            available = m_buffer->m_available;
            complete = m_buffer->m_complete;
            failed = m_buffer->m_failed;
            aborted = m_buffer->m_aborted;
            const qint64 at = pos();
            if (at < available) {
                const qint64 want = qMin(maxlen, available - at);
                lock.unlock();
                // Re-open reader if the file was renamed (.part → finished).
                if (complete) {
                    const QString finished = m_buffer->existingFinishedPath();
                    if (!finished.isEmpty() && m_reader->fileName() != finished) {
                        const qint64 keep = pos();
                        m_reader->close();
                        m_reader->setFileName(finished);
                        if (!m_reader->open(QIODevice::ReadOnly) || !m_reader->seek(keep))
                            return -1;
                    }
                }
                const qint64 n = m_reader->read(data, want);
                return n;
            }
            if (complete || failed || aborted) {
                // Genuine EOF (or hard failure).
                return 0;
            }
            // Playhead caught the writer — block until more bytes are flushed.
            // FFmpeg treats a 0 return as EOF, so we must wait instead of returning 0.
            m_buffer->m_cond.wait(&m_buffer->m_mutex, 500);
        }
        Q_UNUSED(available);
    }
}

qint64 GrowingMediaDevice::writeData(const char *, qint64)
{
    return -1;
}
