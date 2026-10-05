#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace mirrorfly::detail
{
    inline constexpr std::array<const char*, 4> presentation_table_edge_names{
        "left", "top", "right", "bottom"};
    inline constexpr std::array<const char*, 4> presentation_table_edge_xml_names{"lnL", "lnT", "lnR", "lnB"};

    inline std::optional<std::size_t> presentation_table_edge_index(std::string_view name)
    {
        for (std::size_t index = 0; index < presentation_table_edge_names.size(); ++index)
            if (name == presentation_table_edge_names[index])
                return index;
        return std::nullopt;
    }

    inline bool presentation_table_edge_selected(std::string_view target, std::size_t index)
    {
        return target == "all" || target == presentation_table_edge_names[index];
    }
}
