#include "office_ai_recovery.hpp"

#include <QStringList>

namespace mirrorfly
{
    QJsonObject office_ai_recovery(const QString& error)
    {
        if (error == "not_executed_reobserve")
            return {{"category", "not_executed"}, {"next", "observe_remaining"}};
        if (QStringList{"ui_state_unavailable", "state_unavailable", "navigation_not_completed",
                "save_not_completed", "pending_user_or_document", "asynchronous_action_failed",
                "export_failed", "workspace_changed"}
                .contains(error))
            return {{"category", "runtime_failure"}, {"next", "stop"}, {"retryAllowed", false}};
        if (error == "user_input_required" || error == "not_ready")
            return {{"category", "user_input"}, {"next", "wait_for_user"}, {"retryAllowed", false}};
        if (QStringList{"editable_slides_required", "inactive_module", "unsaved_changes",
                "new_presentation_only", "initial_page_already_edited", "stale_revision", "group_not_loaded",
                "already_home", "resume_observation_required", "batch_state_changed",
                "completed_batch_changed", "new_presentation_required", "selection_not_applied",
                "deliverable_already_saved"}
                .contains(error))
            return {{"category", "precondition"}, {"next", "observe_and_correct_precondition"},
                {"retryAllowed", false}, {"sameStateBudget", true}};
        if (error == "editable_copy_required" || error == "editable_document_required")
            return {{"category", "precondition"}, {"next", "create_editable_copy"}, {"retryAllowed", false},
                {"sameStateBudget", true}};
        return {{"category", "arguments_or_operation"}, {"next", "correct_arguments_or_inspect_failure"},
            {"retryAllowed", false}};
    }
}
