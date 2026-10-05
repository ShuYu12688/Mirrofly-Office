#include "spreadsheet_structure.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace
{
    using namespace mirrorfly;
    using namespace mirrorfly::spreadsheet_structure;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> adjusted_interval(
        std::uint32_t first, std::uint32_t last, const SpreadsheetAxisCommand& command)
    {
        std::int64_t low = first, high = last;
        if (command.insert)
        {
            if (low >= command.index)
                low += command.count;
            if (high >= command.index)
                high += command.count;
        }
        else
        {
            const auto end = std::int64_t(command.index) + command.count - 1;
            if (low >= command.index && low <= end)
                low = end + 1;
            if (high >= command.index && high <= end)
                high = std::int64_t(command.index) - 1;
            if (low > high)
                return {};
            if (low > end)
                low -= command.count;
            if (high > end)
                high -= command.count;
        }
        const auto limit = command.column ? maximum_spreadsheet_columns : maximum_spreadsheet_rows;
        require(low >= 0 && high < limit, "插入会将已有内容或区域移出工作表边界。");
        return std::pair{static_cast<std::uint32_t>(low), static_cast<std::uint32_t>(high)};
    }
    std::optional<SpreadsheetRange> adjusted_range(
        SpreadsheetRange range, const SpreadsheetAxisCommand& command)
    {
        auto& first = command.column ? range.first.column : range.first.row;
        auto& last = command.column ? range.last.column : range.last.row;
        const auto interval = adjusted_interval(first, last, command);
        if (!interval)
            return {};
        first = interval->first;
        last = interval->second;
        return range;
    }
    void rewrite_formulas(Node root, const std::string& sheet, const SpreadsheetReferenceChange& change)
    {
        for (auto row : child(root, "sheetData").children())
            for (auto cell : row.children())
                if (auto formula = child(cell, "f"))
                {
                    const auto rewritten =
                        spreadsheet_rewrite_formula(formula.child_value(), sheet, sheet, change);
                    require(rewritten.has_value(), "公式引用无法安全调整。");
                    formula.text().set(rewritten->c_str());
                    cell.remove_child(child(cell, "v"));
                    cell.remove_attribute("t");
                }
    }
    void move_cells(Node root, const SpreadsheetAxisCommand& command)
    {
        auto data = child(root, "sheetData");
        for (auto row = data.first_child(); row;)
        {
            auto next_row = row.next_sibling();
            const auto index = row.attribute("r").as_uint();
            require(index > 0 && index <= maximum_spreadsheet_rows, "工作表行号无效。");
            if (!command.column)
            {
                const auto moved = adjusted_interval(index - 1, index - 1, command);
                if (!moved)
                {
                    data.remove_child(row);
                    row = next_row;
                    continue;
                }
                set(row, "r", std::to_string(moved->first + 1));
            }
            row.remove_attribute("spans");
            for (auto cell = row.first_child(); cell;)
            {
                const auto next = cell.next_sibling();
                const auto address = parse_spreadsheet_address(cell.attribute("r").value());
                require(address.has_value(), "工作表单元格地址无效。");
                auto target = *address;
                auto& axis = command.column ? target.column : target.row;
                const auto moved = adjusted_interval(axis, axis, command);
                if (moved)
                {
                    axis = moved->first;
                    set(cell, "r", spreadsheet_address(target));
                }
                else
                    row.remove_child(cell);
                cell = next;
            }
            row = next_row;
        }
        root.remove_child(child(root, "dimension"));
        if (command.column)
        {
            auto cols = child(root, "cols");
            for (auto node = cols.first_child(); node;)
            {
                const auto next = node.next_sibling();
                const auto first = node.attribute("min").as_uint(), last = node.attribute("max").as_uint();
                require(
                    first > 0 && last >= first && last <= maximum_spreadsheet_columns, "列设置范围无效。");
                const auto moved = adjusted_interval(first - 1, last - 1, command);
                if (!moved)
                    cols.remove_child(node);
                else
                {
                    set(node, "min", std::to_string(moved->first + 1));
                    set(node, "max", std::to_string(moved->second + 1));
                }
                node = next;
            }
        }
    }
    void move_filter(Node filter, const SpreadsheetAxisCommand& command)
    {
        if (!filter)
            return;
        const auto original = parse_range(filter.attribute("ref").value());
        const auto moved = adjusted_range(original, command);
        if (!moved || moved->first.row == moved->last.row)
        {
            filter.parent().remove_child(filter);
            return;
        }
        set(filter, "ref", reference(*moved));
        if (command.column)
            for (auto node = filter.first_child(); node;)
            {
                const auto next = node.next_sibling();
                const auto index = original.first.column + node.attribute("colId").as_uint();
                const auto column = adjusted_interval(index, index, command);
                if (!column)
                    filter.remove_child(node);
                else
                    set(node, "colId", std::to_string(column->first - moved->first.column));
                node = next;
            }
    }
    void move_settings(Node root, const SpreadsheetAxisCommand& command)
    {
        auto merges = child(root, "mergeCells");
        unsigned count = 0;
        for (auto node = merges.first_child(); node;)
        {
            const auto next = node.next_sibling();
            const auto moved = adjusted_range(parse_range(node.attribute("ref").value()), command);
            if (!moved || (moved->first.row == moved->last.row && moved->first.column == moved->last.column))
                merges.remove_child(node);
            else
            {
                set(node, "ref", reference(*moved));
                ++count;
            }
            node = next;
        }
        if (count)
            set(merges, "count", std::to_string(count));
        else
            root.remove_child(merges);
        for (auto node = root.first_child(); node;)
        {
            const auto next = node.next_sibling();
            if (local(node.name()) == "conditionalFormatting")
            {
                const auto moved = adjusted_range(parse_range(node.attribute("sqref").value()), command);
                if (moved)
                    set(node, "sqref", reference(*moved));
                else
                    root.remove_child(node);
            }
            node = next;
        }
        move_filter(child(root, "autoFilter"), command);
        for (auto view : child(root, "sheetViews").children())
        {
            auto pane = child(view, "pane");
            if (pane)
            {
                const auto field = command.column ? "xSplit" : "ySplit";
                const auto frozen = pane.attribute(field).as_uint();
                if (frozen)
                {
                    const auto extent = adjusted_interval(0, frozen - 1, command);
                    const auto split = extent ? extent->second + 1 : 0;
                    require(split <= (command.column ? 8u : 16u), "插入后冻结区域过大，请先取消冻结。");
                    set(pane, field, std::to_string(split));
                }
                const auto x = pane.attribute("xSplit").as_uint();
                const auto y = pane.attribute("ySplit").as_uint();
                if (!x && !y)
                    view.remove_child(pane);
                else
                {
                    set(pane, "topLeftCell", spreadsheet_address({y, x}));
                    set(pane, "activePane", x ? (y ? "bottomRight" : "topRight") : "bottomLeft");
                }
            }
            while (child(view, "selection"))
                view.remove_child(child(view, "selection"));
            view.remove_attribute("topLeftCell");
        }
    }
    void header_cell(Node root, SpreadsheetAddress address, const std::string& text)
    {
        auto data = child(root, "sheetData");
        Node row;
        for (auto node : data.children())
            if (node.attribute("r").as_uint() == address.row + 1)
                row = node;
        require(static_cast<bool>(row), "表格缺少表头行。");
        Node target;
        for (auto node : row.children())
        {
            const auto existing = parse_spreadsheet_address(node.attribute("r").value());
            if (existing && existing->column > address.column)
            {
                target = row.insert_child_before(qualified(row, "c").c_str(), node);
                break;
            }
        }
        if (!target)
            target = add(row, "c");
        set(target, "r", spreadsheet_address(address));
        set(target, "t", "inlineStr");
        add(add(target, "is"), "t").text().set(text.c_str());
    }
    void remove_table(std::vector<OfficePart>& parts, Node sheet, const std::string& sheet_path,
        const std::string& table_path, std::size_t table_index)
    {
        auto table_parts = child(sheet, "tableParts");
        Node target;
        std::size_t index = 0;
        for (auto node : table_parts.children())
            if (index++ == table_index)
                target = node;
        require(static_cast<bool>(target), "表格部件次序无效。");
        std::string relation_id;
        for (auto attr : target.attributes())
            if (local(attr.name()) == "id")
                relation_id = attr.value();
        auto rels = load(parts, relations_path(sheet_path));
        for (auto node = rels.document_element().first_child(); node;)
        {
            const auto next = node.next_sibling();
            if (std::string(node.attribute("Id").value()) == relation_id)
                rels.document_element().remove_child(node);
            node = next;
        }
        store(parts, relations_path(sheet_path), rels);
        table_parts.remove_child(target);
        if (table_parts.first_child())
            set(table_parts, "count", std::to_string(index - 1));
        else
            sheet.remove_child(table_parts);
        auto types = load(parts, "[Content_Types].xml");
        for (auto node = types.document_element().first_child(); node;)
        {
            const auto next = node.next_sibling();
            if (std::string(node.attribute("PartName").value()) == '/' + table_path)
                types.document_element().remove_child(node);
            node = next;
        }
        store(parts, "[Content_Types].xml", types);
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                        [&](const auto& item)
        {
            return item.path == table_path;
        }),
            parts.end());
    }
    void move_tables(std::vector<OfficePart>& parts, Node sheet, const SpreadsheetSheet& source,
        const SpreadsheetAxisCommand& command)
    {
        // Reverse iteration keeps tableParts indices stable when tables disappear.
        for (std::size_t index = source.features.tables.size(); index > 0; --index)
        {
            const auto& table = source.features.tables[index - 1];
            auto xml = load(parts, table.path);
            auto root = xml.document_element();
            const auto moved = adjusted_range(table.range, command);
            if (!moved)
            {
                remove_table(parts, sheet, source.path, table.path, index - 1);
                continue;
            }
            require(moved->first.row < moved->last.row, "删除后表格将没有数据行，请先移除表格对象。");
            if (!command.column && !command.insert)
                require(table.range.first.row < command.index ||
                        table.range.first.row >= std::uint64_t(command.index) + command.count,
                    "不能单独删除表格表头；可先移除表格对象，或删除整个表格区域。");
            set(root, "ref", reference(*moved));
            move_filter(child(root, "autoFilter"), command);
            if (command.column)
            {
                const auto columns = child(root, "tableColumns");
                std::map<std::uint32_t, std::string> names;
                auto old_column = table.range.first.column;
                for (auto node : columns.children())
                {
                    const auto target = adjusted_interval(old_column, old_column, command);
                    if (target)
                        names[target->first] = node.attribute("name").value();
                    ++old_column;
                }
                std::set<std::string> used;
                for (const auto& [column, name] : names)
                {
                    (void)column;
                    used.insert(name);
                }
                auto replacement = root.insert_child_before(qualified(root, "tableColumns").c_str(), columns);
                for (auto column = moved->first.column; column <= moved->last.column; ++column)
                {
                    if (!names.count(column))
                    {
                        std::size_t suffix = 1;
                        std::string name;
                        do
                        {
                            name = "列" + std::to_string(suffix++);
                        } while (used.count(name));
                        names[column] = name;
                        used.insert(name);
                        header_cell(sheet, {moved->first.row, column}, name);
                    }
                    auto node = add(replacement, "tableColumn");
                    set(node, "id", std::to_string(column - moved->first.column + 1));
                    set(node, "name", names.at(column));
                }
                set(replacement, "count", std::to_string(moved->last.column - moved->first.column + 1));
                root.remove_child(columns);
            }
            store(parts, table.path, xml);
        }
    }

}

