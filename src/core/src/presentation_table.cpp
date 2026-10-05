#include "presentation_table.hpp"
#include "presentation_table_edges.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <numeric>

namespace
{
    using Node = pugi::xml_node;
    std::string local(const char* name)
    {
        const auto colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    Node child(Node parent, const std::string& name)
    {
        for (auto item : parent.children())
            if (local(item.name()) == name)
                return item;
        return {};
    }

    double number(pugi::xml_attribute attribute, double fallback = 0)
    {
        if (!attribute)
            return fallback;
        const char* text = attribute.value();
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        return end != text && *end == 0 && std::isfinite(value) && std::abs(value) < 1e12 ? value : fallback;
    }

    Node fill(Node parent)
    {
        for (auto item : parent.children())
        {
            const auto name = local(item.name());
            if (name == "solidFill" || name == "gradFill" || name == "pattFill" || name == "noFill" ||
                name == "blipFill")
                return item;
        }
        return {};
    }

    mirrorfly::PresentationShape cell_shape(
        const mirrorfly::PresentationShape& frame, double x, double y, double width, double height)
    {
        mirrorfly::PresentationShape shape;
        shape.editable = false;
        shape.source_part = frame.source_part;
        shape.source_groups = {frame.source_id};
        shape.width = width;
        shape.height = height;
        shape.transform = frame.transform;
        shape.transform[4] += frame.transform[0] * x + frame.transform[2] * y;
        shape.transform[5] += frame.transform[1] * x + frame.transform[3] * y;
        return shape;
    }

    struct Border
    {
        std::string color;
        double opacity = 1;
        double width = 1;
    };

    void apply_border(Border& border, Node node, const mirrorfly::PresentationTableReader& reader)
    {
        if (!node)
            return;
        if (child(node, "ln"))
            node = child(node, "ln");
        if (const auto source = fill(node))
        {
            const auto value = reader.fill(source);
            border.color = value.color;
            border.opacity = value.opacity;
        }
        if (node.attribute("w"))
            border.width = std::clamp(number(node.attribute("w")) / 12700, 0.0, 72.0);
        if (const auto reference = child(node, "lnRef"))
        {
            const auto value = reader.color(reference);
            border.color = value.first;
            border.opacity = value.second;
        }
    }
}

namespace mirrorfly
{
    bool presentation_table_cell_has_text(Node cell)
    {
        std::vector<Node> pending{cell};
        while (!pending.empty())
        {
            const auto node = pending.back();
            pending.pop_back();
            if (local(node.name()) == "t" && *node.text().as_string())
                return true;
            for (auto item : node.children())
                pending.push_back(item);
        }
        return false;
    }

    PresentationTableCellGrid presentation_table_cell_grid(Node table, std::size_t column_count)
    {
        PresentationTableCellGrid grid;
        if (!table || column_count == 0 || column_count > 256)
            return grid;
        for (auto row : table.children())
        {
            if (local(row.name()) != "tr")
                continue;
            if (grid.size() >= 512 || grid.size() >= 2000 / column_count)
                return {};
            std::vector<Node> cells;
            for (auto cell : row.children())
                if (local(cell.name()) == "tc")
                    cells.push_back(cell);
            if (cells.size() != column_count)
                return {};
            grid.push_back(std::move(cells));
        }
        return grid;
    }

    std::optional<PresentationTableMerge> presentation_table_merge(
        const PresentationTableCellGrid& grid, std::size_t row, std::size_t column)
    {
        if (row >= grid.size() || column >= grid[row].size())
            return std::nullopt;
        const auto origin = grid[row][column];
        const auto column_attribute = origin.attribute("gridSpan");
        const auto row_attribute = origin.attribute("rowSpan");
        const int columns = column_attribute ? column_attribute.as_int(0) : 1;
        const int rows = row_attribute ? row_attribute.as_int(0) : 1;
        if (columns < 1 || rows < 1 || (columns == 1 && rows == 1) ||
            static_cast<std::size_t>(columns) > grid[row].size() - column ||
            static_cast<std::size_t>(rows) > grid.size() - row || origin.attribute("hMerge").as_bool() ||
            origin.attribute("vMerge").as_bool())
            return std::nullopt;
        PresentationTableMerge merge;
        merge.row_span = static_cast<std::size_t>(rows);
        merge.column_span = static_cast<std::size_t>(columns);
        merge.cells.reserve(merge.row_span * merge.column_span);
        for (std::size_t dy = 0; dy < merge.row_span; ++dy)
        {
            for (std::size_t dx = 0; dx < merge.column_span; ++dx)
            {
                const auto cell = grid[row + dy][column + dx];
                const auto grid_span = cell.attribute("gridSpan");
                const auto row_span = cell.attribute("rowSpan");
                if ((grid_span ? grid_span.as_int(0) : 1) != (dx == 0 ? columns : 1) ||
                    (row_span ? row_span.as_int(0) : 1) != (dy == 0 ? rows : 1) ||
                    cell.attribute("hMerge").as_bool() != (dx > 0) ||
                    cell.attribute("vMerge").as_bool() != (dy > 0) ||
                    ((dx > 0 || dy > 0) && presentation_table_cell_has_text(cell)))
                    return std::nullopt;
                merge.cells.push_back(cell);
            }
        }
        return merge;
    }

