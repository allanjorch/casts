#include "backend.h"
#include "covercache.h"
#include "player.h"
#include "theme.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QVariant>
#include <QMouseEvent>
#include <QWheelEvent>

class WindowInputFilter : public QObject {
public:
    QObject *window = nullptr;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!window || !eventIsOurs(watched))
            return false;

        if (event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = static_cast<const QMouseEvent *>(event);
            if (mouse->button() == Qt::BackButton) {
                QMetaObject::invokeMethod(window, "navigateBack", Qt::DirectConnection);
                return true;
            }
            if (mouse->button() == Qt::ForwardButton) {
                QMetaObject::invokeMethod(window, "navigateForward", Qt::DirectConnection);
                return true;
            }
            return false;
        }

        if (event->type() != QEvent::Wheel)
            return false;
        const auto *wheel = static_cast<const QWheelEvent *>(event);
        if (!(wheel->modifiers() & Qt::ControlModifier))
            return false;
        int delta = wheel->angleDelta().y();
        if (delta == 0) {
            const int pixel = wheel->pixelDelta().y();
            if (pixel > 0)
                delta = 120;
            else if (pixel < 0)
                delta = -120;
        }
        if (delta == 0)
            return false;
        QVariant consumed;
        const QVariant deltaArg(delta);
        const bool called = QMetaObject::invokeMethod(
            window, "zoomFromWheel", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, consumed), Q_ARG(QVariant, deltaArg));
        return called && consumed.toBool();
    }

private:
    bool eventIsOurs(QObject *watched) const
    {
        if (watched == window)
            return true;
        if (auto *item = qobject_cast<QQuickItem *>(watched))
            return item->window() == window;
        if (auto *view = qobject_cast<QWindow *>(watched))
            return view == window;
        return false;
    }
};

class UiBridge : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.github.allanjorch.podcast.Ui")
public:
    explicit UiBridge(Backend *backend, QObject *parent = nullptr)
        : QObject(parent)
        , m_backend(backend)
    {
    }

public slots:
    void Raise() { m_backend->raiseWindow(); }

private:
    Backend *m_backend;
};

int runSelfTest(int argc, char **argv);

int main(int argc, char **argv)
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.contains(QStringLiteral("--player")))
        return runPlayer(argc, argv);
    if (args.contains(QStringLiteral("--self-test")))
        return runSelfTest(argc, argv);

    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("allanjorch"));
    app.setApplicationName(QStringLiteral("podcast"));
    app.setApplicationDisplayName(QStringLiteral("Podcasts"));
    app.setDesktopFileName(QStringLiteral("com.github.allanjorch.podcast"));
    QFont uiFont(QStringLiteral("monospace"));
    uiFont.setStyleHint(QFont::Monospace);
    uiFont.setWeight(QFont::Normal);
    app.setFont(uiFont);
    QQuickStyle::setStyle(QStringLiteral("Material"));

    auto bus = QDBusConnection::sessionBus();
    const QString uiService = QStringLiteral("com.github.allanjorch.podcast");
    if (bus.interface()->isServiceRegistered(uiService)) {
        QDBusInterface ui(uiService, QStringLiteral("/com/github/allanjorch/podcast/Ui"),
                          QStringLiteral("com.github.allanjorch.podcast.Ui"), bus);
        ui.call(QStringLiteral("Raise"));
        return 0;
    }

    Library library;
    if (!library.isOpen())
        return 1;

    Theme theme;
    Backend backend(library);
    UiBridge bridge(&backend);
    bus.registerObject(QStringLiteral("/com/github/allanjorch/podcast/Ui"), &bridge,
                       QDBusConnection::ExportAllSlots);
    bus.registerService(uiService);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("covers"), new CoverImageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    WindowInputFilter inputFilter;
    inputFilter.window = engine.rootObjects().constFirst();
    app.installEventFilter(&inputFilter);
    return app.exec();
}

#include "main.moc"
