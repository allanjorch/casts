#include "backend.h"
#include "player.h"
#include "theme.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

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
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}

#include "main.moc"
