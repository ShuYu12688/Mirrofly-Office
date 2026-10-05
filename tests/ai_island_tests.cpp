#include "ai_island_client.hpp"
#include "ai_island_trace.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUuid>

#include <functional>
#include <iostream>
#include <memory>

namespace
{
    bool wait_until(const std::function<bool()>& condition)
    {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < 2000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return condition();
    }

    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }
}

namespace
{
    int run_tests(int argc, char* argv[])
    {
        QCoreApplication application(argc, argv);
        const QVariantMap tool{{"kind", "tool"}, {"title", "office_save"}, {"state", "error"},
            {"elapsedMs", 123}, {"detail", "arguments and full JSON response"}, {"time", "12:00:00"}};
        const QVariantMap receipt = mirrorfly::ai_island_trace_entry(tool);
        if (!check(receipt.size() == 4 && receipt.value("title") == "office_save" &&
                    receipt.value("state") == "error" && receipt.value("elapsedMs") == 123 &&
                    !receipt.contains("detail"),
                "tool receipts contain execution status without arguments or response JSON"))
            return 1;
        if (!check(mirrorfly::ai_island_trace_entry({{"kind", "model"}}).isEmpty() &&
                    mirrorfly::ai_island_trace_entry({{"kind", "notice"}}).isEmpty(),
                "model and internal notice diagnostics stay out of the conversation"))
            return 1;
        const QVariantMap message =
            mirrorfly::ai_island_trace_entry({{"kind", "assistant"}, {"detail", "文件已保存"}});
        const QVariantMap unknown = mirrorfly::ai_island_trace_entry(
            {{"kind", "office"}, {"title", QString(300, 'x')}, {"state", "unknown"}, {"elapsedMs", -1}});
        if (!check(message.value("detail") == "文件已保存" && unknown.value("state") == "pending" &&
                    unknown.value("elapsedMs") == 0 && unknown.value("title").toString().size() == 160,
                "conversation content is preserved and unexpected receipt fields are bounded"))
            return 1;
        QLocalServer server;
        const QString name =
            QStringLiteral("mirrorfly-ai-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!check(server.listen(name), "test transport listens"))
            return 1;
        mirrorfly::AiIslandClient client(name, QStringLiteral("local-test-token"), nullptr, false);
        int trace_changes = 0;
        QObject::connect(&client, &mirrorfly::AiIslandClient::traceChanged, &client, [&trace_changes]()
        {
            ++trace_changes;
        });
        if (!check(wait_until(
                       [&server]()
        {
            return server.hasPendingConnections();
        }),
                "island connects to the local host"))
            return 1;
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        if (!check(wait_until(
                       [&socket]()
        {
            return socket->canReadLine();
        }),
                "island sends the handshake"))
            return 1;
        const QJsonObject hello = QJsonDocument::fromJson(socket->readLine()).object();
        bool passed = check(hello.value("type") == "hello" && hello.value("token") == "local-test-token",
            "handshake carries the expected local token");
        client.markUiReady();
        passed = check(wait_until(
                           [&socket]()
        {
            return socket->canReadLine();
        }) && QJsonDocument::fromJson(socket->readLine()).object().value("type") == "ui_ready",
                     "the helper acknowledges a loaded UI") &&
            passed;
        passed = check(!client.ready(), "island stays hidden before the configured state") && passed;
        socket->write(QJsonDocument(QJsonObject{{"type", "state"}, {"configured", true}, {"busy", false},
                                        {"status", "就绪"}, {"location", "首页"}})
                          .toJson(QJsonDocument::Compact) +
            '\n');
        socket->flush();
        passed = check(wait_until(
                           [&client]()
        {
            return client.ready();
        }) && client.status() == "就绪" &&
                         client.location() == "首页",
                     "authenticated state reveals the island") &&
            passed;
        socket->write(QJsonDocument(QJsonObject{{"type", "state"}, {"configured", true}, {"status", "处理中"},
                                        {"trace", QJsonArray{}}})
                          .toJson(QJsonDocument::Compact) +
            '\n');
        socket->flush();
        passed = check(wait_until(
                           [&client]()
        {
            return client.status() == "处理中";
        }) && trace_changes == 0,
                     "status updates leave the trace model untouched") &&
            passed;
        client.toggle();
        passed = check(client.expanded(), "shortcut state opens the chat panel") && passed;
        client.open();
        passed = check(client.expanded(), "an open command never closes an expanded island") && passed;
        client.sendPrompt(QStringLiteral("生成一个简报"));
        passed = check(client.busy() && client.compact() &&
                         wait_until(
                             [&socket]()
        {
            return socket->canReadLine();
        }),
                     "sending marks the UI pending until the host replies") &&
            passed;
        const QJsonObject request = QJsonDocument::fromJson(socket->readLine()).object();
        passed = check(request.value("type") == "send" && request.value("prompt") == "生成一个简报",
                     "the island sends a bounded prompt command") &&
            passed;
        client.open();
        passed = check(client.expanded() && !client.compact(), "opening restores the input form") && passed;
        client.collapse();
        passed = check(!client.expanded(), "closing the status island collapses it") && passed;
        socket->write(QJsonDocument(QJsonObject{{"type", "state"}, {"configured", true}, {"busy", false},
                                        {"requestId", request.value("requestId")}, {"accepted", true},
                                        {"thinkingEffort", "low"}})
                          .toJson(QJsonDocument::Compact) +
            '\n');
        socket->flush();
        passed = check(wait_until(
                           [&client]()
        {
            return !client.busy() && client.thinkingEffort() == "low";
        }),
                     "the host owns the confirmed thinking setting") &&
            passed;
        client.setThinkingEffort("high");
        passed = check(wait_until(
                           [&socket]()
        {
            return socket->canReadLine();
        }),
                     "effort command is sent") &&
            passed;
        const auto effort = QJsonDocument::fromJson(socket->readLine()).object();
        passed = check(effort.value("type") == "effort" && effort.value("value") == "high" &&
                         client.thinkingEffort() == "low" && client.effortPreview() == "high" &&
                         client.effortPending(),
                     "effort changes wait for the host confirmation") &&
            passed;
        client.setThinkingEffort("max");
        passed = check(wait_until(
                           [&]()
        {
            return socket->canReadLine();
        }),
                     "second effort request") &&
            passed;
        const auto maximum = QJsonDocument::fromJson(socket->readLine()).object();
        socket->write(QJsonDocument(QJsonObject{{"type", "effort_ack"},
                                        {"requestId", effort.value("requestId")}, {"value", "high"}})
                          .toJson(QJsonDocument::Compact) +
            '\n');
        socket->write(QJsonDocument(QJsonObject{{"type", "effort_ack"},
                                        {"requestId", maximum.value("requestId")}, {"value", "max"}})
                          .toJson(QJsonDocument::Compact) +
            '\n');
        passed = check(wait_until(
                           [&]()
        {
            return !client.effortPending();
        }) && client.thinkingEffort() == "max" &&
                         client.effortPreview() == "max",
                     "rapid effort changes accept only the latest acknowledgement") &&
            passed;
        passed = check(!client.sendPrompt(QString(4001, 'x')) && !client.busy(),
                     "an oversized prompt is rejected without locking input") &&
            passed;

        QString rejected;
        QObject::connect(&client, &mirrorfly::AiIslandClient::promptRejected, &client,
            [&](const QString& prompt)
        {
            rejected = prompt;
        });
        for (int index = 0; index < 300; ++index)
        {
            const QString prompt = QStringLiteral("Sequential task %1").arg(index);
            passed = check(client.sendPrompt(prompt), "next task is accepted locally") && passed;
            passed = check(wait_until(
                               [&]()
            {
                return socket->canReadLine();
            }),
                         "task reaches host") &&
                passed;
            const auto next = QJsonDocument::fromJson(socket->readLine()).object();
            socket->write(QJsonDocument(QJsonObject{{"type", "state"}, {"configured", true}, {"busy", false}})
                              .toJson(QJsonDocument::Compact) +
                '\n');
            socket->flush();
            QCoreApplication::processEvents();
            passed = check(client.busy(), "stale idle state cannot release a pending submission") && passed;
            socket->write(
                QJsonDocument(QJsonObject{{"type", "state"}, {"configured", true}, {"busy", false},
                                  {"requestId", next.value("requestId")}, {"accepted", index != 299}})
                    .toJson(QJsonDocument::Compact) +
                '\n');
            passed = check(wait_until(
                               [&]()
            {
                return !client.busy();
            }),
                         "acknowledgement releases input") &&
                passed;
        }
        passed = check(rejected == "Sequential task 299" && !client.compact(),
                     "a host rejection restores the submitted draft") &&
            passed;
        QByteArray backlog;
        for (int index = 0; index < 300; ++index)
            backlog +=
                QJsonDocument(QJsonObject{{"type", "state"}, {"configured", true}, {"busy", false},
                                  {"status", QString::number(index)}, {"statusDetail", QString(5000, 's')}})
                    .toJson(QJsonDocument::Compact) +
                '\n';
        socket->write(backlog);
        passed = check(wait_until(
                           [&]()
        {
            return client.status() == "299";
        }) && client.ready(),
                     "a backlog larger than one MiB of valid frames keeps the connection usable") &&
            passed;
        socket->disconnectFromServer();
        return passed ? 0 : 1;
    }
}

int main(int argc, char* argv[])
{
    return run_tests(argc, argv);
}
