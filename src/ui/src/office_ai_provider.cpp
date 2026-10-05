#include "office_ai_provider.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>

namespace
{
    QJsonArray wire_messages(const QJsonArray& messages, bool deepseek)
    {
        QJsonArray result;
        for (const auto& value : messages)
        {
            auto message = value.toObject();
            if (message.value("role") == "assistant")
            {
                const auto continuation = message.take("providerState").toObject();
                QJsonArray calls;
                for (const auto& entry : message.take("calls").toArray())
                {
                    const auto call = entry.toObject();
                    calls.append(QJsonObject{{"id", call.value("id")}, {"type", "function"},
                        {"function",
                            QJsonObject{{"name", call.value("name")},
                                {"arguments",
                                    QString::fromUtf8(QJsonDocument(call.value("input").toObject())
                                            .toJson(QJsonDocument::Compact))}}}});
                }
                if (!calls.isEmpty() || deepseek)
                    message.insert("tool_calls", calls);
                if (deepseek)
                    message.insert("reasoning_content", continuation.value("reasoning_content").toString());
            }
            else if (message.value("role") == "tool_result")
            {
                message.insert("role", "tool");
                message.insert("tool_call_id", message.take("id"));
            }
            result.append(message);
        }
        return result;
    }

    QJsonArray wire_tools(const QJsonArray& tools)
    {
        QJsonArray result;
        for (const auto& tool : tools)
            result.append(QJsonObject{{"type", "function"}, {"function", tool}});
        return result;
    }
}

namespace mirrorfly
{
    OfficeAiProvider::OfficeAiProvider(QNetworkAccessManager* transport, QObject* parent)
        : QObject(parent), transport_(transport ? transport : &network_), deadline_(this)
    {
        deadline_.setObjectName("officeAiRequestDeadline");
        deadline_.setSingleShot(true);
        connect(&deadline_, &QTimer::timeout, this, [this]()
        {
            if (!reply_)
                return;
            const bool probe = probing_;
            OfficeAiModelReply result;
            result.error_code = "request_deadline";
            result.error = QStringLiteral("模型响应超过时限，本次未完成的指令未执行；可继续或提交新任务。");
            result.elapsed_ms = clock_.elapsed();
            result.first_packet_ms = first_packet_ms_;
            cancel();
            if (probe)
                emit probeCompleted(QStringLiteral("连接检查超时，请检查接口后重试。"));
            else
                emit completed(result);
        });
    }

    OfficeAiProvider::~OfficeAiProvider()
    {
        cancel();
        key_.fill(0);
    }

    bool OfficeAiProvider::validConfiguration(
        const QString& address, const QString& model, const QString& key, const QString& effort)
    {
        const QUrl url(address.trimmed());
        const bool local = url.host() == "localhost" || url.host() == "127.0.0.1" || url.host() == "::1";
        if (!url.isValid() || url.host().isEmpty() ||
            (url.scheme() != "https" && !(url.scheme() == "http" && local)) || !url.userInfo().isEmpty() ||
            !url.query().isEmpty() || !url.fragment().isEmpty() || address.size() > 2048 ||
            model.contains('\n') || model.contains('\r') || key.contains('\n') || key.contains('\r') ||
            model.trimmed().isEmpty() || model.size() > 128 || key.trimmed().isEmpty() || key.size() > 4096 ||
            !QStringList{"none", "low", "high", "max"}.contains(effort))
            return false;
        return true;
    }

    bool OfficeAiProvider::configure(
        const QString& address, const QString& model, const QString& key, const QString& effort)
    {
        if (reply_ || !validConfiguration(address, model, key, effort))
            return false;
        key_.fill(0);
        key_ = key.toUtf8();
        base_url_ = QUrl(address.trimmed());
        model_ = model.trimmed();
        effort_ = effort;
        return true;
    }

    QString OfficeAiProvider::address() const
    {
        return configured() ? base_url_.toString() : QStringLiteral("https://api.deepseek.com");
    }

    QString OfficeAiProvider::model() const
    {
        return configured() ? model_ : QStringLiteral("deepseek-flash");
    }

    QString OfficeAiProvider::effort() const
    {
        return configured() ? effort_ : QStringLiteral("none");
    }

    bool OfficeAiProvider::setEffort(const QString& effort)
    {
        if (reply_ || !configured() || !QStringList{"none", "low", "high", "max"}.contains(effort))
            return false;
        effort_ = effort;
        return true;
    }

