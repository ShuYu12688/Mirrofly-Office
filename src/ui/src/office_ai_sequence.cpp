#include "office_ai_sequence.hpp"
#include "automation_contract.hpp"
#include "document_path.hpp"
#include "office_ai_workspace.hpp"

#include <QLoggingCategory>
#include <QPointer>

#include <utility>

namespace
{
    Q_LOGGING_CATEGORY(aiSequenceLatency, "mirrorfly.ai.sequence.latency", QtInfoMsg)
}

namespace mirrorfly
{
    OfficeAiSequence::OfficeAiSequence(
        Snapshot snapshot, Execute execute, QObject* parent, Subscribe subscribe, Unsubscribe unsubscribe)
        : QObject(parent), timer_(this)
    {
        subscribe_ = std::move(subscribe);
        unsubscribe_ = std::move(unsubscribe);
        snapshot_ = std::move(snapshot);
        execute_ = std::move(execute);
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this]()
        {
            if (active())
                ++fallback_wakes_;
            queueAdvance();
        });
    }

    OfficeAiSequence::~OfficeAiSequence()
    {
        cancel();
    }

    void OfficeAiSequence::queueAdvance()
    {
        if (!active() || advance_queued_)
            return;
        advance_queued_ = true;
        const auto epoch = epoch_;
        QTimer::singleShot(0, this, [this, epoch]()
        {
            if (epoch != epoch_)
                return;
            advance_queued_ = false;
            advance();
        });
    }

    void OfficeAiSequence::waitForState()
    {
        // An invalidation is only a wake-up, never permission to refresh the batch revision.
        // Always keep a fallback: completion may precede subscription or notifications may coalesce.
        timer_.start(1000);
        if (!subscription_ && subscribe_)
        {
            const auto epoch = epoch_;
            const QPointer<OfficeAiSequence> guarded(this);
            subscription_ = subscribe_([guarded, epoch](const std::string&)
            {
                if (guarded && epoch == guarded->epoch_ &&
                    (guarded->phase_ == Phase::WaitingForReady || guarded->phase_ == Phase::WaitingForAction))
                {
                    if (!guarded->advance_queued_)
                        ++guarded->event_wakes_;
                    guarded->queueAdvance();
                }
            });
        }
    }

    void OfficeAiSequence::stopWatching()
    {
        timer_.stop();
        const auto subscription = std::exchange(subscription_, 0);
        if (subscription && unsubscribe_)
            unsubscribe_(subscription);
    }

    bool OfficeAiSequence::active() const
    {
        return phase_ != Phase::Idle;
    }

    QJsonObject OfficeAiSequence::checkpoint() const
    {
        return {{"ok", false}, {"error", "interrupted"}, {"results", results_}, {"executed", index_},
            {"remaining", steps_.size() - index_}, {"nextStep", index_}, {"revision", revision_}};
    }

    void OfficeAiSequence::start(const QJsonArray& steps, const QString& revision, Completion completion)
    {
        cancel();
        steps_ = steps;
        results_ = {};
        revision_ = revision;
        completion_ = std::move(completion);
        index_ = 0;
        pending_module_.clear();
        pending_action_.clear();
        previous_document_.clear();
        destination_.clear();
        total_.start();
        execute_ms_ = 0;
        snapshot_ms_ = 0;
        snapshot_count_ = 0;
        event_wakes_ = 0;
        fallback_wakes_ = 0;
        phase_ = Phase::Executing;
        // Validate the complete batch before any document change.
        for (const auto& step : steps_)
        {
            const auto object = step.toObject();
            if (!step.isObject() || object.size() != 3 || !object.value("module").isString() ||
                !object.value("action").isString() || !object.value("args").isArray())
            {
                finish("invalid_batch");
                return;
            }
        }
        if (steps_.isEmpty() || revision_.isEmpty())
        {
            finish("invalid_batch");
            return;
        }
        queueAdvance();
    }

    bool OfficeAiSequence::settling(const QJsonObject& snapshot) const
    {
        const auto module = snapshot.value("modules").toObject().value(pending_module_).toObject();
        const auto detail = module.value("snapshot").toObject();
        return module.value("busy").toBool() || module.value("locked").toBool() ||
            module.value("loading").toBool() || module.value("syncing").toBool() ||
            module.value("pendingEdits").toInt() > 0 || detail.value("syncing").toBool() ||
            detail.value("pendingEdits").toInt() > 0 ||
            !snapshot.value("ui").toObject().value("ready").toBool();
    }

    QString OfficeAiSequence::completionError(const QJsonObject& snapshot) const
    {
        const auto module = snapshot.value("modules").toObject().value(pending_module_).toObject();
        QString location =
            snapshot.value("ui").toObject().value("state").toObject().value("module").toString();
        if (location == "markdown")
            location = "text";
        const auto* spec = office_action_spec(
            pending_action_ == "new" || pending_action_ == "open" ? "app" : pending_module_, pending_action_);
        if (spec && (spec->effect == ActionEffect::Create || spec->effect == ActionEffect::Navigation) &&
            (pending_action_ == "new" || pending_action_ == "open") &&
            (location != pending_module_ || !module.value("active").toBool() ||
                module.value("documentSession").toString().isEmpty() ||
                module.value("documentSession").toString() == previous_document_))
            return "navigation_not_completed";
        if ((pending_action_ == "showHome" || pending_action_ == "requestHome") && location != "home")
            return "navigation_not_completed";
        if (spec && spec->effect == ActionEffect::Save)
        {
            const QString path =
                module.value("documentPath").toString(QUrl(module.value("saveUrl").toString()).toLocalFile());
            if (module.value("modified").toBool() || !QFileInfo(path).isFile() ||
                (!destination_.isEmpty() && !same_document_path(path, destination_)))
                return "save_not_completed";
            bool copy_unavailable = module.value("readOnly").toBool();
            if (pending_module_ == "slides")
                copy_unavailable = !module.value("editable").toBool();
            if (pending_action_ == "createEditableCopyTo" && copy_unavailable)
                return "editable_copy_not_completed";
        }
        return {};
    }

    void OfficeAiSequence::advance()
    {
        if (!active())
            return;
        const auto epoch = epoch_;
        QElapsedTimer clock;
        clock.start();
        const auto state = snapshot_();
        ++snapshot_count_;
        if (epoch != epoch_ || !active())
            return;
        snapshot_ms_ += clock.elapsed();
        if (!state.value("ok").toBool())
        {
            finish("state_unavailable");
            return;
        }
        const auto ui = state.value("ui").toObject().value("state").toObject();
        if (ui.value("pendingInput").toBool() || !ui.value("blockers").toArray().isEmpty())
        {
            finish("user_input_required");
            return;
        }
        if (phase_ == Phase::WaitingForReady)
        {
            if (settling(state))
            {
                if (wait_.elapsed() >= 60000)
                    finish("pending_user_or_document");
                else
                    waitForState();
                return;
            }
            stopWatching();
            phase_ = Phase::Executing;
        }
        if (phase_ == Phase::WaitingForAction)
        {
            const auto module = state.value("modules").toObject().value(pending_module_).toObject();
            if (!module.value("error").toString().isEmpty())
            {
                finish("asynchronous_action_failed");
                return;
            }
            if (settling(state))
            {
                if (wait_.elapsed() >= 60000)
                    finish("pending_user_or_document");
                else
                    waitForState();
                return;
            }
            const auto detail = module.value("snapshot").toObject();
            if ((pending_module_ == "export" || pending_module_ == "images") &&
                !detail.value("success").toBool())
            {
                finish("export_failed");
                return;
            }
            // Async completion changes global revision. Re-plan remaining steps instead of hiding
            // concurrent changes by blindly substituting the newest revision.
            revision_ = state.value("revision").toString();
            finish(completionError(state));
            return;
        }
        if (index_ == steps_.size())
        {
            // All requested edits already succeeded. Never report them as failed on a later UI signal.
            finish();
            return;
        }
        if (state.value("revision").toString() != revision_)
        {
            finish("stale_revision");
            return;
        }
        auto step = steps_.at(index_).toObject();
        pending_module_ = step.value("module").toString();
        pending_action_ = step.value("action").toString();
        if (settling(state))
        {
            phase_ = Phase::WaitingForReady;
            wait_.start();
            waitForState();
            return;
        }
        if (pending_module_ == "app" && (pending_action_ == "new" || pending_action_ == "open"))
        {
            QString target = step.value("args").toArray().first().toString();
            if (pending_action_ == "open")
                target = office_ai_module_for_file(target);
            if (target == "writer" || target == "markdown")
                target = "text";
            const auto destination = state.value("modules").toObject().value(target).toObject();
            previous_document_ = destination.value("documentSession").toString();
        }
        destination_ = pending_action_ == "saveTo" || pending_action_ == "createEditableCopyTo"
            ? QUrl(step.value("args").toArray().first().toString()).toLocalFile()
            : QString{};
        if (pending_action_ == "save")
        {
            const auto document = state.value("modules").toObject().value(pending_module_).toObject();
            destination_ = document.value("documentPath").toString();
        }
        step.insert("expectedRevision", revision_);
        clock.restart();
        const auto result = execute_(step);
        execute_ms_ += clock.elapsed();
        if (epoch != epoch_ || !active())
            return;
        results_.append(QJsonObject{{"index", index_}, {"module", step.value("module")},
            {"action", step.value("action")}, {"response", result}});
        if (!result.value("ok").toBool())
        {
            finish(result.value("error").toString("action_failed"));
            return;
        }
        ++index_;
        revision_ = result.value("revision").toString();
        const bool pending = result.value("result").toObject().value("status").toString() == "pending";
        if (pending_module_ == "app" && (pending_action_ == "new" || pending_action_ == "open"))
        {
            pending_module_ = step.value("args").toArray().first().toString();
            if (pending_action_ == "open")
                pending_module_ = office_ai_module_for_file(pending_module_);
            if (pending_module_ == "writer" || pending_module_ == "markdown")
                pending_module_ = "text";
        }
        clock.restart();
        const auto after = snapshot_();
        ++snapshot_count_;
        if (epoch != epoch_ || !active())
            return;
        snapshot_ms_ += clock.elapsed();
        if (!after.value("ok").toBool())
        {
            finish("state_unavailable");
            return;
        }
        if (pending || settling(after))
        {
            phase_ = Phase::WaitingForAction;
            wait_.start();
            waitForState();
            return;
        }
        const QString error = completionError(after);
        if (!error.isEmpty())
        {
            finish(error);
            return;
        }
        // Yield between operations so the stop button and state changes can be processed.
        queueAdvance();
    }

    void OfficeAiSequence::finish(const QString& error)
    {
        const bool waiting = phase_ == Phase::WaitingForAction;
        const bool delayed = waiting || phase_ == Phase::WaitingForReady;
        phase_ = Phase::Idle;
        ++epoch_;
        advance_queued_ = false;
        timer_.stop();
        stopWatching();
        if (total_.elapsed() >= 100)
            qCDebug(aiSequenceLatency)
                << pending_module_ << pending_action_ << "totalMs" << total_.elapsed() << "executeMs"
                << execute_ms_ << "snapshotMs" << snapshot_ms_ << "snapshots" << snapshot_count_
                << "eventWakes" << event_wakes_ << "fallbackWakes" << fallback_wakes_ << "executed" << index_;
        auto completion = std::move(completion_);
        if (completion)
            completion(QJsonObject{{"ok", error.isEmpty()}, {"error", error}, {"results", results_},
                {"executed", index_}, {"attempted", results_.size()}, {"remaining", steps_.size() - index_},
                {"nextStep", index_}, {"atomic", false}, {"revision", revision_},
                {"reobserve", waiting && index_ < steps_.size()}, {"settled", waiting && error.isEmpty()},
                {"timing",
                    QJsonObject{{"totalMs", total_.elapsed()}, {"executeMs", execute_ms_},
                        {"snapshotMs", snapshot_ms_}, {"snapshots", snapshot_count_},
                        {"eventWakes", event_wakes_}, {"fallbackWakes", fallback_wakes_},
                        {"waitMs", delayed ? wait_.elapsed() : 0}}}});
    }

    void OfficeAiSequence::cancel()
    {
        phase_ = Phase::Idle;
        ++epoch_;
        advance_queued_ = false;
        timer_.stop();
        stopWatching();
        completion_ = {};
    }
}
