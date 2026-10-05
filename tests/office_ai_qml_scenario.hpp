#pragma once

#include "office_ai_test_transport.hpp"

namespace mirrorfly
{
    class OfficeAiAgent;
}

void run_office_ai_qml_scenario(mirrorfly::OfficeAiAgent& agent, office_ai_test::Transport& network,
    const QString& directory, const std::function<void(bool, const char*)>& check, bool batched = false);

void run_office_ai_multiformat_scenario(mirrorfly::OfficeAiAgent& agent, office_ai_test::Transport& network,
    const QString& directory, const std::function<void(bool, const char*)>& check);
