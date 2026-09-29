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
    Q_PROPERTY(QColor dim READ dim NOTIFY changed)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY changed)
    Q_PROPERTY(qreal textScale READ textScale NOTIFY changed)
    Q_PROPERTY(int caption READ caption NOTIFY changed)
    Q_PROPERTY(int bodySmall READ bodySmall NOTIFY changed)
    Q_PROPERTY(int body READ body NOTIFY changed)
    Q_PROPERTY(int subtitle READ subtitle NOTIFY changed)
    Q_PROPERTY(int titleSize READ titleSize NOTIFY changed)
    Q_PROPERTY(int heading READ heading NOTIFY changed)
    Q_PROPERTY(int display READ display NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QColor accent() const { return m_accent; }
    QColor selection() const { return m_selection; }
    QColor muted() const { return m_muted; }
    QColor dim() const { return m_dim; }
    bool darkMode() const { return m_darkMode; }
    qreal textScale() const { return m_textScale; }
    int caption() const { return m_caption; }
    int bodySmall() const { return m_bodySmall; }
    int body() const { return m_body; }
    int subtitle() const { return m_subtitle; }
    int titleSize() const { return m_titleSize; }
    int heading() const { return m_heading; }
    int display() const { return m_display; }

signals:
    void changed();

private slots:
    void handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                    const QDBusVariant &value);

private:
    void loadColors();
    void loadType();
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
    QColor m_dim = QColor("#9a9a9a");
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    int m_caption = 10;
    int m_bodySmall = 11;
    int m_body = 12;
    int m_subtitle = 13;
    int m_titleSize = 14;
    int m_heading = 16;
    int m_display = 24;
    QFileSystemWatcher m_watcher;
};
