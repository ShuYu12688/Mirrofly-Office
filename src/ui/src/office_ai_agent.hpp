#pragma once

#include "office_ai_context.hpp"
#include "office_ai_log.hpp"
#include "office_ai_provider.hpp"
#include "office_ai_request.hpp"
#include "office_ai_run.hpp"
#include "office_ai_timeline.hpp"
#include "office_ai_toolbox.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

namespace mirrorfly
{
    class OfficeAiSettings;
    class OfficeAiAgent final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
        Q_PROPERTY(QString modelAddress READ modelAddress NOTIFY stateChanged)
        Q_PROPERTY(QString modelName READ modelName NOTIFY stateChanged)
        Q_PROPERTY(QString thinkingEffort READ thinkingEffort NOTIFY stateChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(bool resumable READ resumable NOTIFY stateChanged)
        Q_PROPERTY(QString status READ status NOTIFY stateChanged)
        Q_PROPERTY(QString activity READ activity NOTIFY stateChanged)
        Q_PROPERTY(QString islandStatus READ islandStatus NOTIFY stateChanged)
        Q_PROPERTY(QString answer READ answer NOTIFY stateChanged)
        Q_PROPERTY(QAbstractItemModel* trace READ trace CONSTANT)
        Q_PROPERTY(QString usageText READ usageText NOTIFY stateChanged)
        Q_PROPERTY(QVariantMap usageStats READ usageStats NOTIFY stateChanged)
        Q_PROPERTY(QString workspaceText READ workspaceText NOTIFY stateChanged)
        Q_PROPERTY(QString contextText READ contextText NOTIFY stateChanged)

    public:
        explicit OfficeAiAgent(QObject* parent = nullptr, QNetworkAccessManager* transport = nullptr,
            const QString& log_path = {}, const QString& desktop_directory = {},
            OfficeAiSettings* settings = nullptr);
        ~OfficeAiAgent() override;

        bool configured() const;
        QString modelAddress() const;
        QString modelName() const;
        QString thinkingEffort() const;
        Q_INVOKABLE bool setThinkingEffort(const QString& effort);
        bool busy() const;
        bool resumable() const;
        QString status() const;
        QString activity() const;
        QString islandStatus() const;
        QString answer() const;
        QAbstractItemModel* trace();
        QString usageText() const;
        QVariantMap usageStats() const;
        QString workspaceText() const;
        QString contextText() const;

        Q_INVOKABLE bool configure(
            const QString& base_url, const QString& model, const QString& key, const QString& effort);
        Q_INVOKABLE void testConnection();
        Q_INVOKABLE void start(const QString& prompt);
        Q_INVOKABLE void cancel();
        Q_INVOKABLE void resume();

    signals:
        void stateChanged();
        void modelObserved(const QString& text, const QJsonArray& calls);
        void toolObserved(const QString& name, const QJsonObject& input, const QJsonObject& result);

    private:
        enum class RequestKind
        {
            None,
            Probe,
            Completion
        };

        void sendCompletion();
        void completeTask(const OfficeAiModelReply& reply);
        void completeModel(const OfficeAiModelReply& reply);
        QJsonObject checkNewDeliverable(const QString& kind) const;
        bool checkWorkspace();
        void scheduleNextTool();
        void processNextTool();
        void acceptToolResult(const QJsonObject& result);
        QJsonObject runTool(const QString& name, const QJsonObject& arguments);
        int addTrace(const QString& kind, const QString& title, const QString& detail = {});
        void updateTrace(int index, const QString& state, const QString& detail, qint64 elapsed);
        void finish(const QString& status, const QString& answer = {}, bool retain_task = false,
            const QString& activity = QStringLiteral("error"));
        void retryModel(const OfficeAiModelReply& reply);
        void resumeWithUpdate(const QString& update);
        void beginTask(const QString& user_input, const QString& goal);
        void diagnostic(const QString& event, const QJsonObject& fields);

        OfficeAiProvider provider_;
        OfficeAiSettings* settings_ = nullptr;
        OfficeAiRun run_;
        OfficeAiLog log_;
        OfficeAiToolbox toolbox_;
        QString status_ = QStringLiteral("尚未配置");
        QString answer_;
        QString activity_ = QStringLiteral("ready");
        int completed_tasks_ = 0;
        OfficeAiTimeline trace_;
        OfficeAiContext context_;
        QString current_turn_;
        QString current_request_;
        OfficeAiTaskConstraints constraints_;
        int successful_actions_ = 0;
        int failed_actions_ = 0;
        qint64 input_tokens_ = 0;
        qint64 cached_tokens_ = 0;
        qint64 output_tokens_ = 0;
        qint64 session_input_tokens_ = 0;
        qint64 session_cached_tokens_ = 0;
        qint64 session_output_tokens_ = 0;
        qint64 peak_request_bytes_ = 0;
        qint64 current_request_bytes_ = 0;
        QString response_revision_;
        QHash<QByteArray, int> repeated_failures_;
        QHash<QByteArray, int> repeated_observations_;
        int tool_errors_ = 0;
        bool saved_by_agent_ = false;
        bool document_work_ = false;
        QString saved_module_;
        QElapsedTimer task_clock_;
        QElapsedTimer request_clock_;
        QElapsedTimer tool_clock_;
        int model_trace_ = -1;
        int tool_trace_ = -1;
        int truncation_retries_ = 0;
        int network_retries_ = 0;
        int protocol_retries_ = 0;
        int completion_checks_ = 0;
        int post_save_observations_ = 0;
        QTimer retry_timer_;
        bool resumable_ = false;
        QString paused_revision_;
        QString document_key_;
        QString workspace_text_;
        bool skip_calls_ = false;
        bool task_after_reobserve_ = false;
        QString stop_after_tools_;
        RequestKind request_kind_ = RequestKind::None;
        int turns_ = 0;
        bool busy_ = false;
    };
}
