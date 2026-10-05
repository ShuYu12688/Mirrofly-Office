#include "word_style_merge.hpp"
#include "word_styles.hpp"
#include "word_xml.hpp"

#include <array>
#include <charconv>

namespace
{
    using namespace mirrorfly::word_xml;
    // Word precedence, MS-OI29500 17.7.6.6; cnfStyle is a cached hint, not source geometry.
    constexpr const char* regions[]{"band1Horz", "band2Horz", "band1Vert", "band2Vert", "firstCol", "lastCol",
        "firstRow", "lastRow", "nwCell", "neCell", "swCell", "seCell"};
    constexpr const char* look_names[]{
        "firstRow", "lastRow", "firstColumn", "lastColumn", "noHBand", "noVBand"};

    bool on(pugi::xml_attribute value)
    {
        const std::string_view text = value.value();
        return text == "1" || text == "true" || text == "on";
    }

    std::array<bool, 6> table_look(pugi::xml_node properties)
    {
        const auto look = child(properties, "tblLook");
        std::array<bool, 6> result{};
        bool explicit_flags = false;
        for (unsigned i = 0; i < result.size(); ++i)
        {
            const auto value = attribute(look, look_names[i]);
            explicit_flags = explicit_flags || bool(value);
            result[i] = on(value);
        }
        if (!explicit_flags)
        {
            const std::string_view text = attribute(look, "val").value();
            unsigned mask = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), mask, 16);
            if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size())
                for (unsigned i = 0; i < result.size(); ++i)
                    result[i] = (mask & (0x20U << i)) != 0;
        }
        return result;
    }

    int count(pugi::xml_node properties, const char* name, int fallback = 0)
    {
        return std::clamp(attribute(child(properties, name), "val").as_int(fallback), 0, 256);
    }

    std::map<pugi::xml_node, unsigned> index_regions(pugi::xml_node table, pugi::xml_node properties)
    {
        struct Cell
        {
            pugi::xml_node node;
            int row;
            int column;
            int span;
        };
        std::vector<Cell> cells;
        int rows = 0;
        int columns = 0;
        int headers = 0;
        for (auto grid : child(table, "tblGrid").children())
            if (named(grid, "gridCol"))
                ++columns;
        for (auto row : table.children())
        {
            if (!named(row, "tr"))
                continue;
            const auto row_properties = child(row, "trPr");
            const auto header = child(row_properties, "tblHeader");
            if (rows == headers && header && (!attribute(header, "val") || on(attribute(header, "val"))))
                ++headers;
            int column = count(row_properties, "gridBefore");
            for (auto cell : row.children())
            {
                if (!named(cell, "tc"))
                    continue;
                if (cells.size() >= 200000)
                    throw std::bad_alloc{};
                const int span = std::max(1, count(child(cell, "tcPr"), "gridSpan", 1));
                cells.push_back({cell, rows, column, span});
                column += span;
            }
            columns = std::max(columns, column + count(row_properties, "gridAfter"));
            ++rows;
        }
        const auto look = table_look(properties);
        const int first_rows = look[0] ? std::max(1, headers) : 0;
        const int row_band = std::min(3, count(properties, "tblStyleRowBandSize"));
        const int column_band = std::min(3, count(properties, "tblStyleColBandSize"));
        std::map<pugi::xml_node, unsigned> result;
        for (const auto& cell : cells)
        {
            const bool first_row = look[0] && cell.row < first_rows;
            const bool last_row = look[1] && cell.row == rows - 1;
            const bool first_column = look[2] && cell.column == 0;
            const bool last_column = look[3] && cell.column + cell.span == columns;
            unsigned mask = 0;
            if (!look[4] && row_band > 0 && !first_row && !last_row)
                mask |= 1U << (((cell.row - first_rows) / row_band) % 2);
            if (!look[5] && column_band > 0 && !first_column && !last_column)
                mask |= 1U << (2 + ((std::max(0, cell.column - int(look[2])) / column_band) % 2));
            if (first_column)
                mask |= 1U << 4;
            if (last_column)
                mask |= 1U << 5;
            if (first_row)
                mask |= 1U << 6;
            if (last_row)
                mask |= 1U << 7;
            if (first_row && first_column)
                mask |= 1U << 8;
            if (first_row && last_column)
                mask |= 1U << 9;
            if (last_row && first_column)
                mask |= 1U << 10;
            if (last_row && last_column)
                mask |= 1U << 11;
            result.emplace(cell.node, mask);
        }
        return result;
    }
}

