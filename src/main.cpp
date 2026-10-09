#include "backend.h"
#include "library.h"
#include <cstdio>
#include "covercache.h"
#include "explore.h"
#include "netaccess.h"
#include "player.h"
#include "theme.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QVariant>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

class WindowInputFilter : public QObject {
public:
    QObject *window = nullptr;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!window || !eventIsOurs(watched))
            return false;

        // Page history: mouse side buttons (XButton1/2) anywhere in the window,
        // including over lists, text, the player bar and popups. The app-wide
        // filter sees them before any child MouseArea can accept them.
        if (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::MouseButtonRelease) {
            const auto *mouse = static_cast<const QMouseEvent *>(event);
            const Qt::MouseButton button = mouse->button();
            if (button != Qt::BackButton && button != Qt::ForwardButton)
                return false;
            // Act once per physical press; swallow the rest so nothing else reacts.
            if (event->type() != QEvent::MouseButtonRelease)
                QMetaObject::invokeMethod(window, button == Qt::BackButton ? "back" : "forward",
                                          Qt::DirectConnection);
            return true;
        }

        // Dedicated Back / Forward keys (multimedia keyboards, some mice).
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<const QKeyEvent *>(event);
            if (key->key() == Qt::Key_Back || key->key() == Qt::Key_Forward) {
                if (!key->isAutoRepeat())
                    QMetaObject::invokeMethod(window, key->key() == Qt::Key_Back ? "back" : "forward",
                                              Qt::DirectConnection);
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
    Q_CLASSINFO("D-Bus Interface", "com.github.allanjorch.omaear.Ui")
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
    startupTimer().start();
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.contains(QStringLiteral("--version"))) {
        printf("omaear %s\n", OMAEAR_VERSION);
        return 0;
    }
    if (args.contains(QStringLiteral("--player")))
        return runPlayer(argc, argv);
    if (args.contains(QStringLiteral("--self-test")))
        return runSelfTest(argc, argv);

    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("allanjorch"));
    app.setApplicationName(QStringLiteral("omaear"));
    app.setApplicationDisplayName(QStringLiteral("OmaEar"));
    app.setDesktopFileName(QStringLiteral("com.github.allanjorch.omaear"));
    QFont uiFont(QStringLiteral("monospace"));
    uiFont.setStyleHint(QFont::Monospace);
    uiFont.setWeight(QFont::Normal);
    app.setFont(uiFont);
    QQuickStyle::setStyle(QStringLiteral("Material"));

    auto bus = QDBusConnection::sessionBus();
    const QString uiService = QStringLiteral("com.github.allanjorch.omaear");
    if (bus.interface()->isServiceRegistered(uiService)) {
        QDBusInterface ui(uiService, QStringLiteral("/com/github/allanjorch/omaear/Ui"),
                          QStringLiteral("com.github.allanjorch.omaear.Ui"), bus);
        ui.call(QStringLiteral("Raise"));
        return 0;
    }
    // Pre-rename build still running: it has the library open at the old path.
    if (bus.interface()->isServiceRegistered(QStringLiteral("com.github.allanjorch.podcast"))
        || bus.interface()->isServiceRegistered(QStringLiteral("com.github.allanjorch.podcast.Player"))) {
        const char *msg = "OmaEar: the old Podcasts app is still running. Quit it first (pkill -f podcast), then start OmaEar.";
        fprintf(stderr, "%s\n", msg);
        QProcess::startDetached(QStringLiteral("notify-send"), {QStringLiteral("OmaEar"),
            QStringLiteral("Quit the old Podcasts app first (pkill -f podcast), then start OmaEar.")});
        return 1;
    }
    {
        QString moved;
        Library::migrateLegacyLocations(&moved);
        if (!moved.isEmpty())
            qInfo("[omaear] rename migration:\n%s", qPrintable(moved));
    }

    Library library;
    if (!library.isOpen())
        return 1;
    {
        // Shelf covers into memory while QML loads, so they paint with the first frame.
        QStringList paths;
        for (const ShowRow &row : library.shows())
            if (!row.imagePath.isEmpty())
                paths << row.imagePath;
        prewarmCovers(paths, {kCoverTileSide, kCoverRowSide});
    }

    Theme theme;
    Backend backend(library);
    Explore explore(library, backend);
    UiBridge bridge(&backend);
    bus.registerObject(QStringLiteral("/com/github/allanjorch/omaear/Ui"), &bridge,
                       QDBusConnection::ExportAllSlots);
    bus.registerService(uiService);


    CoverCache coverCache;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(new AppNetworkAccessManagerFactory);
    engine.addImageProvider(QStringLiteral("covers"), new CoverImageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("coverCache"), &coverCache);
    engine.rootContext()->setContextProperty(QStringLiteral("explore"), &explore);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (startupTiming()) {
        fprintf(stderr, "timing: qml loaded %lld ms\n", qint64(startupTimer().elapsed()));
        if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
            auto *conn = new QMetaObject::Connection;
            *conn = QObject::connect(win, &QQuickWindow::frameSwapped, win, [conn]() {
                fprintf(stderr, "timing: first frame %lld ms\n", qint64(startupTimer().elapsed()));
                QObject::disconnect(*conn);
                delete conn;
            });
        }
        // Harness: grow the window (bigger tiles) to check resizes never reload covers.
        if (qEnvironmentVariableIntValue("OMAEAR_TIMING_RESIZE") == 1) {
            if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
                QTimer::singleShot(1500, win, [win]() {
                    fprintf(stderr, "timing: resize %lld ms\n", qint64(startupTimer().elapsed()));
                    win->resize(2560, 1440);
                    QTimer::singleShot(500, win, [win]() { fprintf(stderr, "timing: window now %dx%d\n", win->width(), win->height()); });
                });
            }
        }
        const int quitMs = qEnvironmentVariableIntValue("OMAEAR_TIMING_QUIT_MS");
        if (quitMs > 0)
            QTimer::singleShot(quitMs, &app, &QCoreApplication::quit);
    }
    WindowInputFilter inputFilter;
    inputFilter.window = engine.rootObjects().constFirst();
    app.installEventFilter(&inputFilter);
    return app.exec();
}

#include "main.moc"
