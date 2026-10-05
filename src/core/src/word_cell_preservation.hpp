#pragma once

#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

namespace mirrorfly::word_detail
{
    void patch_cell_style(pugi::xml_node raw, const WordTableCell& before, const WordTableCell& after,
        std::size_t row, const std::string& target_prefix, bool rtl);
}
