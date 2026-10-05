#pragma once

#include <cstddef>
#include <functional>

namespace mirrorfly
{
    enum class OfficeSaveStage
    {
        Writing,
        Committing
    };

    // Observational only. Completion is reported after atomic commit, never before it.
    using OfficeSaveProgress = std::function<void(OfficeSaveStage, std::size_t, std::size_t)>;
}
