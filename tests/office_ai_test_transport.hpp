#pragma once

#include <mirrorfly/automation.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>

#include <cstring>
#include <functional>
#include <utility>

namespace office_ai_test
{
    inline QByteArray json(const QJsonObject& object)
    {
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    inline QJsonObject runtime()
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(mirrorfly::office_runtime_snapshot()))
            .object();
    }

    inline QJsonObject call(const QString& name, const QJsonObject& args, int index = 0)
    {
        return {{"index", index}, {"id", QStringLiteral("call_%1").arg(index)}, {"type", "function"},
            {"function", QJsonObject{{"name", name}, {"arguments", QString::fromUtf8(json(args))}}}};
    }

    struct Fault
    {
        int status = 200;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
        bool incomplete = false;
        bool malformed = false;
        bool truncated = false;
        bool empty_content = false;
        QString finish_reason;
        int delay_ms = 0;
    };

    class Reply final : public QNetworkReply
    {
    public:
        Reply(const QNetworkRequest& request, const QJsonArray& calls, QObject* parent, Fault fault = {})
            : QNetworkReply(parent)
        {
            setRequest(request);
            setUrl(request.url());
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, fault.status);
            open(QIODevice::ReadOnly | QIODevice::Unbuffered);
            auto response_calls = calls;
            if (fault.malformed && !response_calls.isEmpty())
            {
                auto invalid = response_calls.first().toObject();
                auto function = invalid.value("function").toObject();
                function.insert("arguments", "{");
                invalid.insert("function", function);
                response_calls[0] = invalid;
            }
            const QJsonObject delta{{"role", "assistant"},
                {"content",
                    fault.empty_content   ? ""
                        : calls.isEmpty() ? "Finished"
                                          : ""},
                {"reasoning_content", "private reasoning preserved only within complete exchanges"},
                {"tool_calls", response_calls}};
            bytes_ = "data: " +
                json({{"choices",
                          QJsonArray{QJsonObject{{"index", 0}, {"delta", delta},
                              {"finish_reason",
                                  !fault.finish_reason.isEmpty() ? fault.finish_reason
                                      : fault.truncated          ? "length"
                                      : calls.isEmpty()          ? "stop"
                                                                 : "tool_calls"}}}},
                    {"usage", QJsonObject{{"prompt_tokens", 120}, {"completion_tokens", 30}}}}) +
                "\n\ndata: [DONE]\n\n";
            if (fault.incomplete)
                bytes_.replace("data: [DONE]\n\n", "");
            QTimer::singleShot(fault.delay_ms, this, [this, fault]()
            {
                if (fault.error != QNetworkReply::NoError)
                    setError(fault.error, "injected transport failure");
                emit readyRead();
                setFinished(true);
                emit finished();
            });
        }

        void abort() override
        {
            setFinished(true);
        }

        qint64 bytesAvailable() const override
        {
            return bytes_.size() + QNetworkReply::bytesAvailable();
        }

    protected:
        qint64 readData(char* target, qint64 maximum) override
        {
            const auto size = qMin(maximum, static_cast<qint64>(bytes_.size()));
            if (size == 0)
                return -1;
            std::memcpy(target, bytes_.constData(), static_cast<std::size_t>(size));
            bytes_.remove(0, size);
            return size;
        }

    private:
        QByteArray bytes_;
    };

    class Transport final : public QNetworkAccessManager
    {
    public:
        explicit Transport(std::function<void(bool, const char*)> verify) : check_(std::move(verify))
        {
        }
        std::function<void(bool, const char*)> check_;
        std::function<QJsonArray(int, const QJsonObject&)> respond;
        std::function<Fault(int)> fault;
        QList<QByteArray> requests;
        qsizetype output_call_bytes = 0;

    protected:
        QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice* outgoing) override
        {
            const QByteArray payload = outgoing->readAll();
            requests.append(payload);
            check_(payload.size() <= 64 * 1024, "actual outgoing request stays bounded");
            check_(!payload.contains("historical-secret-marker"), "history is never automatically injected");
            const auto body = QJsonDocument::fromJson(payload).object();
            if (body.value("thinking").toObject().value("type") == "disabled")
                check_(!body.contains("reasoning_effort"),
                    "disabled thinking does not send a conflicting effort field");
            else
                check_(QStringList{"low", "high", "max"}.contains(body.value("reasoning_effort").toString()),
                    "thinking effort is encoded only by the provider adapter");
            QSet<QString> pending;
            for (const auto& value : body.value("messages").toArray())
            {
                const auto message = value.toObject();
                if (message.value("role") == "assistant")
                {
                    check_(pending.isEmpty(), "no overlapping assistant tool exchanges");
                    check_(message.contains("reasoning_content"), "retained thinking protocol is complete");
                    for (const auto& item : message.value("tool_calls").toArray())
                        pending.insert(item.toObject().value("id").toString());
                }
                if (message.value("role") == "tool")
                    check_(pending.remove(message.value("tool_call_id").toString()),
                        "tool response has matching call");
            }
            check_(pending.isEmpty(), "all calls receive results before another request");
            const int index = static_cast<int>(requests.size()) - 1;
            const auto calls = respond(index, body);
            if (!calls.isEmpty())
                output_call_bytes += QJsonDocument(calls).toJson(QJsonDocument::Compact).size();
            return new Reply(request, calls, this, fault ? fault(index) : Fault{});
        }
    };

}
