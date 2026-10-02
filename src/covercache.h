#pragma once

#include <QCache>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QString>

// Stable image:// URL for a local cover file. Empty when path is missing.
QString localCoverSource(const QString &path);

// Decoded episode/show covers. Qt Quick's own pixmap cache only retains about
// 2 MB of *unreferenced* images, so opening another show evicts the previous
// one and the list redecodes top to bottom. This cache keeps the pixels.
class CoverImageProvider : public QQuickImageProvider {
public:
    CoverImageProvider();

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QCache<QString, QPixmap> m_cache;
};
