#include "spreadsheet_structure.hpp"

#include <algorithm>

namespace
{
    using namespace mirrorfly;
    using namespace mirrorfly::spreadsheet_structure;
    std::string identity(Node node)
    {
        for (auto attr : node.attributes())
            if (local(attr.name()) == "id")
                return attr.value();
        return {};
    }
    void identity(Node node, const std::string& value)
    {
        for (auto attr : node.attributes())
            if (local(attr.name()) == "id")
            {
                attr.set_value(value.c_str());
                return;
            }
        require(false, "工作表缺少关联标识。");
    }
    void erase_part(std::vector<OfficePart>& parts, const std::string& path)
    {
        const auto removed = std::remove_if(parts.begin(), parts.end(), [&](const auto& item)
        {
            return item.path == path;
        });
        parts.erase(removed, parts.end());
        auto types = load(parts, "[Content_Types].xml");
        for (auto node = types.document_element().first_child(); node;)
        {
            const auto next = node.next_sibling();
            if (std::string(node.attribute("PartName").value()) == '/' + path)
                types.document_element().remove_child(node);
            node = next;
        }
        store(parts, "[Content_Types].xml", types);
    }
    void clone_type(std::vector<OfficePart>& parts, const std::string& source, const std::string& target)
    {
        auto types = load(parts, "[Content_Types].xml");
        for (auto node : types.document_element().children())
            if (std::string(node.attribute("PartName").value()) == '/' + source)
            {
                auto copy = types.document_element().append_copy(node);
                set(copy, "PartName", '/' + target);
                store(parts, "[Content_Types].xml", types);
                return;
            }
        require(false, "复制对象缺少部件类型。");
    }
    std::string unused_path(std::vector<OfficePart>& parts, const std::string& folder)
    {
        for (std::size_t suffix = 1;; ++suffix)
        {
            const auto path = folder + "/mirrorfly-copy" + std::to_string(suffix) + ".xml";
            if (!part(parts, path) && !part(parts, relations_path(path)))
                return path;
        }
    }
    void require_table_relations(std::vector<OfficePart>& parts, const std::string& sheet_path)
    {
        const auto path = relations_path(sheet_path);
        if (!part(parts, path))
            return;
        auto xml = load(parts, path);
        for (auto node : xml.document_element().children())
        {
            const std::string type = node.attribute("Type").value();
            require(type.size() >= 6 && type.substr(type.size() - 6) == "/table" &&
                    std::string(node.attribute("TargetMode").value()) != "External",
                "此工作表关联了批注、绘图或其他对象，暂不能安全复制或删除。");
        }
    }
    void clone_tables(std::vector<OfficePart>& parts, const SpreadsheetDocument& base,
        const SpreadsheetSheet& source, const std::string& target)
    {
        if (source.features.tables.empty())
            return;
        auto sheet_xml = load(parts, source.path);
        auto rels = load(parts, relations_path(source.path));
        std::set<std::uint32_t> ids;
        std::set<std::string> names;
        auto workbook = load(parts, base.workbook_path);
        for (auto node : child(workbook.document_element(), "definedNames").children())
        {
            std::string name = node.attribute("name").value();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
            {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
            });
            names.insert(name);
        }
        for (const auto& sheet : base.sheets)
            for (const auto& table : sheet.features.tables)
            {
                ids.insert(table.id);
                auto name = table.name;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
                {
                    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
                });
                names.insert(name);
            }
        auto entry = child(sheet_xml.document_element(), "tableParts").first_child();
        for (const auto& table : source.features.tables)
        {
            auto xml = load(parts, table.path);
            auto root = xml.document_element();
            std::uint32_t id = 1;
            while (ids.count(id) || names.count("table" + std::to_string(id)))
                ++id;
            ids.insert(id);
            const auto name = "Table" + std::to_string(id);
            names.insert("table" + std::to_string(id));
            set(root, "id", std::to_string(id));
            set(root, "name", name);
            set(root, "displayName", name);
            const auto path = unused_path(parts, "xl/tables");
            parts.push_back({path, {}});
            store(parts, path, xml);
            clone_type(parts, table.path, path);
            bool linked = false;
            for (auto relation : rels.document_element().children())
                if (identity(entry) == relation.attribute("Id").value())
                {
                    set(relation, "Target", '/' + path);
                    linked = true;
                }
            require(linked, "表格关联缺失。");
            entry = entry.next_sibling();
        }
        parts.push_back({relations_path(target), {}});
        store(parts, relations_path(target), rels);
    }
    void rewrite_names(Node workbook, const SpreadsheetDocument& base, const SpreadsheetSheetCommand& command,
        const SpreadsheetReferenceChange& change, const std::vector<std::string>& old_ids,
        const std::vector<std::string>& new_ids)
    {
        auto names = child(workbook, "definedNames");
        std::vector<Node> originals;
        for (auto node : names.children())
            originals.push_back(node);
        for (auto node : originals)
        {
            const bool scoped = static_cast<bool>(node.attribute("localSheetId"));
            const auto index = node.attribute("localSheetId").as_uint(static_cast<unsigned>(command.index));
            require(index < base.sheets.size(), "命名区域的工作表索引无效。");
            if (scoped)
            {
                const auto next = std::find(new_ids.begin(), new_ids.end(), old_ids[index]);
                if (next == new_ids.end())
                {
                    names.remove_child(node);
                    continue;
                }
                set(node, "localSheetId", std::to_string(next - new_ids.begin()));
            }
            const auto& old_host = base.sheets[index].name;
            if (command.action == SpreadsheetSheetAction::Rename ||
                command.action == SpreadsheetSheetAction::Delete)
            {
                const auto host = command.action == SpreadsheetSheetAction::Rename && index == command.index
                    ? command.name
                    : old_host;
                const auto formula = spreadsheet_rewrite_formula(node.child_value(), old_host, host, change);
                require(formula.has_value(), "命名区域含未支持的引用，暂不能改名或删除工作表。");
                node.text().set(formula->c_str());
            }
            if (command.action == SpreadsheetSheetAction::Copy && scoped && index == command.index)
            {
                const auto formula =
                    spreadsheet_rewrite_formula(node.child_value(), old_host, command.name, change);
                require(formula.has_value(), "工作表局部名称含未支持的引用，暂不能复制。");
                auto copy = names.append_copy(node);
                set(copy, "localSheetId", std::to_string(command.index + 1));
                copy.text().set(formula->c_str());
            }
        }
    }
}

