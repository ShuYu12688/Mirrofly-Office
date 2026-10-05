#include "word_cell_preservation.hpp"
#include "word_property_patch.hpp"
#include "word_xml.hpp"

#include <cmath>

namespace mirrorfly::word_detail
{
    using namespace word_xml;

    void patch_cell_style(pugi::xml_node raw, const mirrorfly::WordTableCell& before,
        const mirrorfly::WordTableCell& after, std::size_t row, const std::string& target_prefix, bool rtl)
    {
        pugi::xml_document temporary;
        auto next = temporary.append_child("w:tcPr");
        next.append_attribute("xmlns:w") = main_ns;
        if (before.background != after.background)
        {
            auto shading = next.append_child("w:shd");
            shading.append_attribute("w:val") = "clear";
            shading.append_attribute("w:fill") =
                after.background.empty() ? "auto" : after.background.substr(1).c_str();
        }
        if (before.vertical_alignment != after.vertical_alignment)
            next.append_child("w:vAlign").append_attribute("w:val") = after.vertical_alignment == 1 ? "center"
                : after.vertical_alignment == 2                                                     ? "bottom"
                                                                                                    : "top";
        if (before.margins != after.margins)
        {
            const char* edges[] = {"w:left", "w:top", "w:right", "w:bottom"};
            auto margins = next.append_child("w:tcMar");
            for (std::size_t edge = 0; edge < 4; ++edge)
            {
                auto value = margins.append_child(edges[edge]);
                value.append_attribute("w:w") = static_cast<int>(std::lround(after.margins[edge] * 20));
                value.append_attribute("w:type") = "dxa";
            }
            // Physical edge editing overrides competing logical edges in newer OOXML documents.
            auto target = child(child(raw, "tcPr"), "tcMar");
            target.remove_child(child(target, "start"));
            target.remove_child(child(target, "end"));
        }
        if (row < before.border_rows.size() && row < after.border_rows.size())
        {
            const char* names[] = {"w:left", "w:top", "w:right", "w:bottom"};
            pugi::xml_node borders;
            for (std::size_t edge = 0; edge < 4; ++edge)
            {
                if (before.border_rows[row][edge] == after.border_rows[row][edge])
                    continue;
                if (!borders)
                    borders = next.append_child("w:tcBorders");
                const auto& border = after.border_rows[row][edge];
                auto value = borders.append_child(names[edge]);
                value.append_attribute("w:val") = border.style.empty() ? "nil" : border.style.c_str();
                value.append_attribute("w:color") = border.color.substr(1).c_str();
                value.append_attribute("w:sz") = static_cast<int>(std::lround(border.width * 8));
                if (edge == 0 || edge == 2)
                {
                    // Override inherited logical edges as well as the physical side, without
                    // removing unknown attributes from an existing edge during local patching.
                    auto logical = borders.append_copy(value);
                    logical.set_name((edge == 0) != rtl ? "w:start" : "w:end");
                }
            }
        }
        patch_properties(raw, {}, next, "tcPr", target_prefix);
    }

}
