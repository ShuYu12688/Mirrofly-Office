#pragma once

#include <string>

namespace mirrorfly
{
    // One live AutomationBridge owns these process-wide entry points. Calls must use its GUI thread.
    std::string office_action_catalog();
    std::string office_snapshot();
    // Lightweight live status with the same revision/session identities, without document content trees.
    std::string office_runtime_snapshot();
    std::string office_execute(const std::string& json);
}