namespace mirrorfly
{
    using namespace spreadsheet_structure;
    SpreadsheetEditResult edit_spreadsheet_workbook(
        SpreadsheetDocument& document, const SpreadsheetSheetCommand& command)
    {
        if (document.read_only || command.index >= document.sheets.size())
            return {SpreadsheetError::ReadOnly, "工作表不存在或不允许修改结构。", false};
        const auto action = command.action;
        if ((action == SpreadsheetSheetAction::Copy &&
                document.sheets.size() >= maximum_spreadsheet_sheets) ||
            (action == SpreadsheetSheetAction::Move && command.destination >= document.sheets.size()))
            return {SpreadsheetError::InvalidValue, "工作表数量或目标位置超出范围。", false};
        if ((action == SpreadsheetSheetAction::Move && command.index == command.destination) ||
            (action == SpreadsheetSheetAction::Hide && document.sheets[command.index].hidden) ||
            (action == SpreadsheetSheetAction::Show && !document.sheets[command.index].hidden))
            return {};
        try
        {
            const auto visible =
                std::count_if(document.sheets.begin(), document.sheets.end(), [](const auto& sheet)
            {
                return !sheet.hidden;
            });
            require(document.sheets[command.index].hidden || visible > 1 ||
                    (action != SpreadsheetSheetAction::Delete && action != SpreadsheetSheetAction::Hide),
                "工作簿至少需要一个可见工作表。");
            auto package = serialize_spreadsheet(document);
            if (package.error != SpreadsheetError::None)
                return {package.error, package.message, false};
            auto parsed = parse_spreadsheet(package.parts);
            if (parsed.error != SpreadsheetError::None)
                return {parsed.error, parsed.message, false};
            const auto& base = parsed.document;
            const auto& source = base.sheets[command.index];
            const bool references = action == SpreadsheetSheetAction::Rename ||
                action == SpreadsheetSheetAction::Delete || action == SpreadsheetSheetAction::Copy;
            if (references)
                require_supported(base, package.parts);
            auto workbook = load(package.parts, base.workbook_path);
            auto root = workbook.document_element();
            const std::set<std::string> allowed{
                "fileVersion", "workbookPr", "bookViews", "sheets", "definedNames", "calcPr"};
            for (auto node : root.children())
                require(allowed.count(local(node.name())) != 0,
                    "此工作簿包含暂未支持的结构，请保留原工作表配置。");
            auto sheets = child(root, "sheets");
            std::vector<Node> nodes;
            std::vector<std::string> old_ids;
            for (auto node : sheets.children())
            {
                nodes.push_back(node);
                old_ids.push_back(identity(node));
            }
            require(nodes.size() == base.sheets.size(), "工作表结构与模型不一致。");
            auto current = nodes[command.index];
            SpreadsheetReferenceChange change;
            change.action = action == SpreadsheetSheetAction::Delete
                ? SpreadsheetReferenceAction::DeleteSheet
                : SpreadsheetReferenceAction::RenameSheet;
            change.sheet = source.name;
            change.target_sheet = command.name;
            if (references)
                for (std::size_t index = 0; index < base.sheets.size(); ++index)
                {
                    if (action == SpreadsheetSheetAction::Copy && index != command.index)
                        continue;
                    const auto& sheet = base.sheets[index];
                    auto xml = load(package.parts, sheet.path);
                    auto worksheet = xml.document_element();
                    require_axis_sheet(worksheet);
                    const auto host = index == command.index && action != SpreadsheetSheetAction::Delete
                        ? command.name
                        : sheet.name;
                    for (auto row : child(worksheet, "sheetData").children())
                        for (auto cell : row.children())
                            if (auto formula = child(cell, "f"))
                            {
                                const auto text = spreadsheet_rewrite_formula(
                                    formula.child_value(), sheet.name, host, change);
                                require(text.has_value(), "公式引用无法安全调整。");
                                formula.text().set(text->c_str());
                                cell.remove_child(child(cell, "v"));
                                cell.remove_attribute("t");
                            }
                    if (action == SpreadsheetSheetAction::Copy)
                    {
                        require_table_relations(package.parts, source.path);
                        const auto path = unused_path(package.parts, "xl/worksheets");
                        package.parts.push_back({path, {}});
                        store(package.parts, path, xml);
                        clone_type(package.parts, source.path, path);
                        clone_tables(package.parts, base, source, path);
                        auto rels = load(package.parts, relations_path(base.workbook_path));
                        std::set<std::string> used;
                        Node source_relation;
                        for (auto relation : rels.document_element().children())
                        {
                            used.insert(relation.attribute("Id").value());
                            if (old_ids[command.index] == relation.attribute("Id").value())
                                source_relation = relation;
                        }
                        require(static_cast<bool>(source_relation), "工作表关联缺失。");
                        std::size_t suffix = 1;
                        while (used.count("mirrorflyCopy" + std::to_string(suffix)))
                            ++suffix;
                        const auto id = "mirrorflyCopy" + std::to_string(suffix);
                        auto relation = rels.document_element().append_copy(source_relation);
                        set(relation, "Id", id);
                        set(relation, "Target", '/' + path);
                        store(package.parts, relations_path(base.workbook_path), rels);
                        auto copy = sheets.insert_copy_after(current, current);
                        set(copy, "name", command.name);
                        copy.remove_attribute("state");
                        identity(copy, id);
                        std::set<unsigned> sheet_ids;
                        for (auto node : nodes)
                            sheet_ids.insert(node.attribute("sheetId").as_uint());
                        unsigned sheet_id = 1;
                        while (sheet_ids.count(sheet_id))
                            ++sheet_id;
                        set(copy, "sheetId", std::to_string(sheet_id));
                    }
                    else
                        store(package.parts, sheet.path, xml);
                }
            if (action == SpreadsheetSheetAction::Rename)
                set(current, "name", command.name);
            else if (action == SpreadsheetSheetAction::Hide || action == SpreadsheetSheetAction::Show)
                set(current, "state", action == SpreadsheetSheetAction::Hide ? "hidden" : "visible");
            else if (action == SpreadsheetSheetAction::Move)
            {
                if (command.destination < command.index)
                    sheets.insert_move_before(current, nodes[command.destination]);
                else
                    sheets.insert_move_after(current, nodes[command.destination]);
            }
            else if (action == SpreadsheetSheetAction::Delete)
            {
                require_table_relations(package.parts, source.path);
                for (const auto& table : source.features.tables)
                    erase_part(package.parts, table.path);
                erase_part(package.parts, relations_path(source.path));
                erase_part(package.parts, source.path);
                auto rels = load(package.parts, relations_path(base.workbook_path));
                for (auto node = rels.document_element().first_child(); node;)
                {
                    auto next = node.next_sibling();
                    if (old_ids[command.index] == node.attribute("Id").value())
                        rels.document_element().remove_child(node);
                    node = next;
                }
                store(package.parts, relations_path(base.workbook_path), rels);
                sheets.remove_child(current);
            }
            std::vector<std::string> new_ids;
            std::size_t first_visible = 0;
            bool found_visible = false;
            for (auto node : sheets.children())
            {
                const std::string state = node.attribute("state").value();
                if (!found_visible && (state.empty() || state == "visible"))
                {
                    first_visible = new_ids.size();
                    found_visible = true;
                }
                new_ids.push_back(identity(node));
            }
            rewrite_names(root, base, command, change, old_ids, new_ids);
            for (auto view : child(root, "bookViews").children())
            {
                set(view, "activeTab", std::to_string(first_visible));
                set(view, "firstSheet", std::to_string(first_visible));
            }
            store(package.parts, base.workbook_path, workbook);
            auto result = parse_spreadsheet(std::move(package.parts));
            if (result.error != SpreadsheetError::None)
                return {result.error, result.message, false};
            if (references)
                require_supported(result.document, *result.document.original_parts);
            for (auto& sheet : result.document.sheets)
            {
                const auto previous =
                    std::find_if(document.sheets.begin(), document.sheets.end(), [&](const auto& item)
                {
                    return item.path == sheet.path;
                });
                if (previous != document.sheets.end())
                {
                    sheet.rows = std::max(sheet.rows, previous->rows);
                    sheet.columns = std::max(sheet.columns, previous->columns);
                }
                else if (action == SpreadsheetSheetAction::Copy)
                {
                    sheet.rows = std::max(sheet.rows, document.sheets[command.index].rows);
                    sheet.columns = std::max(sheet.columns, document.sheets[command.index].columns);
                }
            }
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
            return {SpreadsheetError::TooLarge, "工作表结构操作内存不足，原工作簿已保留。", false};
        }
    }

}
