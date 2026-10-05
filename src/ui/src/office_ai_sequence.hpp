#pragma once

#include <mirrorfly/office_ai.hpp>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>

namespace mirrorfly
{
    class OfficeAiSequence final : public QObject
    {
    public:
        using Snapshot = std::function<QJsonObject()>;
        using Execute = std::function<QJsonObject(const QJsonObject&)>;
        using Completion = std::function<void(const QJsonObject&)>;

        using Subscribe = std::function<OfficeChangeSubscription(OfficeChangeObserver)>;
        using Unsubscribe = std::function<bool(OfficeChangeSubscription)>;

        OfficeAiSequence(Snapshot snapshot, Execute execute, QObject* parent = nullptr,
            Subscribe subscribe = office_subscribe_changes,
            Unsubscribe unsubscribe = office_unsubscribe_changes);
        ~OfficeAiSequence() override;
        void start(const QJsonArray& steps, const QString& revision, Completion completion);
        void cancel();
        bool active() const;
        QJsonObject checkpoint() const;

    private:
        enum class Phase
        {
            Idle,
            Executing,
            WaitingForReady,
            WaitingForAction
        };

        void advance();
        void queueAdvance();
        void waitForState();
        void stopWatching();
        void finish(const QString& error = {});
        bool settling(const QJsonObject& snapshot) const;
        QString completionError(const QJsonObject& snapshot) const;

        Subscribe subscribe_;
        Unsubscribe unsubscribe_;
        OfficeChangeSubscription subscription_ = 0;
        std::uint64_t epoch_ = 0;
        bool advance_queued_ = false;
        Snapshot snapshot_;
        Execute execute_;
        Completion completion_;
        QTimer timer_;
        QElapsedTimer wait_;
        QElapsedTimer total_;
        qint64 execute_ms_ = 0;
        qint64 snapshot_ms_ = 0;
        int snapshot_count_ = 0;
        int event_wakes_ = 0;
        int fallback_wakes_ = 0;
        QJsonArray steps_;
        QJsonArray results_;
        QString revision_;
        QString pending_module_;
        QString pending_action_;
        QString previous_document_;
        QString destination_;
        int index_ = 0;
        Phase phase_ = Phase::Idle;
    };
}
