#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace mirrorfly
{
    QJsonObject office_ai_compact_result(const QJsonObject& result);

    // Compact only complete exchanges. A checkpoint starts a fresh request conversation;
    // retained messages carry normalized calls and an opaque provider continuation.
    class OfficeAiContext
    {
    public:
        void begin(const QString& system, const QString& request);
        bool canAcceptUserUpdate(const QString& update) const;
        bool userUpdate(const QString& update);
        void setExecutionConstraints(const QJsonObject& constraints);
        void reviseSlideTarget(const QJsonObject& slides, int pages);
        void reconcileSlideTarget(const QJsonObject& slides);
        void recordMilestone(const QString& key, const QJsonObject& value);
        QJsonArray savedFiles() const;
        void focus(const QString& module);
        void assistant(const QJsonObject& message);
        void result(const QJsonObject& call, const QJsonObject& result);
        void notice(const QString& text);
        void interrupt();
        bool unfinished() const;
        QJsonObject updateTask(const QJsonObject& task);
        QString compactionReason() const;
        QJsonArray messages(const QJsonObject& workspace, bool compose_enabled = false);
        int compactions() const;

    private:
        void recordFormat(const QJsonObject& call, const QJsonObject& result);
        QJsonArray historicalFormats() const;
        QJsonObject currentMilestones(const QJsonObject& workspace) const;
        QJsonArray compose(const QJsonObject& workspace, bool compose_enabled) const;
        QJsonObject execution_constraints_;
        QString system_;
        QString request_;
        QString module_;
        bool focus_changed_ = false;
        bool latest_failed_ = false;
        QStringList human_updates_;
        QString notice_;
        QJsonArray replay_;
        QJsonArray reports_;
        QJsonArray applied_;
        QJsonObject totals_;
        QJsonObject references_;
        QJsonObject formats_;
        QStringList format_order_;
        QJsonObject task_;
        QJsonObject deck_plans_;
        QJsonObject milestones_;
        QStringList milestone_order_;
        QStringList deck_order_;
        QString compaction_reason_;
        QStringList reference_order_;
        int pending_ = 0;
        int rounds_ = 0;
        int compactions_ = 0;
    };
}
