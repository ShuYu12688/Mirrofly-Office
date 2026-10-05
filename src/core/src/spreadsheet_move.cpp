#include "spreadsheet_structure.hpp"

#include <algorithm>

namespace
{
    using namespace mirrorfly;
    using namespace mirrorfly::spreadsheet_structure;
    bool contains(SpreadsheetRange range, SpreadsheetAddress address)
    {
        return address.row >= range.first.row && address.row <= range.last.row &&
            address.column >= range.first.column && address.column <= range.last.column;
    }
    bool contains(SpreadsheetRange outer, SpreadsheetRange inner)
    {
        return contains(outer, inner.first) && contains(outer, inner.last);
    }
    bool intersects(SpreadsheetRange a, SpreadsheetRange b)
    {
        return a.first.row <= b.last.row && a.last.row >= b.first.row && a.first.column <= b.last.column &&
            a.last.column >= b.first.column;
    }
    SpreadsheetAddress moved(SpreadsheetAddress address, const SpreadsheetMoveCommand& command)
    {
        return {address.row - command.range.first.row + command.destination.row,
            address.column - command.range.first.column + command.destination.column};
    }
    SpreadsheetRange moved(SpreadsheetRange range, const SpreadsheetMoveCommand& command)
    {
        return {moved(range.first, command), moved(range.last, command)};
    }
    void namespaces(Node source, Node copy)
    {
        for (auto attr : source.attributes())
            if (std::string(attr.name()) == "xmlns" || std::string(attr.name()).find("xmlns:") == 0)
                set(copy, attr.name(), attr.value());
    }
    void check_regions(const SpreadsheetDocument& document, const SpreadsheetMoveCommand& command)
    {
        const auto target_range = moved(command.range, command);
        const auto& source = spreadsheet_features(document, command.sheet_index);
        const auto& target = spreadsheet_features(document, command.target_sheet);
        for (const auto& merge : source.merges)
        {
            require(!intersects(merge, command.range) || contains(command.range, merge),
                "请选中完整的合并区域再剪切。");
            if (contains(command.range, merge))
            {
                const auto range = moved(merge, command);
                require(!((range.first.row < target.frozen_rows && range.last.row >= target.frozen_rows) ||
                            (range.first.column < target.frozen_columns &&
                                range.last.column >= target.frozen_columns)),
                    "移动后的合并区域跨过冻结线，请先取消目标冻结。");
            }
        }
        for (const auto& merge : target.merges)
        {
            if (command.sheet_index == command.target_sheet && contains(command.range, merge))
                continue;
            require(!intersects(merge, target_range), "粘贴目标含合并单元格，请先取消目标合并。");
        }
        for (const auto& rule : source.conditions)
            require(!intersects(rule.range, command.range) || contains(command.range, rule.range),
                "剪切区域只覆盖了部分条件格式，请选中完整规则区域，或先清除该规则。");
        for (const auto& table : source.tables)
        {
            const SpreadsheetRange header{
                table.range.first, {table.range.first.row, table.range.last.column}};
            require(!intersects(header, command.range) || contains(command.range, table.range),
                "表头须与完整表格一起移动；表格数据行可以单独剪切。");
            if (contains(command.range, table.range))
                for (const auto& existing : target.tables)
                {
                    if (command.sheet_index == command.target_sheet &&
                        contains(command.range, existing.range))
                        continue;
                    require(!intersects(moved(table.range, command), existing.range),
                        "移动后的表格与目标表格重叠。");
                }
        }
        for (const auto& table : target.tables)
        {
            if (command.sheet_index == command.target_sheet && contains(command.range, table.range))
                continue;
            const SpreadsheetRange header{
                table.range.first, {table.range.first.row, table.range.last.column}};
            require(!intersects(header, target_range), "粘贴目标包含表头，请避开表头或先移除目标表格对象。");
        }
        if (source.filter && contains(command.range, *source.filter) && target.filter)
            require(command.sheet_index == command.target_sheet,
                "目标工作表已有筛选，请先清除后再移动筛选区域。");
    }
    void normalize_filter_rows(Node root, const SpreadsheetFeatures& features)
    {
        if (features.filter)
            for (auto row : child(root, "sheetData").children())
            {
                const auto index = row.attribute("r").as_uint();
                if (index && index - 1 > features.filter->first.row && index - 1 <= features.filter->last.row)
                    set(row, "hidden", features.hidden_rows.count(index - 1) ? "1" : "0");
            }
    }
    void rewrite(Node root, std::size_t index, const SpreadsheetDocument& document,
        const SpreadsheetMoveCommand& command, const SpreadsheetReferenceChange& change)
    {
        const auto& host = document.sheets[index].name;
        for (auto row : child(root, "sheetData").children())
            for (auto cell : row.children())
                if (auto formula = child(cell, "f"))
                {
                    const auto address = parse_spreadsheet_address(cell.attribute("r").value());
                    require(address.has_value(), "公式单元格地址无效。");
                    const bool transferring =
                        index == command.sheet_index && contains(command.range, *address);
                    const auto& result_host = transferring ? change.target_sheet : host;
                    const auto value =
                        spreadsheet_rewrite_formula(formula.child_value(), host, result_host, change);
                    require(value.has_value(), "剪切后公式引用无法安全调整。");
                    formula.text().set(value->c_str());
                    cell.remove_child(child(cell, "v"));
                    cell.remove_attribute("t");
                }
    }
    void clear_cells(Node root, SpreadsheetRange range)
    {
        for (auto row : child(root, "sheetData").children())
        {
            row.remove_attribute("spans");
            for (auto cell = row.first_child(); cell;)
            {
                const auto next = cell.next_sibling();
                const auto address = parse_spreadsheet_address(cell.attribute("r").value());
                require(address.has_value(), "单元格地址无效。");
                if (contains(range, *address))
                    row.remove_child(cell);
                cell = next;
            }
        }
        root.remove_child(child(root, "dimension"));
    }
    void insert_cell(Node root, Node cell, SpreadsheetAddress address)
    {
        auto data = child(root, "sheetData");
        Node row;
        for (auto existing : data.children())
        {
            const auto index = existing.attribute("r").as_uint();
            if (index == address.row + 1)
            {
                row = existing;
                break;
            }
            if (index > address.row + 1)
            {
                row = data.insert_child_before(qualified(data, "row").c_str(), existing);
                break;
            }
        }
        if (!row)
            row = add(data, "row");
        set(row, "r", std::to_string(address.row + 1));
        Node copy;
        for (auto existing : row.children())
        {
            const auto at = parse_spreadsheet_address(existing.attribute("r").value());
            if (at && at->column > address.column)
            {
                copy = row.insert_copy_before(cell, existing);
                break;
            }
        }
        if (!copy)
            copy = row.append_copy(cell);
        set(copy, "r", spreadsheet_address(address));
    }
    void move_regions(Node source, Node target, const SpreadsheetMoveCommand& command)
    {
        auto merges = child(source, "mergeCells");
        std::vector<Node> moved_merges;
        for (auto node : merges.children())
            if (contains(command.range, parse_range(node.attribute("ref").value())))
                moved_merges.push_back(node);
        if (!moved_merges.empty())
        {
            auto destination = child(target, "mergeCells");
            if (!destination)
                destination = add(target, "mergeCells");
            for (auto node : moved_merges)
            {
                auto copy = destination.append_copy(node);
                namespaces(source, copy);
                set(copy, "ref", reference(moved(parse_range(node.attribute("ref").value()), command)));
                merges.remove_child(node);
            }
            for (auto container : {merges, destination})
            {
                unsigned count = 0;
                for (auto node : container.children())
                {
                    (void)node;
                    ++count;
                }
                set(container, "count", std::to_string(count));
            }
            if (!merges.first_child())
                source.remove_child(merges);
        }
        std::vector<Node> rules;
        unsigned priority = 0;
        for (auto node : target.children())
            if (local(node.name()) == "conditionalFormatting")
                for (auto rule : node.children())
                    priority = std::max(priority, rule.attribute("priority").as_uint());
        for (auto node : source.children())
            if (local(node.name()) == "conditionalFormatting" &&
                contains(command.range, parse_range(node.attribute("sqref").value())))
                rules.push_back(node);
        for (auto node : rules)
        {
            auto copy = target.append_copy(node);
            namespaces(source, copy);
            set(copy, "sqref", reference(moved(parse_range(node.attribute("sqref").value()), command)));
            if (source != target)
                for (auto rule : copy.children())
                    set(rule, "priority", std::to_string(++priority));
            source.remove_child(node);
        }
        auto filter = child(source, "autoFilter");
        if (filter && contains(command.range, parse_range(filter.attribute("ref").value())))
        {
            auto copy = target.append_copy(filter);
            namespaces(source, copy);
            set(copy, "ref", reference(moved(parse_range(filter.attribute("ref").value()), command)));
            source.remove_child(filter);
        }
    }
    void order_regions(Node root)
    {
        // These supported worksheet children have a fixed OOXML sequence.
        const std::vector<const char*> order{"sheetPr", "dimension", "sheetViews", "sheetFormatPr", "cols",
            "sheetData", "sheetCalcPr", "autoFilter", "mergeCells", "conditionalFormatting", "printOptions",
            "pageMargins", "pageSetup", "headerFooter", "tableParts"};
        for (const auto* name : order)
        {
            std::vector<Node> nodes;
            for (auto node : root.children())
                if (local(node.name()) == name)
                    nodes.push_back(node);
            for (auto node : nodes)
                root.append_move(node);
        }
    }
    void move_tables(std::vector<OfficePart>& parts, const SpreadsheetDocument& base, Node source,
        Node target, const SpreadsheetMoveCommand& command)
    {
        const auto& from = base.sheets[command.sheet_index];
        const auto& to = base.sheets[command.target_sheet];
        auto entries = child(source, "tableParts");
        std::vector<Node> nodes;
        for (auto node : entries.children())
            nodes.push_back(node);
        for (std::size_t index = 0; index < from.features.tables.size(); ++index)
        {
            const auto& table = from.features.tables[index];
            if (!contains(command.range, table.range))
                continue;
            auto xml = load(parts, table.path);
            auto root = xml.document_element();
            set(root, "ref", reference(moved(table.range, command)));
            if (auto filter = child(root, "autoFilter"))
                set(filter, "ref", reference(moved(parse_range(filter.attribute("ref").value()), command)));
            store(parts, table.path, xml);
            if (command.sheet_index == command.target_sheet)
                continue;
            auto source_rels = load(parts, relations_path(from.path));
            std::string id;
            for (auto attr : nodes[index].attributes())
                if (local(attr.name()) == "id")
                    id = attr.value();
            for (auto relation = source_rels.document_element().first_child(); relation;)
            {
                const auto next = relation.next_sibling();
                if (id == relation.attribute("Id").value())
                    source_rels.document_element().remove_child(relation);
                relation = next;
            }
            store(parts, relations_path(from.path), source_rels);
            const auto target_path = relations_path(to.path);
            if (!part(parts, target_path))
                parts.push_back({target_path,
                    "<Relationships "
                    "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"/>"});
            auto target_rels = load(parts, target_path);
            std::set<std::string> ids;
            for (auto relation : target_rels.document_element().children())
                ids.insert(relation.attribute("Id").value());
            std::size_t suffix = 1;
            while (ids.count("mirrorflyMove" + std::to_string(suffix)))
                ++suffix;
            id = "mirrorflyMove" + std::to_string(suffix);
            auto relation = add(target_rels.document_element(), "Relationship");
            set(relation, "Id", id);
            set(relation, "Target", '/' + table.path);
            set(relation, "Type",
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table");
            store(parts, target_path, target_rels);
            auto destination = child(target, "tableParts");
            if (!destination)
                destination = add(target, "tableParts");
            auto entry = add(destination, "tablePart");
            set(entry, "xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
            set(entry, "r:id", id);
            entries.remove_child(nodes[index]);
        }
        for (auto root : {source, target})
        {
            auto container = child(root, "tableParts");
            unsigned count = 0;
            for (auto node : container.children())
            {
                (void)node;
                ++count;
            }
            if (count)
                set(container, "count", std::to_string(count));
            else
                root.remove_child(container);
        }
    }
}

namespace mirrorfly
{
    using namespace spreadsheet_structure;
    SpreadsheetEditResult apply_spreadsheet_move(
        SpreadsheetDocument& document, const SpreadsheetMoveCommand& command)
    {
        const auto& r = command.range;
        if (command.sheet_index >= document.sheets.size() || command.target_sheet >= document.sheets.size() ||
            r.first.row > r.last.row || r.first.column > r.last.column ||
            r.last.row >= maximum_spreadsheet_rows || r.last.column >= maximum_spreadsheet_columns ||
            std::uint64_t(r.last.row - r.first.row + 1) * (r.last.column - r.first.column + 1) >
                maximum_spreadsheet_batch_cells ||
            std::uint64_t(command.destination.row) + r.last.row - r.first.row >= maximum_spreadsheet_rows ||
            std::uint64_t(command.destination.column) + r.last.column - r.first.column >=
                maximum_spreadsheet_columns)
            return {SpreadsheetError::InvalidValue, "移动范围无效或超过单次 4096 格限制。", false};
        if (command.sheet_index == command.target_sheet && r.first.row == command.destination.row &&
            r.first.column == command.destination.column)
            return {};
        try
        {
            auto package = serialize_spreadsheet(document);
            if (package.error != SpreadsheetError::None)
                return {package.error, package.message, false};
            auto parsed = parse_spreadsheet(package.parts);
            if (parsed.error != SpreadsheetError::None)
                return {parsed.error, parsed.message, false};
            const auto& base = parsed.document;
            require_supported(base, package.parts);
            check_regions(base, command);
            std::vector<pugi::xml_document> worksheets;
            for (const auto& sheet : base.sheets)
                worksheets.push_back(load(package.parts, sheet.path));
            auto source = worksheets[command.sheet_index].document_element();
            auto target = worksheets[command.target_sheet].document_element();
            require_axis_sheet(source);
            require_axis_sheet(target);
            SpreadsheetReferenceChange change;
            change.action = SpreadsheetReferenceAction::Move;
            change.sheet = base.sheets[command.sheet_index].name;
            change.target_sheet = base.sheets[command.target_sheet].name;
            change.range = r;
            change.destination = command.destination;
            for (std::size_t index = 0; index < worksheets.size(); ++index)
            {
                require_axis_sheet(worksheets[index].document_element());
                rewrite(worksheets[index].document_element(), index, base, command, change);
            }
            pugi::xml_document buffer;
            auto cells = buffer.append_child("cells");
            for (auto row : child(source, "sheetData").children())
                for (auto cell : row.children())
                {
                    const auto address = parse_spreadsheet_address(cell.attribute("r").value());
                    require(address.has_value(), "单元格地址无效。");
                    if (contains(r, *address))
                    {
                        auto copy = cells.append_copy(cell);
                        if (source != target)
                            namespaces(source, copy);
                    }
                }
            normalize_filter_rows(source, spreadsheet_features(base, command.sheet_index));
            normalize_filter_rows(target, spreadsheet_features(base, command.target_sheet));
            clear_cells(source, r);
            clear_cells(target, moved(r, command));
            for (auto cell : cells.children())
                insert_cell(
                    target, cell, moved(*parse_spreadsheet_address(cell.attribute("r").value()), command));
            move_regions(source, target, command);
            move_tables(package.parts, base, source, target, command);
            order_regions(source);
            order_regions(target);
            for (std::size_t index = 0; index < worksheets.size(); ++index)
                store(package.parts, base.sheets[index].path, worksheets[index]);
            auto workbook = load(package.parts, base.workbook_path);
            for (auto name : child(workbook.document_element(), "definedNames").children())
            {
                const auto index =
                    name.attribute("localSheetId").as_uint(static_cast<unsigned>(command.sheet_index));
                require(index < base.sheets.size(), "命名区域的工作表索引无效。");
                const auto& host = base.sheets[index].name;
                const auto formula = spreadsheet_rewrite_formula(name.child_value(), host, host, change);
                require(formula.has_value(), "命名区域含未支持的引用，暂不能移动单元格。");
                name.text().set(formula->c_str());
            }
            store(package.parts, base.workbook_path, workbook);
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
            const auto destination = moved(r, command);
            auto& target_sheet = result.document.sheets[command.target_sheet];
            target_sheet.rows = std::max(target_sheet.rows, destination.last.row + 1);
            target_sheet.columns = std::max(target_sheet.columns, destination.last.column + 1);
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
            return {SpreadsheetError::TooLarge, "剪切移动内存不足，原工作簿已保留。", false};
        }
    }
}