namespace mirrorfly
{
    using namespace spreadsheet_structure;
    SpreadsheetEditResult apply_spreadsheet_axis_command(
        SpreadsheetDocument& document, const SpreadsheetAxisCommand& command)
    {
        const auto limit = command.column ? maximum_spreadsheet_columns : maximum_spreadsheet_rows;
        if (command.sheet_index >= document.sheets.size() || !command.count || command.count > 4096 ||
            std::uint64_t(command.index) + command.count > limit)
            return {SpreadsheetError::InvalidValue, "行列范围无效，单次最多操作 4096 行或列。", false};
        try
        {
            auto package = serialize_spreadsheet(document);
            if (package.error != SpreadsheetError::None)
                return {package.error, package.message, false};
            auto base = parse_spreadsheet(package.parts);
            if (base.error != SpreadsheetError::None)
                return {base.error, base.message, false};
            require_supported(base.document, package.parts);
            const auto& source = base.document.sheets[command.sheet_index];
            SpreadsheetReferenceChange change;
            change.action =
                command.insert ? SpreadsheetReferenceAction::Insert : SpreadsheetReferenceAction::Erase;
            change.sheet = source.name;
            change.column = command.column;
            change.index = command.index;
            change.count = command.count;
            for (std::size_t index = 0; index < base.document.sheets.size(); ++index)
            {
                const auto& sheet = base.document.sheets[index];
                auto xml = load(package.parts, sheet.path);
                auto root = xml.document_element();
                require_axis_sheet(root);
                rewrite_formulas(root, sheet.name, change);
                if (index == command.sheet_index)
                {
                    if (sheet.features.filter)
                        for (auto row : child(root, "sheetData").children())
                        {
                            const auto r = row.attribute("r").as_uint();
                            if (r && r - 1 > sheet.features.filter->first.row &&
                                r - 1 <= sheet.features.filter->last.row)
                                set(row, "hidden", sheet.features.hidden_rows.count(r - 1) ? "1" : "0");
                        }
                    move_cells(root, command);
                    move_settings(root, command);
                    move_tables(package.parts, root, sheet, command);
                }
                store(package.parts, sheet.path, xml);
            }
            auto workbook = load(package.parts, base.document.workbook_path);
            for (auto name : child(workbook.document_element(), "definedNames").children())
            {
                const auto index =
                    name.attribute("localSheetId").as_uint(static_cast<unsigned>(command.sheet_index));
                require(index < base.document.sheets.size(), "命名区域的工作表索引无效。");
                const auto& host = base.document.sheets[index].name;
                const auto adjusted = spreadsheet_rewrite_formula(name.child_value(), host, host, change);
                require(adjusted.has_value(), "命名区域含未支持的引用，暂不能调整行列。");
                name.text().set(adjusted->c_str());
            }
            store(package.parts, base.document.workbook_path, workbook);
            auto result = parse_spreadsheet(std::move(package.parts));
            if (result.error != SpreadsheetError::None)
                return {result.error, result.message, false};
            require_supported(result.document, *result.document.original_parts);
            for (std::size_t index = 0; index < document.sheets.size(); ++index)
            {
                result.document.sheets[index].rows =
                    std::max(result.document.sheets[index].rows, document.sheets[index].rows);
                result.document.sheets[index].columns =
                    std::max(result.document.sheets[index].columns, document.sheets[index].columns);
            }
            auto& target = result.document.sheets[command.sheet_index];
            auto& extent = command.column ? target.columns : target.rows;
            const auto& previous_sheet = document.sheets[command.sheet_index];
            const auto previous = command.column ? previous_sheet.columns : previous_sheet.rows;
            if (command.insert)
                extent = std::max(extent,
                    static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(limit, std::uint64_t(previous) + command.count)));
            result.document.caches_stale = true;
            document = std::move(result.document);
            return {SpreadsheetError::None, {}, true};
        }
        catch (const Failure& failure)
        {
            return {failure.error, failure.message, false};
        }
        catch (const std::bad_alloc&)
        {
            return {SpreadsheetError::TooLarge, "行列操作超过内存预算，原工作簿已保留。", false};
        }
        catch (const std::exception& failure)
        {
            return {SpreadsheetError::InvalidValue, failure.what(), false};
        }
    }
}