    std::vector<PresentationShape> presentation_table_shapes(Node table, const PresentationShape& frame,
        Node style_list, const PresentationTableReader& reader, std::vector<std::string>& warnings)
    {
        std::vector<PresentationShape> shapes;
        std::vector<PresentationShape> borders;
        std::vector<double> widths;
        std::vector<double> heights;
        std::vector<Node> rows;
        for (auto item : child(table, "tblGrid").children())
            if (local(item.name()) == "gridCol")
                widths.push_back(std::max(0.0, number(item.attribute("w"))));
        for (auto row : table.children())
            if (local(row.name()) == "tr")
            {
                rows.push_back(row);
                heights.push_back(std::max(0.0, number(row.attribute("h"))));
            }
        bool has_merges = false;
        for (auto row : rows)
            for (auto cell : row.children())
                if (local(cell.name()) == "tc")
                    has_merges = has_merges || cell.attribute("gridSpan").as_int(1) != 1 ||
                        cell.attribute("rowSpan").as_int(1) != 1 || cell.attribute("hMerge").as_bool() ||
                        cell.attribute("vMerge").as_bool();
        const double total_width = std::accumulate(widths.begin(), widths.end(), 0.0);
        const double total_height = std::accumulate(heights.begin(), heights.end(), 0.0);
        if (widths.empty() || rows.empty() || widths.size() > 256 || rows.size() > 512 ||
            rows.size() * widths.size() > 2000 || total_width <= 0 || total_height <= 0)
        {
            warnings.push_back("表格尺寸无效或超过 2000 格显示上限；原始对象保留。");
            return shapes;
        }
        const auto merge_grid = presentation_table_cell_grid(table, widths.size());
        const auto structure = presentation_table_structure(merge_grid);
        for (auto& width : widths)
            width *= frame.width / total_width;
        for (auto& height : heights)
            height *= frame.height / total_height;
        const auto properties = child(table, "tblPr");
        const PresentationTableStyleOptions style_options{properties.attribute("firstRow").as_bool(),
            properties.attribute("lastRow").as_bool(), properties.attribute("firstCol").as_bool(),
            properties.attribute("lastCol").as_bool(), properties.attribute("bandRow").as_bool(),
            properties.attribute("bandCol").as_bool()};
        const std::string style_id = child(properties, "tableStyleId").text().as_string();
        Node style = child(properties, "tableStyle");
        if (!style && !style_id.empty())
            for (auto item : style_list.children())
                if (std::string(item.attribute("styleId").value()) == style_id)
                    style = item;
        const bool fallback = !style_id.empty() && !style;
        if (fallback)
            warnings.push_back("内置表格样式使用主题近似配色；原始样式 ID 保留。");
        pugi::xml_document fallback_style;
        auto fallback_header = fallback_style.append_child("firstRow");
        auto fallback_text = fallback_header.append_child("tcTxStyle");
        fallback_text.append_attribute("b") = "1";
        fallback_text.append_child("srgbClr").append_attribute("val") = "FFFFFF";
        std::vector<bool> covered(widths.size() * rows.size(), false);
        double y = 0;
        for (std::size_t row = 0; row < rows.size(); ++row)
        {
            std::vector<Node> cells;
            for (auto cell : rows[row].children())
                if (local(cell.name()) == "tc")
                    cells.push_back(cell);
            const bool explicit_continuations = cells.size() >= widths.size();
            std::size_t column = 0;
            double x = 0;
            std::size_t xml_cell = 0;
            for (auto cell : cells)
            {
                if (column >= widths.size())
                    break;
                const auto span = static_cast<std::size_t>(std::clamp(
                    number(cell.attribute("gridSpan"), 1), 1.0, static_cast<double>(widths.size() - column)));
                const auto row_span = static_cast<std::size_t>(std::clamp(
                    number(cell.attribute("rowSpan"), 1), 1.0, static_cast<double>(rows.size() - row)));
                const auto step = explicit_continuations ? 1 : span;
                const double width =
                    std::accumulate(widths.begin() + column, widths.begin() + column + span, 0.0);
                const double height =
                    std::accumulate(heights.begin() + row, heights.begin() + row + row_span, 0.0);
                const bool continuation = cell.attribute("hMerge").as_bool() ||
                    cell.attribute("vMerge").as_bool() || covered[row * widths.size() + column];
                if (!continuation && width > 0 && height > 0)
                {
                    for (std::size_t dy = 0; dy < row_span; ++dy)
                        for (std::size_t dx = 0; dx < span; ++dx)
                            covered[(row + dy) * widths.size() + column + dx] = true;
                    std::vector<Node> regions{child(style, "wholeTbl")};
                    const bool first_row = row == 0 && properties.attribute("firstRow").as_bool();
                    const bool last_row =
                        row + row_span == rows.size() && properties.attribute("lastRow").as_bool();
                    const bool first_column = column == 0 && properties.attribute("firstCol").as_bool();
                    const bool last_column =
                        column + span == widths.size() && properties.attribute("lastCol").as_bool();
                    if (properties.attribute("bandRow").as_bool())
                    {
                        const auto stripe =
                            row - (properties.attribute("firstRow").as_bool() && row > 0 ? 1 : 0);
                        regions.push_back(child(style, stripe % 2 == 0 ? "band1H" : "band2H"));
                    }
                    if (properties.attribute("bandCol").as_bool())
                    {
                        const auto stripe =
                            column - (properties.attribute("firstCol").as_bool() && column > 0 ? 1 : 0);
                        regions.push_back(child(style, stripe % 2 == 0 ? "band1V" : "band2V"));
                    }
                    if (first_column)
                        regions.push_back(child(style, "firstCol"));
                    if (last_column)
                        regions.push_back(child(style, "lastCol"));
                    if (first_row)
                        regions.push_back(child(style, "firstRow"));
                    if (last_row)
                        regions.push_back(child(style, "lastRow"));
                    if (first_row && first_column)
                        regions.push_back(child(style, "nwCell"));
                    if (first_row && last_column)
                        regions.push_back(child(style, "neCell"));
                    if (last_row && first_column)
                        regions.push_back(child(style, "swCell"));
                    if (last_row && last_column)
                        regions.push_back(child(style, "seCell"));
                    if (fallback && first_row)
                        regions.push_back(fallback_header);
                    auto shape = cell_shape(frame, x, y, width, height);
                    shape.editable = frame.editable;
                    shape.table_cell = PresentationTableCell{
                        frame.source_id, row, column, xml_cell, rows.size(), widths.size(), has_merges};
                    shape.table_cell->style_available = static_cast<bool>(style) || !style_id.empty();
                    shape.table_cell->style_options = style_options;
                    shape.table_cell->structure_options = structure.options(row, column);
                    shape.table_cell->row_span = row_span;
                    shape.table_cell->column_span = span;
                    shape.table_cell->unmergeable =
                        presentation_table_merge(merge_grid, row, column).has_value();
                    shape.name = "表格 " + std::to_string(row + 1) + "," + std::to_string(column + 1);
                    shape.source_id =
                        frame.source_id + ":cell:" + std::to_string(row) + ":" + std::to_string(column);
                    shape.fill.color = "#FFFFFF";
                    if (fallback && first_row)
                        shape.fill = reader.header_fill;
                    else if (fallback && properties.attribute("bandRow").as_bool() && row % 2 == 1)
                        shape.fill = reader.band_fill;
                    std::array<Border, 4> edge;
                    for (auto& item : edge)
                        item.color = fallback ? "#FFFFFF" : "";
                    std::vector<Node> text_styles;
                    for (auto region : regions)
                    {
                        const auto cell_style = child(region, "tcStyle");
                        auto fill_node = fill(child(cell_style, "fill"));
                        if (!fill_node)
                            fill_node = fill(cell_style);
                        if (fill_node)
                            shape.fill = reader.fill(fill_node);
                        text_styles.push_back(child(region, "tcTxStyle"));
                        for (std::size_t index = 0; index < edge.size(); ++index)
                        {
                            const auto lines = child(cell_style, "tcBdr");
                            const bool internal = (index == 0 && column > 0) || (index == 1 && row > 0) ||
                                (index == 2 && column + span < widths.size()) ||
                                (index == 3 && row + row_span < rows.size());
                            if (local(region.name()) == "wholeTbl" && internal)
                                apply_border(edge[index],
                                    child(lines, index == 0 || index == 2 ? "insideV" : "insideH"), reader);
                            else
                                apply_border(edge[index],
                                    child(lines, detail::presentation_table_edge_names[index]), reader);
                        }
                    }
                    const auto cell_properties = child(cell, "tcPr");
                    shape.table_cell->inherited_fill = shape.fill;
                    shape.table_cell->local_fill_override = static_cast<bool>(fill(cell_properties));
                    shape.table_cell->local_border_override =
                        std::any_of(detail::presentation_table_edge_xml_names.begin(),
                            detail::presentation_table_edge_xml_names.end(), [&](const char* name)
                    {
                        return static_cast<bool>(child(cell_properties, name));
                    });
                    if (const auto fill_node = fill(cell_properties))
                        shape.fill = reader.fill(fill_node);
                    shape.text = reader.text(cell, text_styles);
                    const std::pair<const char*, double*> margins[]{{"marL", &shape.text.inset_left},
                        {"marR", &shape.text.inset_right}, {"marT", &shape.text.inset_top},
                        {"marB", &shape.text.inset_bottom}};
                    for (const auto& margin : margins)
                        if (cell_properties.attribute(margin.first))
                            *margin.second = std::clamp(
                                number(cell_properties.attribute(margin.first)) / 12700, 0.0, 1000.0);
                    const std::string anchor = cell_properties.attribute("anchor").value();
                    if (!anchor.empty())
                        shape.text.vertical_alignment =
                            anchor == "ctr" ? "center" : (anchor == "b" ? "bottom" : "top");
                    if (cell_properties.attribute("vert"))
                        shape.text.vertical = cell_properties.attribute("vert").value();
                    for (std::size_t index = 0; index < edge.size(); ++index)
                    {
                        apply_border(edge[index],
                            child(cell_properties, detail::presentation_table_edge_xml_names[index]), reader);
                        shape.table_cell->border_edges[index] = {edge[index].color, edge[index].opacity,
                            edge[index].width,
                            static_cast<bool>(
                                child(cell_properties, detail::presentation_table_edge_xml_names[index]))};
                        if (edge[index].color.empty() || edge[index].width <= 0)
                            continue;
                        const bool vertical = index == 0 || index == 2;
                        auto line = cell_shape(frame, x + (index == 2 ? width : 0),
                            y + (index == 3 ? height : 0), vertical ? 0 : width, vertical ? height : 0);
                        line.geometry = "line";
                        line.outline_color = edge[index].color;
                        line.outline_opacity = edge[index].opacity;
                        line.outline_width = edge[index].width;
                        borders.push_back(std::move(line));
                    }
                    if (!edge[0].color.empty() && edge[0].width > 0 && std::abs(edge[0].opacity - 1) < 1e-6 &&
                        std::all_of(edge.begin() + 1, edge.end(), [&](const Border& item)
                    {
                        return item.color == edge[0].color && std::abs(item.width - edge[0].width) < 1e-6 &&
                            std::abs(item.opacity - edge[0].opacity) < 1e-6;
                    }))
                    {
                        shape.table_cell->border_color = edge[0].color;
                        shape.table_cell->border_width = edge[0].width;
                    }
                    shapes.push_back(std::move(shape));
                }
                x += std::accumulate(widths.begin() + column, widths.begin() + column + step, 0.0);
                column += step;
                ++xml_cell;
            }
            y += heights[row];
        }
        shapes.insert(
            shapes.end(), std::make_move_iterator(borders.begin()), std::make_move_iterator(borders.end()));
        return shapes;
    }
}
