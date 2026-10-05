#include "document_path.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>

#include <QJsonDocument>
#include <QUuid>

namespace mirrorfly
{
    void OfficeAiAgent::retryModel(const OfficeAiModelReply& reply)
    {
        diagnostic("request_error",
            {{"code", reply.error_code}, {"httpStatus", reply.http_status}, {"retryable", reply.retryable},
                {"finishReason", reply.finish_reason}, {"attempt", network_retries_},
                {"repairAttempt", protocol_retries_}, {"elapsedMs", reply.elapsed_ms}});
        updateTrace(model_trace_, "error", reply.error, reply.elapsed_ms);
        answer_.clear();
        if (reply.error_code == "invalid_tool_envelope")
        {
            if (protocol_retries_ >= 2 || !run_.accept({}))
            {
                finish(reply.error + QStringLiteral(" 原任务已保留，可修正要求后继续。"), {}, true);
                return;
            }
            ++protocol_retries_;
            context_.notice("The last model response had an invalid tool-call envelope. None of its calls "
                            "ran. A reply with tool calls must finish with tool_calls; use unique IDs and "
                            "JSON object arguments. Keep earlier accepted edits; do not replay them.");
            sendCompletion();
            return;
        }
        if (!reply.retryable || network_retries_ >= 3 || !run_.accept({}))
        {
            finish(reply.error + QStringLiteral(" 原任务已保留，恢复连接后可继续。"), {}, true);
            return;
        }
        const int delays[]{1000, 3000, 8000};
        const int delay = qMax(delays[network_retries_++], reply.retry_after_ms);
        const auto retry_status = QStringLiteral("连接中断，%1 秒后重试（%2/3）；已完成的编辑保留。");
        activity_ = "retrying";
        status_ = retry_status.arg(delay / 1000).arg(network_retries_);
        retry_timer_.start(delay);
        emit stateChanged();
    }

    void OfficeAiAgent::resume()
    {
        resumeWithUpdate({});
    }

    void OfficeAiAgent::resumeWithUpdate(const QString& update)
    {
        if (busy_ || !configured() || !resumable_)
            return;
        const auto runtime =
            QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
        if (!office_ai_workspace(runtime).value("ok").toBool() ||
            office_ai_document_key(runtime) != document_key_ || !toolbox_.checkWorkspace())
        {
            status_ = QStringLiteral("当前文档与暂停时不同；原任务仍保留，请回到原文档后继续。");
            emit stateChanged();
            return;
        }
        if (runtime.value("revision").toString() != paused_revision_)
        {
            context_.notice("The document changed while paused. Read current content before editing; "
                            "saved receipts describe past state, not the present document.");
            toolbox_.requireObservation();
        }
        if (!update.isEmpty() && !context_.canAcceptUserUpdate(update))
        {
            status_ = QStringLiteral("续作要求过长或累计指令已达到上下文上限；原任务和保护约束仍保留。");
            emit stateChanged();
            return;
        }
        run_.begin();
        request_kind_ = RequestKind::Completion;
        turns_ = 0;
        truncation_retries_ = 0;
        task_clock_.restart();
        network_retries_ = 0;
        protocol_retries_ = 0;
        completion_checks_ = 0;
        post_save_observations_ = 0;
        stop_after_tools_.clear();
        repeated_failures_.clear();
        repeated_observations_.clear();
        if (!update.isEmpty())
        {
            const auto updated = office_ai_task_constraints(update);
            if (updated.slide_pages > 0 && updated.slide_pages != constraints_.slide_pages)
                context_.reviseSlideTarget(
                    runtime.value("modules").toObject().value("slides").toObject(), updated.slide_pages);
            constraints_.merge(updated);
            toolbox_.setRequestedSlidePages(constraints_.slide_pages);
            context_.setExecutionConstraints(constraints_.checkpoint());
            toolbox_.updateTaskRequest(update);
            context_.userUpdate(update);
        }
        current_turn_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        resumable_ = false;
        busy_ = true;
        status_ = QStringLiteral("继续原任务…");
        addTrace("user", QStringLiteral("继续原任务"), update.isEmpty() ? current_request_ : update);
        diagnostic("resume", {{"document", document_key_}, {"revision", runtime.value("revision")}});
        emit stateChanged();
        sendCompletion();
    }
}
