#include "office_ai_agent.hpp"
#include "office_ai_recovery.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/office_ai.hpp>

#include <QJsonDocument>

namespace mirrorfly
{
    void OfficeAiAgent::scheduleNextTool()
    {
        const auto epoch = run_.epoch();
        QTimer::singleShot(0, this, [this, epoch]()
        {
            if (epoch == run_.epoch())
                processNextTool();
        });
    }

    void OfficeAiAgent::processNextTool()
    {
        if (!busy_ || request_kind_ != RequestKind::Completion || !checkWorkspace())
            return;
        if (run_.phase() == OfficeAiRun::Phase::Ready)
        {
            if (!stop_after_tools_.isEmpty())
                finish(stop_after_tools_, {}, true);
            else if (post_save_observations_ >= 16)
                finish(QStringLiteral("文件已保存，但随后连续查询没有产生新编辑；已暂停并保留进度，可继续。"),
                    {}, true);
            else if (toolbox_.stagnantEdits())
                finish(QStringLiteral(
                    "相同样式已应用，重复提交没有新增编辑；已保留结果，请指定下一步希望调整的内容。"));
            else
                sendCompletion();
            return;
        }
        if (run_.phase() != OfficeAiRun::Phase::Tools)
            return;
        const auto epoch = run_.epoch();
        const auto call = run_.current();
        const auto name = call.value("name").toString();
        const auto arguments = call.value("input").toObject();
        const auto traits = office_ai_tool_traits(name, arguments);
        const bool independent = traits.independent;
        if (skip_calls_ && !independent && !(task_after_reobserve_ && name == "office_task"))
        {
            task_after_reobserve_ = false;
            acceptToolResult({{"ok", false}, {"error", "not_executed_reobserve"}});
            return;
        }
        if (skip_calls_)
            task_after_reobserve_ = false;
        const QString detail = QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
        if (traits.saving)
            activity_ = "saving";
        else if (name == "office_compose_table")
            activity_ = "calculating";
        else if (traits.document_work)
            activity_ = "writing";
        else if (name == "office_task")
            activity_ = "processing";
        else
            activity_ = "reading";
        emit stateChanged();
        if (epoch != run_.epoch() || !busy_)
            return;
        tool_clock_.start();
        tool_trace_ = addTrace("tool", name, detail);
        updateTrace(tool_trace_, "running", detail, 0);
        QStringList new_kinds;
        if (name == "office_new")
            new_kinds.append(
                arguments.value("kind") == "text" ? "writer" : arguments.value("kind").toString());
        const auto app_new_kind = [](const QJsonObject& step)
        {
            const QString op = step.value("op").toString();
            if (op != "app.new" && (step.value("module") != "app" || step.value("action") != "new"))
                return QString{};
            const auto args = step.value("args").toArray();
            return args.isEmpty() ? QString{} : args.first().toString();
        };
        if (name == "office_action")
            new_kinds.append(app_new_kind(arguments));
        if (name == "office_batch")
            for (const auto& value : arguments.value("steps").toArray())
                new_kinds.append(app_new_kind(value.toObject()));
        for (const auto& kind : new_kinds)
        {
            const auto guard = checkNewDeliverable(kind);
            if (!guard.value("ok").toBool())
            {
                acceptToolResult(guard);
                return;
            }
        }
        if (traits.asynchronous)
        {
            toolbox_.execute(name, arguments, [this, epoch, name](const QJsonObject& result)
            {
                if (epoch != run_.epoch())
                    return;
                document_key_ = toolbox_.documentKey();
                const bool reobserve = result.value("settled").toBool() || result.value("reobserve").toBool();
                skip_calls_ = skip_calls_ || reobserve;
                task_after_reobserve_ = result.value("ok").toBool() && reobserve;
                if (result.value("file").toObject().value("saved").toBool())
                {
                    saved_by_agent_ = true;
                    saved_module_ = result.value("file").toObject().value("module").toString();
                    completion_checks_ = 0;
                    post_save_observations_ = 0;
                }
                if (name.startsWith("office_compose") && result.value("ok").toBool())
                    document_work_ = true;
                acceptToolResult(result);
            });
            return;
        }
        acceptToolResult(runTool(name, arguments));
    }

