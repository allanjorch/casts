#include "theme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QTextStream>
#include <QVariant>
#include <QtMath>

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
    loadType();
    watchColors();
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadColors();
        loadType();
        watchColors();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadColors();
        loadType();
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
    QColor blue;
    bool haveBlue = false;
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
            else if (key == QStringLiteral("blue")) {
                blue = QColor(value);
                haveBlue = blue.isValid();
            } else if (key == QStringLiteral("muted")) {
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
    if (!haveBlue)
        blue = accent;

    const QColor dim = foreground.darker(155);
    const bool changedColors = background != m_background || foreground != m_foreground
        || accent != m_accent || blue != m_blue || selection != m_selection || muted != m_muted
        || dim != m_dim || dark != m_darkMode;
    m_background = background;
    m_foreground = foreground;
    m_accent = accent;
    m_blue = blue;
    m_selection = selection;
    m_muted = muted;
    m_dim = dim;
    m_darkMode = dark;
    if (changedColors)
        emit changed();
}

void Theme::loadType()
{
    int base = 12;
    QHash<QString, int> over;
    const auto read = [&base, &over](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        bool inFont = false;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                inFont = line == QStringLiteral("[font]");
                continue;
            }
            if (!inFont)
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;
            const QString key = line.left(equals).trimmed();
            const int value = line.mid(equals + 1).trimmed().toInt();
            if (value <= 0)
                continue;
            if (key == QStringLiteral("base-size"))
                base = value;
            else
                over.insert(key, value);
        }
    };
    read(QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme/shell.toml"));
    read(QDir::homePath() + QStringLiteral("/.config/omarchy/shell.toml"));

    const auto token = [&over, base](const QString &key, double mult) {
        const int explicitValue = over.value(key, 0);
        if (explicitValue > 0)
            return explicitValue;
        return std::max(1, qRound(base * mult));
    };
    // Bump the two smallest tokens +4 so caption/bodySmall stay readable when
    // base-size is small (senior / high display scale). Body +2 for episode
    // notes and general readability. Sticks even if shell.toml only sets
    // base-size; title/heading multipliers unchanged.
    const int caption = token(QStringLiteral("caption"), 0.833) + 4;
    const int bodySmall = token(QStringLiteral("body-small"), 0.917) + 4;
    const int body = token(QStringLiteral("body"), 1.0) + 2;
    const int subtitle = token(QStringLiteral("subtitle"), 1.083);
    const int titleSize = token(QStringLiteral("title"), 1.167);
    const int heading = token(QStringLiteral("heading"), 1.333);
    const int display = token(QStringLiteral("display"), 2.0);
    if (caption == m_caption && bodySmall == m_bodySmall && body == m_body && subtitle == m_subtitle
        && titleSize == m_titleSize && heading == m_heading && display == m_display)
        return;
    m_caption = caption;
    m_bodySmall = bodySmall;
    m_body = body;
    m_subtitle = subtitle;
    m_titleSize = titleSize;
    m_heading = heading;
    m_display = display;
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
    const QString themeShell = theme + QStringLiteral("/shell.toml");
    const QString userConfig = QDir::homePath() + QStringLiteral("/.config/omarchy");
    const QString userShell = userConfig + QStringLiteral("/shell.toml");
    if (QDir(current).exists())
        m_watcher.addPath(current);
    if (QDir(theme).exists())
        m_watcher.addPath(theme);
    if (QFile::exists(colors))
        m_watcher.addPath(colors);
    if (QFile::exists(themeShell))
        m_watcher.addPath(themeShell);
    if (QDir(userConfig).exists())
        m_watcher.addPath(userConfig);
    if (QFile::exists(userShell))
        m_watcher.addPath(userShell);
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
