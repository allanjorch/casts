#include "theme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QVariant>

namespace {
QVariant unwrap(QVariant value)
{
    while (value.canConvert<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

QColor fallbackMix(const QColor &background, const QColor &foreground)
{
    return QColor::fromRgbF(
        background.redF() + (foreground.redF() - background.redF()) * 0.45,
        background.greenF() + (foreground.greenF() - background.greenF()) * 0.45,
        background.blueF() + (foreground.blueF() - background.blueF()) * 0.45);
}
}

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    loadColors();
    watchColors();
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadColors();
        watchColors();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadColors();
        watchColors();
    });

    QDBusConnection::sessionBus().connect(
        QString(),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"),
        QStringLiteral("SettingChanged"),
        this,
        SLOT(handlePortalSettingChanged(QString,QString,QDBusVariant)));
    requestTextScale();
}

void Theme::loadColors()
{
    bool dark = true;
    QColor background(QStringLiteral("#101010"));
    QColor foreground(QStringLiteral("#eeeeee"));
    QColor accent(QStringLiteral("#5584aa"));
    QColor selection(QStringLiteral("#186a9a"));
    QColor muted;
    bool haveMuted = false;
    QString mode;

    const QString path = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;
            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                    || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                mode = value;
            else if (key == QStringLiteral("background"))
                background = QColor(value);
            else if (key == QStringLiteral("foreground"))
                foreground = QColor(value);
            else if (key == QStringLiteral("accent"))
                accent = QColor(value);
            else if (key == QStringLiteral("selection"))
                selection = QColor(value);
            else if (key == QStringLiteral("muted")) {
                muted = QColor(value);
                haveMuted = muted.isValid();
            }
        }
    }

    if (mode == QStringLiteral("light"))
        dark = false;
    else if (mode == QStringLiteral("dark"))
        dark = true;
    else if (background.isValid())
        dark = 0.299 * background.redF() + 0.587 * background.greenF() + 0.114 * background.blueF() < 0.5;

    if (!dark && !file.exists()) {
        background = QColor(QStringLiteral("#ffffff"));
        foreground = QColor(QStringLiteral("#222324"));
        accent = QColor(QStringLiteral("#2077b2"));
        selection = QColor(QStringLiteral("#2077b2"));
    }
    if (!haveMuted)
        muted = fallbackMix(background, foreground);

    const bool changedColors = background != m_background || foreground != m_foreground
        || accent != m_accent || selection != m_selection || muted != m_muted || dark != m_darkMode;
    m_background = background;
    m_foreground = foreground;
    m_accent = accent;
    m_selection = selection;
    m_muted = muted;
    m_darkMode = dark;
    if (changedColors)
        emit changed();
}

void Theme::watchColors()
{
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    const QString current = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
    const QString theme = current + QStringLiteral("/theme");
    const QString colors = theme + QStringLiteral("/colors.toml");
    if (QDir(current).exists())
        m_watcher.addPath(current);
    if (QDir(theme).exists())
        m_watcher.addPath(theme);
    if (QFile::exists(colors))
        m_watcher.addPath(colors);
}

void Theme::requestPortalSetting(const QString &nameSpace, const QString &key,
                                 const std::function<void(const QVariant &)> &handler)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    auto request = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"),
        QStringLiteral("Read"));
    request << nameSpace << key;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [handler](QDBusPendingCallWatcher *finished) {
                const QDBusPendingReply<QDBusVariant> reply(*finished);
                finished->deleteLater();
                if (reply.isValid())
                    handler(reply.value().variant());
            });
}

void Theme::requestTextScale()
{
    requestPortalSetting(QStringLiteral("org.gnome.desktop.interface"),
                         QStringLiteral("text-scaling-factor"),
                         [this](const QVariant &value) { applyTextScale(unwrap(value).toDouble()); });
}

void Theme::applyTextScale(qreal scale)
{
    if (scale <= 0)
        return;
    scale = qBound(0.5, scale, 3.0);
    if (qFuzzyCompare(m_textScale, scale))
        return;
    m_textScale = scale;
    emit changed();
}

void Theme::handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                       const QDBusVariant &value)
{
    if (nameSpace == QStringLiteral("org.gnome.desktop.interface")
        && key == QStringLiteral("text-scaling-factor"))
        applyTextScale(unwrap(value.variant()).toDouble());
}
