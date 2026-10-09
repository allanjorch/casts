#include "covercache.h"
#include <cstdio>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QCache>
#include <QAtomicInt>
#include <QThreadPool>
#include <QUrl>

namespace {
QMutex cacheMutex;

// On-disk thumbnails at the requested display size. Originals are up to
// 3000x3000, so decoding them on every launch made covers fill in one by one;
// a ~300px thumb decodes in about a millisecond.
QString thumbDir()
{
    static const QString dir = [] {
        const QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/thumbs");
        QDir().mkpath(d);
        return d;
    }();
    return dir;
}

QString thumbPath(const QString &key)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return thumbDir() + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".jpg");
}

QString absolutePath(const QString &id)
{
    if (id.isEmpty())
        return {};
    // image:// id is the percent-encoded absolute path. QQuick strips one
    // leading slash, so the encoded "%2F" has to survive that and be decoded here.
    QString path = QUrl::fromPercentEncoding(id.toUtf8());
    if (!path.startsWith(QLatin1Char('/')) && QFileInfo::exists(QLatin1Char('/') + path))
        path.prepend(QLatin1Char('/'));
    return path;
}
}

QElapsedTimer &startupTimer()
{
    static QElapsedTimer timer;
    return timer;
}

bool startupTiming()
{
    static const bool on = qEnvironmentVariableIntValue("PODCAST_TIMING") == 1;
    return on;
}

QString localCoverSource(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    return QStringLiteral("image://covers/")
        + QString::fromLatin1(QUrl::toPercentEncoding(path));
}

namespace {
QCache<QString, QImage> &memoryCache()
{
    static QCache<QString, QImage> cache(96 * 1024 * 1024); // several libraries of tiles
    return cache;
}

QString cacheKey(const QString &path, int w, int h)
{
    // mtime so a rewritten file (force-reload) is not served from the old image.
    const qint64 stamp = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    return path + QLatin1Char('|') + QString::number(stamp) + QLatin1Char('|')
        + QString::number(w) + QLatin1Char('x') + QString::number(h);
}

// Thread-safe; decodes outside the lock so the prewarm and the provider overlap.
QImage loadCover(const QString &path, int requestW, int requestH, bool *hit = nullptr)
{
    if (hit)
        *hit = false;
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    const QString key = cacheKey(path, requestW, requestH);
    {
        QMutexLocker lock(&cacheMutex);
        if (QImage *cached = memoryCache().object(key)) {
            if (hit)
                *hit = true;
            return *cached;
        }
    }
    const bool sized = requestW > 0 && requestH > 0;
    const QString thumb = sized ? thumbPath(key) : QString();
    QImage image;
    if (sized && QFileInfo::exists(thumb))
        image = QImage(thumb);
    if (image.isNull()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize original = reader.size();
        bool shrunk = false;
        if (sized && original.isValid() && !original.isEmpty()) {
            QSize scaled = original.scaled(requestW, requestH, Qt::KeepAspectRatio);
            if (!scaled.isEmpty() && scaled.width() < original.width()) {
                reader.setScaledSize(scaled);
                shrunk = true;
            }
        }
        image = reader.read();
        if (image.isNull())
            return {};
        if (shrunk && !image.hasAlphaChannel()) {
            QSaveFile out(thumb);
            if (out.open(QIODevice::WriteOnly) && image.save(&out, "JPG", 90))
                out.commit();
        }
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int cost = qMax(1, int(image.sizeInBytes()));
    QMutexLocker lock(&cacheMutex);
    memoryCache().insert(key, new QImage(image), cost);
    return image;
}
}

void prewarmCovers(const QStringList &paths, const QList<int> &sides)
{
    if (startupTiming())
        fprintf(stderr, "timing: prewarm start %lld ms\n", qint64(startupTimer().elapsed()));
    // One task per cover on the global pool (all cores), tile size first.
    auto *left = new QAtomicInt(int(paths.size() * sides.size()));
    for (int side : sides) {
        for (const QString &path : paths) {
            QThreadPool::globalInstance()->start([path, side, left]() {
                loadCover(path, side, side);
                if (left->fetchAndAddOrdered(-1) == 1) {
                    if (startupTiming())
                        fprintf(stderr, "timing: prewarm done %lld ms\n", qint64(startupTimer().elapsed()));
                    delete left;
                }
            }, side == sides.constFirst() ? 1 : 0);
        }
    }
}

bool CoverCache::ready(const QString &source, int side) const
{
    static const QString prefix = QStringLiteral("image://covers/");
    if (!source.startsWith(prefix))
        return false;
    const QString path = absolutePath(source.mid(prefix.size()));
    if (path.isEmpty())
        return false;
    const QString key = cacheKey(path, side, side);
    QMutexLocker lock(&cacheMutex);
    return memoryCache().contains(key);
}

CoverImageProvider::CoverImageProvider()
    : QQuickImageProvider(Image)
{
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // QML asks for side / devicePixelRatio, which Qt scales back up with rounding;
    // snap to the exact bucket so every request shares the prewarmed entry.
    auto snap = [](int v) {
        for (int side : {kCoverTileSide, kCoverRowSide})
            if (qAbs(v - side) <= 4)
                return side;
        return v > 0 ? v : 0;
    };
    const int w = snap(requestedSize.width());
    const int h = snap(requestedSize.height());
    bool hit = false;
    const QImage image = loadCover(absolutePath(id), w, h, &hit);
    if (size)
        *size = image.size();
    if (startupTiming())
        fprintf(stderr, "timing: cover %lld ms %dx%d %s\n", qint64(startupTimer().elapsed()),
                image.width(), image.height(), hit ? "memory" : "decoded");
    return image;
}