    void OfficeAiAgent::acceptToolResult(const QJsonObject& raw_result)
    {
        if (!busy_ || run_.phase() != OfficeAiRun::Phase::Tools)
            return;
        const auto epoch = run_.epoch();
        const auto call = run_.current();
        const QJsonObject function{{"name", call.value("name")},
            {"arguments",
                QString::fromUtf8(
                    QJsonDocument(call.value("input").toObject()).toJson(QJsonDocument::Compact))}};
        auto result = raw_result;
        if (!result.value("ok").toBool())
            task_after_reobserve_ = false;
        const QString error = result.value("error").toString();
        const auto recovery = office_ai_recovery(error);
        if (!result.value("ok").toBool())
            result.insert("recoveryPolicy", recovery);
        const auto input = call.value("input").toObject();
        emit toolObserved(call.value("name").toString(), input, result);
        if (epoch != run_.epoch() || !busy_)
            return;
        diagnostic("tool",
            {{"name", function.value("name")}, {"ok", result.value("ok")}, {"error", result.value("error")},
                {"module", input.value("module").toString(input.value("op").toString().section('.', 0, 0))},
                {"action", input.value("action").toString(input.value("op").toString().section('.', 1))},
                {"schema", input.value("name").toString().left(128)}, {"group", input.value("group")},
                {"executed", result.value("executed")}, {"remaining", result.value("remaining")},
                {"callId", call.value("id")}, {"recovery", result.value("recoveryPolicy")},
                {"revision", result.value("revision")}, {"field", result.value("field")},
                {"stepIndex", result.value("stepIndex")}, {"elementIndex", result.value("elementIndex")},
                {"resultBytes", QJsonDocument(result).toJson(QJsonDocument::Compact).size()},
                {"elapsedMs", tool_clock_.isValid() ? tool_clock_.elapsed() : 0}});
        const QString outcome = result.value("ok").toBool()
            ? QStringLiteral("成功")
            : result.value("error").toString(QStringLiteral("调用未完成"));
        updateTrace(tool_trace_, result.value("ok").toBool() ? "done" : "error",
            function.value("arguments").toString() + "\n" + outcome + "\n" +
                QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)).left(4000),
            tool_clock_.isValid() ? tool_clock_.elapsed() : 0);
        tool_trace_ = -1;
        if (recovery.value("category") == "user_input")
        {
            stop_after_tools_ =
                QStringLiteral("请先完成当前输入或保存确认，然后发送继续；已完成的编辑保留。");
        }
        if (recovery.value("category") == "runtime_failure")
            stop_after_tools_ =
                QStringLiteral("程序未能确认操作完成，已停止自动重试；已完成的编辑保留。错误：") + error;
        context_.result(call, result);
        if (result.value("ok").toBool())
        {
            const auto runtime =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
            context_.reconcileSlideTarget(runtime.value("modules").toObject().value("slides").toObject());
        }
        const QString tool = call.value("name").toString();
        if (tool == "office_save" && result.value("file").toObject().value("saved").toBool())
            context_.notice("The file is saved. If it was the last requested deliverable, mark the "
                            "office_task checklist done with no remaining items and finish with the "
                            "saved path. Call office_home only to create another requested document.");
        if ((tool == "office_compose_slides" || tool == "office_continue_slides") &&
            result.value("ok").toBool())
            completion_checks_ = 0;
        auto classification = office_ai_tool_traits(tool, input);
        if (result.value("resolvedOperation").isObject())
            classification =
                office_ai_tool_traits("office_action", result.value("resolvedOperation").toObject());
        else if (tool == "office_batch" && result.value("results").isArray())
            classification = office_ai_tool_traits("office_batch", {{"steps", result.value("results")}});
        const bool observation = classification.observation;
        if (result.value("ok").toBool() && saved_by_agent_)
        {
            if (observation)
            {
                ++post_save_observations_;
                if (post_save_observations_ == 8)
                {
                    context_.notice("The last file is saved, and eight later observations produced no "
                                    "new edit. If every requested deliverable is complete, update "
                                    "office_task and finish with the saved path. Otherwise perform the "
                                    "specific remaining action; do not keep querying unchanged state.");
                    diagnostic("post_save_observation_streak", {{"count", post_save_observations_}});
                }
            }
            else if (tool != "office_task" || input.value("phase") == "done")
                post_save_observations_ = 0;
        }
        if (result.value("ok").toBool() && observation)
        {
            auto query = input;
            query.remove("expectedRevision");
            auto content = office_ai_compact_result(result);
            content.remove("revision");
            const auto state =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
            const QByteArray fingerprint = tool.toUtf8() + state.value("revision").toString().toUtf8() +
                QJsonDocument(query).toJson(QJsonDocument::Compact) +
                QJsonDocument(content).toJson(QJsonDocument::Compact);
            if (repeated_observations_.size() >= 128 && !repeated_observations_.contains(fingerprint))
                repeated_observations_.clear();
            if (++repeated_observations_[fingerprint] == 3)
            {
                addTrace("notice", QStringLiteral("重复查询提示，继续执行"), tool);
                context_.notice(
                    "This query has returned unchanged data three times at the same revision. "
                    "Reuse its result and execute the next unfinished edit, follow nextOffset, or finish if "
                    "the "
                    "requested work is verified. Reload a schema only if it is missing from context.");
                diagnostic("repeat_observation",
                    {{"name", tool},
                        {"module",
                            input.value("module").toString(input.value("op").toString().section('.', 0, 0))},
                        {"action",
                            input.value("action").toString(input.value("op").toString().section('.', 1))},
                        {"revision", state.value("revision")}});
            }
            if (repeated_observations_[fingerprint] >= 6)
                stop_after_tools_ = QStringLiteral(
                    "同一状态已被重复查询六次，已暂停以避免继续消耗 token；已完成的编辑保留，可发送继续。");
        }
        if (!result.value("ok").toBool() && result.value("error") != "not_executed_reobserve")
        {
            ++tool_errors_;
            auto args = input;
            args.remove("expectedRevision");
            QByteArray fingerprint = function.value("name").toString().toUtf8() +
                QJsonDocument(args).toJson(QJsonDocument::Compact) + error.toUtf8();
            if (recovery.value("sameStateBudget").toBool())
            {
                const auto workspace = office_ai_workspace(
                    QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object());
                // Changing generated text cannot repair an unchanged document precondition.
                const auto documents = workspace.value("documents").toObject();
                auto groups = toolbox_.groups().values();
                groups.sort();
                const QJsonObject conditions{{"documents", documents},
                    {"loadedGroups", QJsonArray::fromStringList(groups)},
                    {"requiredGroup", result.value("requiredGroup")}, {"module", args.value("module")}};
                fingerprint = error.toUtf8() + document_key_.toUtf8() +
                    QJsonDocument(conditions).toJson(QJsonDocument::Compact);
            }
            if (++repeated_failures_[fingerprint] >= 3)
            {
                stop_after_tools_ =
                    QStringLiteral("同一错误的前提条件仍未解决，已停止自动重试；已完成的编辑保留。错误：") +
                    error;
            }
        }
        skip_calls_ = skip_calls_ || !result.value("ok").toBool();
        if (!run_.resolve(call.value("id").toString()))
            return;
        scheduleNextTool();
    }

    QJsonObject OfficeAiAgent::runTool(const QString& name, const QJsonObject& arguments)
    {
        if (name == "office_groups")
            return office_ai_groups();
        if (name == "office_task")
            return context_.updateTask(arguments);
        return toolbox_.query(name, arguments);
    }
}