    bool OfficeAiProvider::configured() const
    {
        return base_url_.isValid() && !model_.isEmpty() && !key_.isEmpty();
    }

    QUrl OfficeAiProvider::endpoint(const QString& suffix) const
    {
        QString base = base_url_.toString();
        while (base.endsWith('/'))
            base.chop(1);
        if (base.endsWith("/chat/completions"))
            base.chop(QStringLiteral("/chat/completions").size());
        return QUrl(base + suffix);
    }

    void OfficeAiProvider::probe()
    {
        if (!configured() || reply_)
            return;
        QNetworkRequest request(endpoint("/models"));
        request.setRawHeader("Authorization", "Bearer " + key_);
        request.setTransferTimeout(20000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto* reply = transport_->get(request);
        reply_ = reply;
        probing_ = true;
        clock_.start();
        deadline_.start(20000);
        connect(reply, &QNetworkReply::finished, this, [this, reply]()
        {
            if (reply_ != reply)
                return;
            reply_ = nullptr;
            deadline_.stop();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            QString message = QStringLiteral("连接失败（HTTP %1）。").arg(status);
            if (reply->error() == QNetworkReply::NoError && status == 200)
            {
                bool found = false;
                for (const auto& item : response.value("data").toArray())
                    found = found || item.toObject().value("id").toString() == model_;
                message = found ? QStringLiteral("连接成功，模型可用。")
                                : QStringLiteral("接口可用，但未列出当前模型名称。");
            }
            reply->deleteLater();
            emit probeCompleted(message);
        });
    }

    QJsonObject OfficeAiProvider::request(const QJsonArray& messages, const QJsonArray& tools)
    {
        if (!configured() || reply_)
            return {{"ok", false}, {"error", QStringLiteral("模型未配置或上一请求尚未结束。")}};
        const bool deepseek = base_url_.host() == "api.deepseek.com";
        const auto provider_messages = wire_messages(messages, deepseek);
        const auto provider_tools = wire_tools(tools);
        QJsonObject body{{"model", model_}, {"messages", provider_messages}, {"tools", provider_tools},
            {"tool_choice", "auto"}, {"stream", true},
            {"stream_options", QJsonObject{{"include_usage", true}}}};
        if (deepseek)
            body.insert("thinking", QJsonObject{{"type", effort_ == "none" ? "disabled" : "enabled"}});
        if (effort_ != "none")
            body.insert("reasoning_effort", !deepseek && effort_ == "max" ? "high" : effort_);
        const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        QJsonArray semantic_messages;
        for (const auto& value : provider_messages)
        {
            auto message = value.toObject();
            message.remove("reasoning_content");
            semantic_messages.append(message);
        }
        auto semantic_body = body;
        semantic_body.insert("messages", semantic_messages);
        const auto semantic_bytes = QJsonDocument(semantic_body).toJson(QJsonDocument::Compact).size();
        // Provider continuation is opaque state, not another copy of the task instructions.
        // Preserve it across tool calls without relaxing the semantic-context limit.
        if (semantic_bytes > 64 * 1024 || payload.size() > 512 * 1024)
            return {{"ok", false},
                {"error", QStringLiteral("本次请求上下文过大，已停止发送；已完成的编辑保留。")}};
        QNetworkRequest request(endpoint("/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + key_);
        request.setTransferTimeout(120000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        stream_ = {};
        first_packet_ms_ = -1;
        clock_.start();
        ui_clock_.start();
        auto* reply = transport_->post(request, payload);
        reply_ = reply;
        probing_ = false;
        // Transfer inactivity alone cannot stop a stream sending endless keepalive packets.
        deadline_.start(300000);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply]()
        {
            if (reply_ == reply)
                read(reply);
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply]()
        {
            if (reply_ != reply)
                return;
            complete(reply);
        });
        return {{"ok", true}, {"bytes", payload.size()}, {"semanticBytes", semantic_bytes},
            {"toolSchemaBytes", QJsonDocument(provider_tools).toJson(QJsonDocument::Compact).size()},
            {"messageBytes", QJsonDocument(provider_messages).toJson(QJsonDocument::Compact).size()}};
    }

    void OfficeAiProvider::read(QNetworkReply* reply)
    {
        const auto bytes = reply->readAll();
        if (bytes.isEmpty())
            return;
        if (first_packet_ms_ < 0)
            first_packet_ms_ = clock_.elapsed();
        stream_.append(bytes);
        if (stream_.failed())
        {
            reply->abort();
            return;
        }
        if (ui_clock_.elapsed() >= 50 || stream_.complete())
        {
            ui_clock_.restart();
            emit progress(stream_.content(), clock_.elapsed(), stream_.activity());
        }
    }

    void OfficeAiProvider::complete(QNetworkReply* reply)
    {
        read(reply);
        if (reply_ != reply)
            return;
        reply_ = nullptr;
        deadline_.stop();
        OfficeAiModelReply result;
        result.elapsed_ms = clock_.elapsed();
        result.first_packet_ms = first_packet_ms_;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.http_status = status;
        const bool success = reply->error() == QNetworkReply::NoError && status == 200 && stream_.complete();
        const auto network_error = reply->error();
        const bool temporary_network = network_error == QNetworkReply::RemoteHostClosedError ||
            network_error == QNetworkReply::HostNotFoundError ||
            network_error == QNetworkReply::TimeoutError ||
            network_error == QNetworkReply::TemporaryNetworkFailureError ||
            network_error == QNetworkReply::NetworkSessionFailedError ||
            network_error == QNetworkReply::ConnectionRefusedError;
        result.retry_after_ms = qBound(0, reply->rawHeader("Retry-After").toInt(), 30) * 1000;
        reply->deleteLater();
        if (!success)
        {
            result.retryable = !stream_.failed() &&
                (temporary_network || status == 408 || status == 429 || status == 500 || status == 502 ||
                    status == 503 || status == 504 || status == 200);
            result.error_code = stream_.failed() ? "invalid_stream" : "incomplete_response";
            result.error = QStringLiteral("模型响应未完成（HTTP %1），未执行本次指令。").arg(status);
            stream_ = {};
            emit completed(result);
            return;
        }
        const auto response = stream_.response();
        const auto choice = response.value("choices").toArray().first().toObject();
        const auto message = choice.value("message").toObject();
        const auto reason = choice.value("finish_reason").toString();
        result.finish_reason = reason;
        result.usage = response.value("usage").toObject();
        result.text = message.value("content").toString();
        const auto calls = message.value("tool_calls").toArray();
        bool valid = (reason == "stop" && calls.isEmpty()) || (reason == "tool_calls" && !calls.isEmpty());
        QSet<QString> ids;
        for (const auto& value : calls)
        {
            const auto call = value.toObject();
            const auto function = call.value("function").toObject();
            const auto id = call.value("id").toString();
            QJsonParseError error;
            const auto input =
                QJsonDocument::fromJson(function.value("arguments").toString().toUtf8(), &error);
            valid = valid && !id.isEmpty() && !ids.contains(id) && call.value("type") == "function" &&
                !function.value("name").toString().isEmpty() && error.error == QJsonParseError::NoError &&
                input.isObject();
            ids.insert(id);
            result.calls.append(
                QJsonObject{{"id", id}, {"name", function.value("name")}, {"input", input.object()}});
        }
        result.continuation = {{"role", "assistant"}, {"content", result.text}, {"calls", result.calls},
            {"providerState", QJsonObject{{"reasoning_content", message.value("reasoning_content")}}}};
        if (reason == "length")
            result.stop = OfficeAiStop::Truncated;
        else if (reason == "insufficient_system_resource" || reason == "aborted")
        {
            result.retryable = true;
            result.error_code = "provider_interrupted";
            result.error = QStringLiteral("模型生成中断，本次指令未执行。");
        }
        else if (reason == "content_filter")
        {
            result.error_code = "content_filtered";
            result.error = QStringLiteral("模型未返回可用内容，本次指令未执行。");
        }
        else if (valid && calls.isEmpty() && result.text.trimmed().isEmpty())
        {
            result.retryable = true;
            result.error_code = "empty_completion";
            result.error = QStringLiteral("模型返回空白结果，本次指令未执行。");
        }
        else if (valid)
            result.stop = calls.isEmpty() ? OfficeAiStop::Complete : OfficeAiStop::Tools;
        else
        {
            result.error_code = "invalid_tool_envelope";
            result.error = QStringLiteral("模型的调用标识或结构无效，本次指令均未执行。");
        }
        stream_ = {};
        emit completed(result);
    }

    void OfficeAiProvider::cancel()
    {
        deadline_.stop();
        if (reply_)
        {
            auto* reply = reply_.data();
            reply_ = nullptr;
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
            reply->deleteLater();
        }
        stream_ = {};
    }
}
