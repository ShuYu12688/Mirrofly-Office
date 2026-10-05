#include "office_ai_agent.hpp"
#include "automation_contract.hpp"
#include "office_ai_settings.hpp"
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
    OfficeAiAgent::OfficeAiAgent(QObject* parent, QNetworkAccessManager* transport, const QString& log_path,
        const QString& desktop_directory, OfficeAiSettings* settings)
        : QObject(parent), provider_(transport, this), log_(log_path), toolbox_(desktop_directory, this)
    {
        settings_ = settings;
        if (settings_ != nullptr)
        {
            if (!settings_->load())
                status_ = QStringLiteral("保存的模型配置无法读取，请重新配置。");
            else
            {
                const auto& configuration = settings_->configuration();
                if (provider_.configure(
                        configuration.address, configuration.model, configuration.key, configuration.effort))
                    status_ = QStringLiteral("已恢复模型配置。");
            }
        }
        retry_timer_.setSingleShot(true);
        connect(&retry_timer_, &QTimer::timeout, this, [this]()
        {
            if (busy_ && run_.phase() == OfficeAiRun::Phase::Ready)
                sendCompletion();
        });
        connect(&provider_, &OfficeAiProvider::completed, this, &OfficeAiAgent::completeModel);
        connect(&provider_, &OfficeAiProvider::probeCompleted, this, [this](const QString& status)
        {
            if (busy_ && request_kind_ == RequestKind::Probe)
                finish(status, {}, false, status.startsWith(QStringLiteral("连接成功")) ? "ready" : "error");
        });
        connect(&provider_, &OfficeAiProvider::progress, this,
            [this](const QString& text, qint64 elapsed, const QString& activity)
        {
            if (!busy_ || run_.phase() != OfficeAiRun::Phase::Model)
                return;
            const auto status = QStringLiteral("模型响应中 · %1 秒").arg(elapsed / 1000);
            if (answer_ == text && activity_ == activity && status_ == status)
                return;
            answer_ = text;
            activity_ = activity;
            status_ = status;
            emit stateChanged();
        });
        connect(&toolbox_, &OfficeAiToolbox::notice, this, [this](const QString& text)
        {
            context_.notice(text);
        });
        connect(&toolbox_, &OfficeAiToolbox::diagnostic, this, &OfficeAiAgent::diagnostic);
        connect(&toolbox_, &OfficeAiToolbox::milestone, this,
            [this](const QString& key, const QJsonObject& value)
        {
            context_.recordMilestone(key, value);
        });
        connect(&toolbox_, &OfficeAiToolbox::focused, this, [this](const QString& module)
        {
            context_.focus(module);
        });
        connect(&toolbox_, &OfficeAiToolbox::actionFinished, this,
            [this](const QString& module, const QString& action, const QJsonArray& args,
                const QJsonObject& result, qint64 elapsed)
        {
            if (result.value("ok").toBool())
                ++successful_actions_;
            else
                ++failed_actions_;
            if (result.value("progress") == "document_edit_accepted")
            {
                repeated_failures_.clear();
                document_work_ = true;
            }
            const auto* spec = office_action_spec(module, action);
            if (result.value("ok").toBool() && spec && result.value("progress") != "unchanged" &&
                (spec->effect == ActionEffect::Document || spec->effect == ActionEffect::Create))
                document_work_ = true;
            // Successful low-level steps belong to the enclosing tool receipt. Hundreds of
            // per-step rows force needless scrolling and text layout while a recipe is running.
            if (result.value("ok").toBool())
                return;
            const QString operation = (action == "applyEdit" || action == "execute") && !args.isEmpty()
                ? " / " + args.first().toString()
                : QString{};
            const int row = addTrace("office", module + "." + action + operation);
            updateTrace(row, result.value("ok").toBool() ? "done" : "error", result.value("error").toString(),
                elapsed);
        });
    }

    OfficeAiAgent::~OfficeAiAgent()
    {
        cancel();
    }

    bool OfficeAiAgent::configured() const
    {
        return provider_.configured();
    }

    bool OfficeAiAgent::busy() const
    {
        return busy_;
    }

    bool OfficeAiAgent::resumable() const
    {
        return resumable_;
    }

    QString OfficeAiAgent::activity() const
    {
        return activity_;
    }

    QString OfficeAiAgent::islandStatus() const
    {
        if (!configured())
            return QStringLiteral("先连接模型");
        if (activity_ == "completed")
            return QStringList{QStringLiteral("完成了"), QStringLiteral("搞定了"), QStringLiteral("做好了")}
                .at((completed_tasks_ - 1) % 3);
        if (!busy_)
            return activity_ == "error" ? QStringLiteral("这次未完成")
                : resumable_            ? QStringLiteral("已暂停，可继续")
                                        : QStringLiteral("随时可以开始");
        if (activity_ == "thinking")
            return QStringLiteral("思考中");
        if (activity_ == "responding" || activity_ == "planning_tools")
            return QStringLiteral("整理中");
        if (activity_ == "reading")
            return QStringLiteral("读取中");
        if (activity_ == "writing")
            return QStringLiteral("写入中");
        if (activity_ == "calculating")
            return QStringLiteral("计算中");
        if (activity_ == "saving")
            return QStringLiteral("保存中");
        if (activity_ == "retrying")
            return QStringLiteral("等待重连");
        return QStringLiteral("处理中");
    }

    QString OfficeAiAgent::status() const
    {
        return status_;
    }

    QString OfficeAiAgent::answer() const
    {
        return answer_;
    }

    QAbstractItemModel* OfficeAiAgent::trace()
    {
        return &trace_;
    }

    QString OfficeAiAgent::usageText() const
    {
        return QStringLiteral("本轮累计：输入 %1（缓存命中 %2）/ 输出 %3 token")
            .arg(input_tokens_)
            .arg(cached_tokens_)
            .arg(output_tokens_);
    }

    QString OfficeAiAgent::workspaceText() const
    {
        return workspace_text_;
    }

    QVariantMap OfficeAiAgent::usageStats() const
    {
        return {{"input", session_input_tokens_}, {"output", session_output_tokens_},
            {"cached", session_cached_tokens_}, {"total", session_input_tokens_ + session_output_tokens_}};
    }

    QString OfficeAiAgent::contextText() const
    {
        return QStringLiteral("本次请求 %1 KB · 上下文整理 %2 次")
            .arg(current_request_bytes_ / 1024.0, 0, 'f', 1)
            .arg(context_.compactions());
    }

    bool OfficeAiAgent::checkWorkspace()
    {
        const auto state = read_office_state();
        if (!office_ai_workspace(state).value("ok").toBool())
        {
            diagnostic("workspace_error", {{"error", "ui_state_unavailable"}});
            finish(QStringLiteral("工作区状态接口异常，无法确认当前文档；已停止请求，已完成的编辑保留。"), {},
                true);
            return false;
        }
        if (!document_key_.isEmpty() && !toolbox_.checkWorkspace())
        {
            finish(QStringLiteral("当前文档已切换，本轮已停止。请在目标文档中发送继续要求。"), {}, true);
            return false;
        }
        const QString module =
            state.value("ui").toObject().value("state").toObject().value("module").toString();
        const auto document =
            state.value("modules").toObject().value(module == "markdown" ? "text" : module).toObject();
        workspace_text_ = module + " · " + document.value("documentName").toString(QStringLiteral("首页"));
        return true;
    }

    int OfficeAiAgent::addTrace(const QString& kind, const QString& title, const QString& detail)
    {
        // Prune during long tasks and repeated resumes, keeping active row references valid.
        while (trace_.size() >= 500)
        {
            trace_.removeFirst();
            model_trace_ = qMax(-1, model_trace_ - 1);
            tool_trace_ = qMax(-1, tool_trace_ - 1);
        }
        const int index = static_cast<int>(trace_.size());
        trace_.append(
            QVariantMap{{"kind", kind}, {"title", title}, {"detail", detail.left(24000)}, {"state", "done"},
                {"elapsedMs", 0}, {"time", QDateTime::currentDateTime().toString("HH:mm:ss")}});
        emit stateChanged();
        return index;
    }

    void OfficeAiAgent::updateTrace(int index, const QString& state, const QString& detail, qint64 elapsed)
    {
        if (index < 0 || index >= trace_.size())
            return;
        auto row = trace_.at(index).toMap();
        row.insert("state", state);
        row.insert("detail", detail.left(24000));
        row.insert("elapsedMs", elapsed);
        trace_.update(index, row);
        emit stateChanged();
    }

}
