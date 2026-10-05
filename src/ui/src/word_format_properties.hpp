#pragma once

#include <QTextFormat>

namespace mirrorfly
{
    inline constexpr int word_paragraph_border_property = QTextFormat::UserProperty + 31;
    inline constexpr int word_character_border_property = QTextFormat::UserProperty + 33;
    // Other properties in 31–38 belong to the borders, annotations and distribution adapters.
    inline constexpr int word_source_paragraph_property = QTextFormat::UserProperty + 41;
    inline constexpr int word_source_run_property = QTextFormat::UserProperty + 42;
    inline constexpr int word_source_image_property = QTextFormat::UserProperty + 43;
    inline constexpr int word_source_table_property = QTextFormat::UserProperty + 44;
    inline constexpr int word_table_gap_property = QTextFormat::UserProperty + 45;
    inline constexpr int word_cell_borders_property = QTextFormat::UserProperty + 46;
    inline constexpr int word_cell_border_display_property = QTextFormat::UserProperty + 47;
    inline constexpr int word_table_border_layout_property = QTextFormat::UserProperty + 48;
    inline constexpr int word_double_underline_property = QTextFormat::UserProperty + 49;
    inline constexpr int word_double_strike_property = QTextFormat::UserProperty + 50;
    inline constexpr int word_list_instance_property = QTextFormat::UserProperty + 51;
    inline constexpr int word_list_marker_property = QTextFormat::UserProperty + 52;
    inline constexpr int word_list_text_property = QTextFormat::UserProperty + 53;
    inline constexpr int word_tabs_property = QTextFormat::UserProperty + 54;
}