namespace mirrorfly::word_detail
{
    std::string StyleResolver::table_id(pugi::xml_node table) const
    {
        const std::string id = attribute(child(child(table, "tblPr"), "tblStyle"), "val").value();
        const auto found = definitions_.find(id);
        return found != definitions_.end() &&
                std::string_view(attribute(found->second, "type").value()) == "table"
            ? id
            : default_table_;
    }

    void StyleResolver::merge_table_style(pugi::xml_node target, const std::string& id, unsigned mask) const
    {
        const auto styles = chain(id, "table");
        const auto merge = [&](pugi::xml_node source)
        {
            for (const auto* name : {"tblPr", "tcPr", "pPr", "rPr"})
            {
                const auto properties = child(source, name);
                if (!properties)
                    continue;
                auto destination = child(target, name);
                if (!destination)
                    destination = target.append_child(("w:" + std::string(name)).c_str());
                // Table style booleans replace inherited values, unlike paragraph style toggles.
                merge_properties(destination, properties);
            }
        };
        for (auto style : styles)
            merge(style);
        for (unsigned i = 0; i < std::size(regions); ++i)
            if (mask & (1U << i))
                for (auto style : styles)
                    for (auto condition : style.children())
                        if (named(condition, "tblStylePr") &&
                            std::string_view(attribute(condition, "type").value()) == regions[i])
                            merge(condition);
    }

    void StyleResolver::table_properties(pugi::xml_node table, pugi::xml_node target) const
    {
        for (const auto style : chain(table_id(table), "table"))
            merge_properties(target, child(style, "tblPr"));
        merge_properties(target, child(table, "tblPr"));
    }

    std::pair<std::string, unsigned> StyleResolver::table_context(pugi::xml_node node, bool row_wide)
    {
        pugi::xml_node cell;
        auto table = node;
        while (table && !named(table, "tbl"))
        {
            if (named(table, "tc"))
                cell = table;
            table = table.parent();
        }
        if (!table || !cell)
            return {};
        auto found = tables_.find(table);
        if (found == tables_.end())
        {
            pugi::xml_document xml;
            const auto properties = property_root(xml, "w:tblPr");
            table_properties(table, properties);
            TableContext context{table_id(table), index_regions(table, properties), {}};
            for (const auto& entry : context.regions)
                context.row_regions[entry.first.parent()] |= entry.second;
            cell_count_ += context.regions.size();
            if (cell_count_ > 200000 || tables_.size() >= 10000)
                throw std::bad_alloc{};
            found = tables_.emplace(table, std::move(context)).first;
        }
        const auto& index = row_wide ? found->second.row_regions : found->second.regions;
        const auto region = index.find(row_wide ? cell.parent() : cell);
        return {found->second.id, region == index.end() ? 0 : region->second};
    }

    void StyleResolver::cell_table_properties(pugi::xml_node cell, pugi::xml_node target)
    {
        const auto context = table_context(cell);
        pugi::xml_document xml;
        const auto base = property_root(xml, "w:style");
        merge_table_style(base, context.first, context.second);
        merge_properties(target, child(base, "tblPr"));
        const auto row = cell.parent();
        merge_properties(target, child(row.parent(), "tblPr"));
        merge_properties(target, child(row, "tblPrEx"));
    }

    void StyleResolver::cell_properties(pugi::xml_node cell, pugi::xml_node target, bool direct)
    {
        const auto context = table_context(cell);
        pugi::xml_document xml;
        const auto base = property_root(xml, "w:style");
        merge_table_style(base, context.first, context.second);
        merge_properties(target, child(base, "tcPr"));
        const auto row = table_context(cell, true);
        if (row.second != context.second)
        {
            pugi::xml_document row_xml;
            const auto row_base = property_root(row_xml, "w:style");
            merge_table_style(row_base, row.first, row.second);
            // Word applies these conditional cell properties to the entire row (17.7.6.8).
            for (const auto* name : {"tcMar", "vAlign", "noWrap"})
                if (const auto value = child(child(row_base, "tcPr"), name))
                {
                    if (auto existing = child(target, name))
                        target.remove_child(existing);
                    target.append_copy(value);
                }
        }
        // Structure comes only from the document, never from a malformed style definition.
        for (const auto* name : {"gridSpan", "vMerge", "hMerge", "tcW"})
            if (auto node = child(target, name))
                target.remove_child(node);
        if (direct)
            merge_properties(target, child(cell, "tcPr"));
    }
}
