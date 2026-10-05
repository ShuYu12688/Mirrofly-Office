#include "desktop_resident.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>

int run_desktop_resident_tests(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    if (application.arguments().size() == 4 && application.arguments().at(1) == "--forward")
    {
        mirrorfly::DesktopResident secondary(application.arguments().at(2));
        return secondary.prepare(application.arguments().at(3)) ==
                mirrorfly::DesktopResident::Startup::Forwarded
            ? 0
            : 1;
    }
    QTemporaryDir directory;
    mirrorfly::DesktopResident primary(directory.path());
    if (primary.prepare() != mirrorfly::DesktopResident::Startup::Primary)
        return 1;
    QQuickWindow window;
    primary.attach(&window, false);
    window.show();
    primary.hideMain();
    if (window.isVisible())
        return 1;
    primary.restoreMain();
    if (!window.isVisible())
        return 1;
    QString forwarded;
    QObject::connect(&primary, &mirrorfly::DesktopResident::fileRequested, &application,
        [&forwarded](const QString& path)
    {
        forwarded = path;
    });
    primary.hideMain();
    QProcess secondary;
    secondary.start(
        QCoreApplication::applicationFilePath(), {"--forward", directory.path(), "document.pptx"});
    QElapsedTimer timer;
    timer.start();
    while ((forwarded.isEmpty() || secondary.state() != QProcess::NotRunning) && timer.elapsed() < 4000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (forwarded != "document.pptx" || !window.isVisible() || secondary.state() != QProcess::NotRunning ||
        secondary.exitCode() != 0)
    {
        std::cerr << "forwarding must restore the existing window and pass the file request: "
                  << secondary.readAllStandardError().toStdString() << '\n';
        secondary.kill();
        secondary.waitForFinished(1000);
        return 1;
    }
    primary.completeExit();
    QTimer::singleShot(1000, &application, [&application]()
    {
        application.exit(1);
    });
    return application.exec();
}

int main(int argc, char* argv[])
{
    return run_desktop_resident_tests(argc, argv);
}
