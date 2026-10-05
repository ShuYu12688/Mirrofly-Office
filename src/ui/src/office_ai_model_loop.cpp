#include "office_ai_agent.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>

#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace mirrorfly
{
    void OfficeAiAgent::testConnection()
    {
        if (!configured() || busy_)
            return;
        busy_ = true;
        request_kind_ = RequestKind::Probe;
        activity_ = "waiting";
        status_ = QStringLiteral("正在检查模型接口…");
        emit stateChanged();
        provider_.probe();
    }

    void OfficeAiAgent::sendCompletion()
    {
        // A changing query or repeated checklist cannot keep the input locked indefinitely.
        if (turns_ >= 96 || (task_clock_.isValid() && task_clock_.elapsed() >= 15 * 60 * 1000))
        {
            finish(QStringLiteral("本轮达到执行预算，已暂停并保留进度；可继续或提交新任务。"), {}, true);
            return;
        }
        if (!checkWorkspace())
            return;
        if (!run_.request())
        {
            finish(QStringLiteral("任务调度状态异常，已释放输入并保留进度；可继续或提交新任务。"), {}, true);
            return;
        }
        const auto workspace = office_ai_workspace(
            QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object());
        if (network_retries_ > 0 && response_revision_ != workspace.value("revision").toString())
        {
            toolbox_.requireObservation();
            context_.notice("The document changed during the connection interruption. Read its current "
                            "content before editing. Earlier tool results describe the previous state.");
        }
        ++turns_;
        request_kind_ = RequestKind::Completion;
        response_revision_ = workspace.value("revision").toString();
        toolbox_.beginResponse(response_revision_, turns_);
        answer_.clear();
        request_clock_.start();
        const auto messages = context_.messages(workspace, toolbox_.groups().contains("compose"));
        const auto sent = provider_.request(messages, office_ai_tools(toolbox_.groups()));
        if (!sent.value("ok").toBool())
        {
            finish(sent.value("error").toString(), {}, true);
            return;
        }
        current_request_bytes_ = sent.value("bytes").toInteger();
        peak_request_bytes_ = qMax(peak_request_bytes_, current_request_bytes_);
        auto fields = sent;
        fields.remove("ok");
        fields.insert("number", turns_);
        fields.insert("compactions", context_.compactions());
        fields.insert("compactionReason", context_.compactionReason());
        fields.insert("revision", response_revision_);
        diagnostic("request", fields);
        model_trace_ = addTrace("model",
            QStringLiteral("模型请求 %1 · %2 KB · 已整理 %3 次")
                .arg(turns_)
                .arg(sent.value("bytes").toDouble() / 1024.0, 0, 'f', 1)
                .arg(context_.compactions()));
        updateTrace(model_trace_, "running", QStringLiteral("等待模型"), 0);
        activity_ = "waiting";
        status_ = QStringLiteral("模型请求 %1…").arg(turns_);
        emit stateChanged();
    }

    void OfficeAiAgent::completeModel(const OfficeAiModelReply& reply)
    {
        if (!busy_ || run_.phase() != OfficeAiRun::Phase::Model)
            return;
        if (reply.stop == OfficeAiStop::Error)
        {
            retryModel(reply);
            return;
        }
        emit modelObserved(reply.text, reply.calls);
        network_retries_ = 0;
        const auto usage = reply.usage;
        const qint64 input = qMax<qint64>(0, usage.value("prompt_tokens").toInteger());
        const qint64 output = qMax<qint64>(0, usage.value("completion_tokens").toInteger());
        const qint64 cached = qBound<qint64>(0,
            usage.value("prompt_cache_hit_tokens")
                .toInteger(
                    usage.value("prompt_tokens_details").toObject().value("cached_tokens").toInteger()),
            input);
        input_tokens_ += input;
        cached_tokens_ += cached;
        output_tokens_ += output;
        session_input_tokens_ += input;
        session_cached_tokens_ += cached;
        session_output_tokens_ += output;
        diagnostic("response",
            {{"number", turns_}, {"firstPacketMs", reply.first_packet_ms}, {"elapsedMs", reply.elapsed_ms},
                {"finishReason", reply.finish_reason}, {"callCount", reply.calls.size()},
                {"textChars", reply.text.size()}, {"usage", usage}});
        updateTrace(model_trace_, "done",
            QStringLiteral("首包 %1 ms · 总计 %2 ms · 输入 %3 / 输出 %4 token")
                .arg(reply.first_packet_ms)
                .arg(reply.elapsed_ms)
                .arg(usage.value("prompt_tokens").toInt())
                .arg(usage.value("completion_tokens").toInt()),
            reply.elapsed_ms);
        if (!checkWorkspace())
            return;
        if (reply.stop == OfficeAiStop::Truncated)
        {
            answer_.clear();
            if (truncation_retries_++ == 0 && run_.accept({}))
            {
                context_.notice("The last response was truncated; none of its tool calls ran. Return the "
                                "next smaller coherent batch. Keep all earlier successful work.");
                sendCompletion();
                return;
            }
            finish(QStringLiteral("连续输出截断，已停止；已完成的编辑保留，请缩小要求后继续。"), {}, true);
            return;
        }
        truncation_retries_ = 0;
        if (!run_.accept(reply.calls))
        {
            diagnostic("protocol_error", {{"error", "invalid_tool_call_envelope"}});
            finish(QStringLiteral("模型的调用标识或结构无效，本次指令均未执行。"));
            return;
        }
        if (reply.stop == OfficeAiStop::Complete)
        {
            completeTask(reply);
            return;
        }
        context_.assistant(reply.continuation);
        skip_calls_ = false;
        task_after_reobserve_ = false;
        scheduleNextTool();
    }
}
