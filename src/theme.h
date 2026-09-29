#pragma once

#include <QDBusVariant>
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QVariant>
#include <functional>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY changed)
    Q_PROPERTY(qreal textScale READ textScale NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QColor accent() const { return m_accent; }
    QColor selection() const { return m_selection; }
    QColor muted() const { return m_muted; }
    bool darkMode() const { return m_darkMode; }
    qreal textScale() const { return m_textScale; }

signals:
    void changed();

private slots:
    void handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                    const QDBusVariant &value);

private:
    void loadColors();
    void watchColors();
    void requestPortalSetting(const QString &nameSpace, const QString &key,
                              const std::function<void(const QVariant &)> &handler);
    void requestTextScale();
    void applyTextScale(qreal scale);

    QColor m_background = QColor("#101010");
    QColor m_foreground = QColor("#eeeeee");
    QColor m_accent = QColor("#5584aa");
    QColor m_selection = QColor("#186a9a");
    QColor m_muted = QColor("#8a8a8a");
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QFileSystemWatcher m_watcher;
};
