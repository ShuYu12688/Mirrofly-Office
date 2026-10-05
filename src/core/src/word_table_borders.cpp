#include "word_table_borders.hpp"
#include "word_style_merge.hpp"
#include "word_styles.hpp"
#include "word_xml.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace
{
    using namespace mirrorfly;
    using namespace mirrorfly::word_xml;

    WordBorder read_border(pugi::xml_node node, word_detail::StyleResolver& styles, bool cell)
    {
        WordBorder result;
        if (!node)
            return result;
        result.style = attribute(node, "val").value();
        result.cell_specific = cell;
        const std::string color = attribute(node, "color").value();
        if (color.size() == 6 && color.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
            result.color = "#" + color;
        if (const auto theme = styles.theme_color(node); !theme.empty())
            result.color = theme;
        const double width = attribute(node, "sz").as_double(4) / 8;
        result.width = std::isfinite(width) ? std::clamp(width, 0.0, 12.0) : 0.5;
        return result;
    }

    std::array<WordBorder, 6> read_edges(pugi::xml_node node, word_detail::StyleResolver& styles, bool cell,
        bool rtl, pugi::xml_node direct = {}, pugi::xml_node parent_direct = {})
    {
        const char* names[] = {"left", "top", "right", "bottom", "insideH", "insideV"};
        std::array<WordBorder, 6> result;
        for (std::size_t edge = 0; edge < result.size(); ++edge)
        {
            auto value = child(node, names[edge]);
            if (edge == 0 || edge == 2)
            {
                const auto* logical_name = (edge == 0) != rtl ? "start" : "end";
                const auto priority =
                    child(direct, names[edge]) || child(direct, logical_name) ? direct : parent_direct;
                if (const auto logical = child(node, logical_name))
                    if (!child(priority, names[edge]) || child(priority, logical_name))
                        value = logical;
            }
            result[edge] = read_border(value, styles, cell);
        }
        return result;
    }
}

namespace mirrorfly
{
    bool WordBorder::operator==(const WordBorder& other) const
    {
        return std::tie(style, color, width, cell_specific) ==
            std::tie(other.style, other.color, other.width, other.cell_specific);
    }

    bool WordBorder::operator!=(const WordBorder& other) const
    {
        return !(*this == other);
    }

    WordEditResult set_word_cell_border(WordDocument& document, std::size_t table_index,
        std::size_t cell_index, WordBorderEdge edge, WordBorder border)
    {
        const auto side = static_cast<std::size_t>(edge);
        if (table_index >= document.tables.size() ||
            cell_index >= document.tables[table_index].cells.size() || side >= 4)
            return {false, false, "表格、单元格或边框方向越界。"};
        const std::string styles[] = {"nil", "single", "double", "dotted", "dashed"};
        if (!word_detail::valid_border(border) ||
            std::find(std::begin(styles), std::end(styles), border.style) == std::end(styles) ||
            (border.style != "nil" && border.width < 0.25) ||
            std::abs(border.width * 8 - std::round(border.width * 8)) > 0.000001)
            return {false, false, "边框需要受支持线型、#RRGGBB颜色及0.25至12磅的八分之一磅刻度。"};
        auto& cell = document.tables[table_index].cells[cell_index];
        if (cell.border_rows.size() != cell.row_span || cell.border_rows.empty())
            return {false, false, "单元格缺少完整边框来源，未修改不确定区域。"};
        border.cell_specific = true;
        bool changed = false;
        for (std::size_t row = 0; row < cell.border_rows.size(); ++row)
        {
            if ((side == 1 && row != 0) || (side == 3 && row + 1 != cell.border_rows.size()))
                continue;
            auto& target = cell.border_rows[row][side];
            changed = changed || target != border;
            target = border;
        }
        return {true, changed, {}};
    }
}

namespace mirrorfly::word_detail
{
    bool valid_border(const WordBorder& border)
    {
        return border.style.size() <= 64 &&
            border.style.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") ==
            std::string::npos &&
            border.color.size() == 7 && border.color.front() == '#' &&
            border.color.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos &&
            std::isfinite(border.width) && border.width >= 0 && border.width <= 12;
    }

    void read_table_borders(WordTable& table, pugi::xml_node source, StyleResolver& styles)
    {
        pugi::xml_document table_xml;
        const auto properties = property_root(table_xml, "w:tblPr");
        styles.table_properties(source, properties);
        const bool rtl = enabled(child(properties, "bidiVisual"));
        table.right_to_left = rtl;
        table.border_layout_supported =
            !rtl && attribute(child(properties, "tblCellSpacing"), "w").as_double() == 0;
        table.borders = read_edges(
            child(properties, "tblBorders"), styles, false, rtl, child(child(source, "tblPr"), "tblBorders"));
        const auto& top = table.borders[1];
        table.border_color = top.color;
        table.border_width = top.style.empty() || top.style == "nil" || top.style == "none" ? 0 : top.width;
        const auto columns = table.column_widths.size();
        if (columns == 0 || columns > 256 || table.rows > 65536 / columns)
            return;
        std::vector<std::size_t> owners(table.rows * columns, table.cells.size());
        for (std::size_t index = 0; index < table.cells.size(); ++index)
        {
            auto& cell = table.cells[index];
            if (cell.row >= table.rows || cell.column >= columns || cell.row_span > table.rows - cell.row ||
                cell.column_span > columns - cell.column)
                continue;
            cell.border_rows.resize(cell.row_span);
            for (std::size_t row = cell.row; row < cell.row + cell.row_span; ++row)
                owners[row * columns + cell.column] = index;
        }
        std::size_t row_index = 0;
        for (auto row : source.children())
        {
            if (!named(row, "tr"))
                continue;
            std::size_t column = static_cast<std::size_t>(
                std::clamp(attribute(child(child(row, "trPr"), "gridBefore"), "val").as_int(), 0, 255));
            for (auto raw : row.children())
            {
                if (!named(raw, "tc"))
                    continue;
                pugi::xml_document cell_xml, inherited_xml;
                const auto format = property_root(cell_xml, "w:tcPr");
                styles.cell_properties(raw, format, false);
                const auto direct = child(raw, "tcPr");
                const auto span = static_cast<std::size_t>(
                    std::clamp(attribute(child(direct, "gridSpan"), "val").as_int(1), 1, 256));
                if (column < columns && row_index < table.rows)
                {
                    const auto owner = owners[row_index * columns + column];
                    if (owner < table.cells.size())
                    {
                        auto& cell = table.cells[owner];
                        auto& edges = cell.border_rows[row_index - cell.row];
                        const auto inherited = property_root(inherited_xml, "w:tblPr");
                        styles.cell_table_properties(raw, inherited);
                        if (attribute(child(inherited, "tblCellSpacing"), "w").as_double() != 0)
                            table.border_layout_supported = false;
                        for (const auto* diagonal : {"tl2br", "tr2bl"})
                        {
                            auto line = child(child(direct, "tcBorders"), diagonal);
                            if (!line || std::string_view(attribute(line, "val").value()) == "none")
                                line = child(child(format, "tcBorders"), diagonal);
                            const std::string_view kind = attribute(line, "val").value();
                            if (line && !kind.empty() && kind != "nil" && kind != "none")
                                table.border_layout_supported = false;
                        }
                        const auto defaults = read_edges(child(inherited, "tblBorders"), styles, false, rtl,
                            child(child(row, "tblPrEx"), "tblBorders"),
                            child(child(source, "tblPr"), "tblBorders"));
                        auto local = read_edges(child(format, "tcBorders"), styles, true, rtl);
                        const auto overrides = read_edges(child(direct, "tcBorders"), styles, true, rtl);
                        // Word treats direct none as absent and nil as explicit suppression.
                        for (std::size_t edge = 0; edge < local.size(); ++edge)
                            if (!overrides[edge].style.empty() && overrides[edge].style != "none")
                                local[edge] = overrides[edge];
                        const bool outside[] = {column == 0, row_index == 0, column + span == columns,
                            row_index + 1 == table.rows};
                        for (std::size_t edge = 0; edge < 4; ++edge)
                        {
                            const auto interior = edge == 0 || edge == 2 ? 5U : 4U;
                            edges[edge] = defaults[outside[edge] ? edge : interior];
                            if (!outside[edge] && !local[interior].style.empty() &&
                                local[interior].style != "none")
                                edges[edge] = local[interior];
                            if (!local[edge].style.empty() && local[edge].style != "none")
                                edges[edge] = local[edge];
                        }
                    }
                }
                column += span;
            }
            ++row_index;
        }
    }
}
