#include "office_ai_provider.hpp"
#include "office_ai_test_transport.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <iostream>

namespace
{
    class Transport final : public QNetworkAccessManager
    {
    public:
        QNetworkRequest last_request;
        QJsonObject last_body;

    protected:
        QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice* outgoing) override
        {
            last_request = request;
            last_body = outgoing ? QJsonDocument::fromJson(outgoing->readAll()).object() : QJsonObject{};
            return new office_ai_test::Reply(request, {}, this);
        }
    };

    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    bool wait_for(bool& completed)
    {
        QElapsedTimer timer;
        timer.start();
        while (!completed && timer.elapsed() < 2000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return completed;
    }
}

int run_ai_endpoint_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    using mirrorfly::OfficeAiProvider;
    bool passed = check(OfficeAiProvider::validConfiguration(
                            "https://custom.example:8443/api/v1/", "custom-model", "fake-key", "none") &&
            OfficeAiProvider::validConfiguration("http://127.0.0.1:11434/v1", "local", "fake-key", "low"),
        "custom HTTPS paths and loopback HTTP services are supported");
    for (const auto& address : {"http://remote.example/v1", "file:///models", "https://a:b@custom.example/v1",
             "https://custom.example/v1?key=secret", "https://custom.example/v1#fragment"})
        passed = check(!OfficeAiProvider::validConfiguration(address, "model", "key", "none"),
                     "invalid service addresses are rejected") &&
            passed;
    passed =
        check(!OfficeAiProvider::validConfiguration("https://custom.example", "model", "key\r\n", "none"),
            "header control characters cannot enter credentials") &&
        passed;
    Transport transport;
    OfficeAiProvider provider(&transport);
    bool completed = false;
    QObject::connect(&provider, &OfficeAiProvider::completed, &provider,
        [&completed](const mirrorfly::OfficeAiModelReply& reply)
    {
        completed = reply.stop == mirrorfly::OfficeAiStop::Complete;
    });
    passed = check(provider.configure("https://custom.example/api/v1/", "custom", "fake-key", "max"),
                 "configure a custom provider") &&
        passed;
    const QJsonArray messages{QJsonObject{{"role", "assistant"}, {"content", "hello"},
        {"providerState", QJsonObject{{"reasoning_content", "private-deepseek-marker"}}},
        {"calls", QJsonArray{}}}};
    passed = check(provider.request(messages, {}).value("ok").toBool() && wait_for(completed),
                 "custom provider returns through the existing stream parser") &&
        passed;
    const auto message = transport.last_body.value("messages").toArray().first().toObject();
    passed = check(transport.last_request.url() == QUrl("https://custom.example/api/v1/chat/completions") &&
                     transport.last_request.rawHeader("Authorization") == "Bearer fake-key" &&
                     !transport.last_body.contains("thinking") &&
                     transport.last_body.value("reasoning_effort") == "high" &&
                     !message.contains("reasoning_content") && !message.contains("providerState") &&
                     !message.contains("tool_calls"),
                 "custom requests preserve the base path and omit DeepSeek-specific fields") &&
        passed;
    passed = check(provider.configure("http://localhost:11434/v1/chat/completions", "local", "fake", "none"),
                 "full chat endpoint addresses are accepted") &&
        passed;
    completed = false;
    provider.request({}, {});
    passed = check(wait_for(completed) &&
                     transport.last_request.url() == QUrl("http://localhost:11434/v1/chat/completions") &&
                     !transport.last_body.contains("reasoning_effort"),
                 "full endpoints are not duplicated and disabled effort adds no vendor option") &&
        passed;
    provider.probe();
    passed = check(transport.last_request.url() == QUrl("http://localhost:11434/v1/models"),
                 "connection probes use the matching models endpoint") &&
        passed;
    provider.cancel();
    passed = check(provider.configure("https://api.deepseek.com", "deepseek-flash", "fake", "max"),
                 "configure maximum thinking through the production provider") &&
        passed;
    const QString long_reasoning(90000, QChar('r'));
    const QJsonArray continuation{QJsonObject{{"role", "assistant"}, {"content", ""}, {"calls", QJsonArray{}},
        {"providerState", QJsonObject{{"reasoning_content", long_reasoning}}}}};
    completed = false;
    const auto receipt = provider.request(continuation, {});
    passed = check(receipt.value("ok").toBool() && receipt.value("semanticBytes").toInt() < 1024 &&
                     wait_for(completed) && transport.last_body.value("reasoning_effort") == "max" &&
                     transport.last_body.value("messages")
                             .toArray()
                             .first()
                             .toObject()
                             .value("reasoning_content")
                             .toString() == long_reasoning,
                 "long provider continuation stays exact without bypassing the semantic budget") &&
        passed;
    const QJsonArray oversized{QJsonObject{{"role", "user"}, {"content", QString(70000, QChar('x'))}}};
    passed = check(!provider.request(oversized, {}).value("ok").toBool(),
                 "semantic instructions retain the original request-size limit") &&
        passed;
    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_ai_endpoint_tests(argc, argv);
}
