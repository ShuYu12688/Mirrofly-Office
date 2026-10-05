#include "ai_island_protocol.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/office_ai.hpp>

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace
{
    QJsonObject read_office_state()
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(mirrorfly::office_runtime_snapshot()))
            .object();
    }

}

namespace mirrorfly
{
    void OfficeAiAgent::finish(
        const QString& status, const QString& answer, bool retain_task, const QString& activity)
    {
        const bool conversation = request_kind_ == RequestKind::Completion;
        retry_timer_.stop();
        if (conversation)
        {
            resumable_ = retain_task;
            paused_revision_ = response_revision_;
            if (retain_task)
            {
                if (run_.phase() == OfficeAiRun::Phase::Tools)
                    toolbox_.requireObservation();
                context_.interrupt();
            }
        }
        run_.cancel();
        provider_.cancel();
        toolbox_.cancel();
        busy_ = false;
        request_kind_ = RequestKind::None;
        activity_ = retain_task ? QStringLiteral("paused") : activity;
        if (activity_ == "completed")
            ++completed_tasks_;
        status_ = status;
        answer_ = answer;
        if (!current_turn_.isEmpty())
        {
            const QJsonObject execution{{"acceptedActions", successful_actions_},
                {"failedActions", failed_actions_}, {"inputTokens", input_tokens_},
                {"cachedInputTokens", cached_tokens_}, {"outputTokens", output_tokens_},
                {"modelRequests", turns_}, {"peakRequestBytes", peak_request_bytes_},
                {"contextCompactions", context_.compactions()}, {"toolErrors", tool_errors_},
                {"savedByAgent", saved_by_agent_}, {"resumable", retain_task},
                {"elapsedMs", task_clock_.isValid() ? task_clock_.elapsed() : 0}};
            diagnostic("finish", execution);
            current_turn_.clear();
        }
        if (model_trace_ >= 0 && model_trace_ < trace_.size() &&
            trace_.at(model_trace_).toMap().value("state") == "running")
            updateTrace(model_trace_, "stopped", status, request_clock_.elapsed());
        if (tool_trace_ >= 0 && tool_trace_ < trace_.size() &&
            trace_.at(tool_trace_).toMap().value("state") == "running")
            updateTrace(tool_trace_, "stopped", status, tool_clock_.elapsed());
        if (conversation)
        {
            addTrace("assistant", QStringLiteral("AI"), answer_.isEmpty() ? status : answer_);
        }
        // Task context and provider continuation exist only in this running session.
        if (conversation && !retain_task)
            context_ = {};
        emit stateChanged();
    }

    void OfficeAiAgent::start(const QString& prompt)
    {
        if (!configured() || busy_)
            return;
        const QString request = prompt.trimmed();
        if (request.isEmpty() || request.size() > ai_prompt_limit)
        {
            status_ = QStringLiteral("请输入不超过 4000 字的制作要求。");
            emit stateChanged();
            return;
        }
        if (resumable_ &&
            QRegularExpression(
                "^(?:保存了[，, ]*)?(?:请你?|好的[，, ]*)?\\s*(继续|接着做|resume\\b|continue\\b)",
                QRegularExpression::CaseInsensitiveOption)
                .match(request)
                .hasMatch())
        {
            resumeWithUpdate(request);
            return;
        }
        if (!resumable_ &&
            QRegularExpression(
                "^(继续|继续完成|继续任务|接着做|保存了[，, ]?请你继续|resume|continue)[。.!！\\s]*$",
                QRegularExpression::CaseInsensitiveOption)
                .match(request)
                .hasMatch())
        {
            status_ = QStringLiteral("当前会话没有待继续的任务，请说明需要处理的文档和要求。");
            emit stateChanged();
            return;
        }
        beginTask(request, request);
    }

    void OfficeAiAgent::beginTask(const QString& user_input, const QString& goal)
    {
        model_trace_ = -1;
        tool_trace_ = -1;
        addTrace("user", QStringLiteral("你"), user_input);
        answer_.clear();
        task_clock_.start();
        toolbox_.reset();
        toolbox_.updateTaskRequest(goal);
        constraints_ = office_ai_task_constraints(goal);
        const int slide_pages = constraints_.slide_pages;
        toolbox_.setRequestedSlidePages(slide_pages);
        resumable_ = false;
        retry_timer_.stop();
        run_.begin();
        request_kind_ = RequestKind::Completion;
        successful_actions_ = 0;
        failed_actions_ = 0;
        input_tokens_ = 0;
        cached_tokens_ = 0;
        output_tokens_ = 0;
        peak_request_bytes_ = 0;
        current_request_bytes_ = 0;
        repeated_failures_.clear();
        repeated_observations_.clear();
        tool_errors_ = 0;
        skip_calls_ = false;
        task_after_reobserve_ = false;
        document_work_ = false;
        stop_after_tools_.clear();
        saved_by_agent_ = false;
        saved_module_.clear();
        truncation_retries_ = 0;
        network_retries_ = 0;
        protocol_retries_ = 0;
        completion_checks_ = 0;
        post_save_observations_ = 0;
        document_key_ = office_ai_document_key(read_office_state());
        current_turn_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        current_request_ = goal;
        QString system = office_ai_system_prompt();
        context_.begin(system, goal);
        context_.setExecutionConstraints(constraints_.checkpoint());
        turns_ = 0;
        busy_ = true;
        activity_ = "waiting";
        status_ = QStringLiteral("正在规划文档…");
        emit stateChanged();
        sendCompletion();
    }

    void OfficeAiAgent::cancel()
    {
        toolbox_.cancel();
        provider_.cancel();
        if (busy_)
            finish(QStringLiteral("已停止；已完成的编辑保留，可继续原任务。"), {},
                request_kind_ == RequestKind::Completion);
    }
}
