#include <mirrorfly/ai_island.hpp>

#include "ai_island_client.hpp"
#include "theme.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QTextStream>

namespace
{
    void write_diagnostic(const QString& message)
    {
        const QString path = qEnvironmentVariableIsSet("MIRRORFLY_AI_ISLAND_DIAGNOSTICS")
            ? qEnvironmentVariable("MIRRORFLY_AI_ISLAND_DIAGNOSTICS")
            : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                QStringLiteral("/ai-island.log");
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            QTextStream stream(&file);
            stream << message << '\n';
        }
    }
}

int mirrorfly::run_ai_island_interface(int argc, char* argv[])
{
    QQuickStyle::setStyle("Basic");
    QGuiApplication application(argc, argv);
    if (application.arguments().size() != 3)
        return 1;
    mirrorfly::AiIslandClient client(application.arguments().at(1), application.arguments().at(2));
    const auto theme = mirrorfly::load_theme(QCoreApplication::applicationDirPath());
    QVariantList screens;
    for (const QScreen* screen : QGuiApplication::screens())
    {
        const QRect geometry = screen->geometry();
        const QRect available = screen->availableGeometry();
        screens.append(QVariantMap{{"x", geometry.x()}, {"y", geometry.y()}, {"width", geometry.width()},
            {"height", geometry.height()}, {"availableX", available.x()}, {"availableY", available.y()},
            {"availableWidth", available.width()}, {"availableHeight", available.height()}});
    }
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &application, [](const QList<QQmlError>& warnings)
    {
        for (const QQmlError& warning : warnings)
            write_diagnostic(warning.toString());
    });
    engine.setInitialProperties(
        {{"client", QVariant::fromValue(&client)}, {"theme", theme.values}, {"screens", screens}});
    engine.load(QUrl(QStringLiteral("qrc:/mirrorfly/ui/AiIslandWindow.qml")));
    if (engine.rootObjects().isEmpty())
    {
        write_diagnostic(QStringLiteral("AI island QML root failed to load"));
        return 2;
    }
    client.markUiReady();
    client.attachWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst()));
    return application.exec();
}
