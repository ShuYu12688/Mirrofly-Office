#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUuid>

#include <functional>
#include <iostream>
#include <memory>

namespace
{
    bool wait_until(const std::function<bool()>& condition, int timeout_ms)
    {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < timeout_ms)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return condition();
    }

    int run_tests(int argc, char* argv[])
    {
        QCoreApplication application(argc, argv);
        QLocalServer server;
        const QString name =
            QStringLiteral("mirrorfly-ai-launch-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!server.listen(name))
            return 1;

        QProcess process;
        QTemporaryDir diagnostics;
        const QString diagnostic_path = diagnostics.filePath(QStringLiteral("island.log"));
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QSG_RHI_BACKEND", "software");
        environment.insert("MIRRORFLY_AI_ISLAND_DIAGNOSTICS", diagnostic_path);
        process.setProcessEnvironment(environment);
        const QString executable = application.arguments().size() > 1
            ? application.arguments().at(1)
            : QStringLiteral(MIRRORFLY_AI_ISLAND_EXECUTABLE);
        process.start(executable, {name, QStringLiteral("test-island-token")});
        const bool connected = wait_until(
                                   [&server, &process]()
        {
            return server.hasPendingConnections() || process.state() == QProcess::NotRunning;
        }, 5000) &&
            server.hasPendingConnections();
        if (!connected)
        {
            QFile diagnostic_file(diagnostic_path);
            diagnostic_file.open(QIODevice::ReadOnly);
            std::cerr << "Hidden AI island failed to connect: "
                      << "exit_code=" << process.exitCode()
                      << " process_error=" << process.errorString().toStdString()
                      << " stderr=" << process.readAllStandardError().toStdString()
                      << " diagnostic=" << diagnostic_file.readAll().toStdString() << '\n';
            process.kill();
            process.waitForFinished(1000);
            return 1;
        }
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        if (!wait_until([&socket]()
        {
            return socket->canReadLine();
        }, 3000))
        {
            std::cerr << "Hidden AI island did not authenticate: "
                      << process.readAllStandardError().toStdString() << '\n';
            process.kill();
            process.waitForFinished(1000);
            return 1;
        }
        const QJsonObject hello = QJsonDocument::fromJson(socket->readLine()).object();
        const bool authenticated =
            hello.value("type") == "hello" && hello.value("token") == "test-island-token";
        socket->write(QJsonDocument(QJsonObject{{"type", "state"}, {"configured", true}, {"location", "首页"},
                                        {"trace", QJsonArray{}}})
                          .toJson(QJsonDocument::Compact) +
            '\n');
        socket->write(QJsonDocument(QJsonObject{{"type", "open"}}).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
        const bool stays_alive = wait_until([&process]()
        {
            return process.state() == QProcess::NotRunning;
        }, 250) == false;
        QFile diagnostic_file(diagnostic_path);
        diagnostic_file.open(QIODevice::ReadOnly);
        const QByteArray diagnostic_text = diagnostic_file.readAll();
        socket->disconnectFromServer();
        const bool exits_cleanly = wait_until(
                                       [&process]()
        {
            return process.state() == QProcess::NotRunning;
        }, 3000) &&
            process.exitCode() == 0;
        if (!authenticated || !stays_alive || !exits_cleanly || !diagnostic_text.isEmpty())
        {
            std::cerr << "authenticated=" << authenticated << " stays_alive=" << stays_alive
                      << " exits_cleanly=" << exits_cleanly << " exit_code=" << process.exitCode()
                      << " stderr=" << process.readAllStandardError().toStdString()
                      << " diagnostic=" << diagnostic_text.toStdString() << '\n';
        }
        return authenticated && stays_alive && exits_cleanly && diagnostic_text.isEmpty() ? 0 : 1;
    }
}

int main(int argc, char* argv[])
{
    return run_tests(argc, argv);
}
