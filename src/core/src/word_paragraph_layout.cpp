#include "word_paragraph_layout.hpp"
#include "word_style_merge.hpp"
#include "word_tabs.hpp"
#include "word_xml.hpp"
#include <algorithm>
namespace mirrorfly::word_detail
{
    void read_paragraph_layout(pugi::xml_node properties, WordParagraph& paragraph)
    {
        using namespace word_xml;
        const auto outline = attribute(child(properties, "outlineLvl"), "val");
        if (outline && outline.as_int(9) >= 0 && outline.as_int(9) < 3)
        {
            paragraph.heading = outline.as_int() + 1;
        }
        const std::string_view align = attribute(child(properties, "jc"), "val").value();
        paragraph.right_to_left = enabled(child(properties, "bidi"));
        paragraph.alignment = align == "distribute" ? 4
            : align == "center"                     ? 1
                                                    : (align == "right" ? 2 : (align == "both" ? 3 : 0));
        const auto spacing = child(properties, "spacing");
        const std::string_view rule = attribute(spacing, "lineRule").value();
        paragraph.line_spacing_rule = rule == "exact" ? 1 : rule == "atLeast" ? 2 : 0;
        if (paragraph.line_spacing_rule)
            paragraph.line_spacing_points =
                std::clamp(attribute(spacing, "line").as_double(240) / 20, 1.0, 144.0);
        else if (attribute(spacing, "line"))
        {
            paragraph.line_spacing = std::clamp(attribute(spacing, "line").as_double(360) / 240, 1.0, 2.0);
        }
        paragraph.space_before =
            std::clamp(attribute(spacing, "before").as_double(0) / 20, 0.0, maximum_word_spacing_points);
        paragraph.space_after =
            std::clamp(attribute(spacing, "after").as_double(paragraph.space_after * 20) / 20, 0.0,
                maximum_word_spacing_points);
        const auto indentation = child(properties, "ind");
        auto left = attribute(indentation, "left");
        if (!left)
        {
            left = attribute(indentation, "start");
        }
        paragraph.left_indent = std::clamp(left.as_double(0) / 20, 0.0, maximum_word_indent_points);
        auto right = attribute(indentation, "right");
        if (!right)
            right = attribute(indentation, "end");
        paragraph.right_indent = std::clamp(right.as_double(0) / 20, 0.0, maximum_word_indent_points);
        if (const auto first_line = attribute(indentation, "firstLine"))
        {
            paragraph.first_line_indent = std::clamp(first_line.as_double(0) / 20, -144.0, 144.0);
        }
        else if (const auto hanging = attribute(indentation, "hanging"))
        {
            paragraph.first_line_indent = -std::clamp(hanging.as_double(0) / 20, 0.0, 144.0);
        }
        paragraph.tabs = read_tab_stops(child(properties, "tabs"));
    }
}
