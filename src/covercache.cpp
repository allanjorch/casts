#include "covercache.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QUrl>

namespace {
QMutex cacheMutex;

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

QString localCoverSource(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    return QStringLiteral("image://covers/")
        + QString::fromLatin1(QUrl::toPercentEncoding(path));
}

CoverImageProvider::CoverImageProvider()
    : QQuickImageProvider(Pixmap)
{
    // Several "Show all" libraries at list-cover resolution.
    m_cache.setMaxCost(96 * 1024 * 1024);
}

QPixmap CoverImageProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString path = absolutePath(id);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};

    const int requestW = requestedSize.width() > 0 ? requestedSize.width() : 0;
    const int requestH = requestedSize.height() > 0 ? requestedSize.height() : 0;
    // mtime so a rewritten file (force-reload) is not served from the old pixmap.
    const qint64 stamp = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    const QString key = path + QLatin1Char('|') + QString::number(stamp)
        + QLatin1Char('|') + QString::number(requestW)
        + QLatin1Char('x') + QString::number(requestH);

    QMutexLocker lock(&cacheMutex);
    if (QPixmap *cached = m_cache.object(key)) {
        if (size)
            *size = cached->size();
        return *cached;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (requestW > 0 && requestH > 0 && original.isValid() && !original.isEmpty()) {
        QSize scaled = original.scaled(requestW, requestH, Qt::KeepAspectRatio);
        if (!scaled.isEmpty())
            reader.setScaledSize(scaled);
    }
    const QImage image = reader.read();
    if (image.isNull())
        return {};

    QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.isNull())
        return {};
    if (size)
        *size = pixmap.size();
    const int cost = qMax(1, pixmap.width() * pixmap.height() * qMax(1, pixmap.depth() / 8));
    m_cache.insert(key, new QPixmap(pixmap), cost);
    return pixmap;
}
