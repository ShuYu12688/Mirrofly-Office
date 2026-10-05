#pragma once

#include <mirrorfly/presentation.hpp>

#include <array>
#include <string>
#include <vector>

namespace mirrorfly
{
    std::string fixed_group_tree();

    PresentationThemeDefinition authored_theme_definition(const std::array<std::string, 6>& palette);

    bool append_authored_theme_parts(
        std::vector<PresentationPart>& parts, const std::vector<std::array<std::string, 6>>& palettes);
}
