#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QQuickImageProvider>
#include <QString>

#include <QElapsedTimer>
// Startup timing (OMAEAR_TIMING=1): started first thing in main().
QElapsedTimer &startupTimer();
bool startupTiming();

// Stable image:// URL for a local cover file. Empty when path is missing.
QString localCoverSource(const QString &path);

// Fixed decode sides (px). Tiles use one constant sourceSize so window resizes
// never reload; the GPU scales (smooth + mipmap).
constexpr int kCoverTileSide = 512;
constexpr int kCoverRowSide = 256;

// Background thread: decode these covers into the memory cache (startup).
void prewarmCovers(const QStringList &paths, const QList<int> &sides);

// QML: true when source at side x side is already decoded in memory, so a
// synchronous Image paints it with the delegate at zero cost.
class CoverCache : public QObject {
    Q_OBJECT
    Q_PROPERTY(int tileSide READ tileSide CONSTANT)
    Q_PROPERTY(int rowSide READ rowSide CONSTANT)
public:
    using QObject::QObject;
    int tileSide() const { return kCoverTileSide; }
    int rowSide() const { return kCoverRowSide; }
    Q_INVOKABLE bool ready(const QString &source, int side) const;
};

// Decoded episode/show covers, memory-cached (Qt Quick's own cache keeps only
// ~2 MB of unreferenced images) and backed by on-disk thumbs.
class CoverImageProvider : public QQuickImageProvider {
public:
    CoverImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
