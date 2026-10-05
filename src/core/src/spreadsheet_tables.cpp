#include "spreadsheet_tables.hpp"
#include "spreadsheet_colors.hpp"
#include "spreadsheet_filters.hpp"

#include <pugixml.hpp>
#include <utf8.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace
{
    using namespace mirrorfly;
    using Node = pugi::xml_node;
    using Style = std::map<std::string, SpreadsheetFormat>;
    constexpr auto spreadsheet_namespace = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
    constexpr auto relationship_namespace =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    std::string lower(std::string text);

    std::string local(const char* name)
    {
        const std::string text(name);
        const auto colon = text.find(':');
        return colon == std::string::npos ? text : text.substr(colon + 1);
    }
    Node child(Node root, const char* name)
    {
        for (auto node : root.children())
            if (local(node.name()) == name)
                return node;
        return {};
    }
    std::string qualified(Node parent, const char* name)
    {
        const std::string text(parent.name());
        const auto colon = text.find(':');
        return colon == std::string::npos ? name : text.substr(0, colon + 1) + name;
    }
    Node add(Node parent, const char* name)
    {
        return parent.append_child(qualified(parent, name).c_str());
    }
    void set(Node node, const char* key, const std::string& value)
    {
        auto attribute = node.attribute(key);
        if (!attribute)
            attribute = node.append_attribute(key);
        attribute = value.c_str();
    }
    unsigned count(Node node)
    {
        unsigned result = 0;
        for (auto item : node.children())
            if (item.type() == pugi::node_element)
                ++result;
        return result;
    }
    bool only_attributes(Node node, const std::set<std::string>& names)
    {
        for (auto attribute : node.attributes())
            if (!names.count(attribute.name()))
                return false;
        return true;
    }
    const OfficePart* part(const std::vector<OfficePart>& parts, const std::string& path)
    {
        const auto found = std::find_if(parts.begin(), parts.end(), [&](const auto& item)
        {
            return item.path == path;
        });
        return found == parts.end() ? nullptr : &*found;
    }
    std::string save(const pugi::xml_document& xml)
    {
        std::ostringstream out;
        xml.save(out, "", pugi::format_raw, pugi::encoding_utf8);
        return out.str();
    }
    void store(std::vector<OfficePart>& parts, const std::string& path, const pugi::xml_document& xml)
    {
        for (auto& item : parts)
            if (item.path == path)
            {
                item.bytes = save(xml);
                return;
            }
        parts.push_back({path, save(xml)});
    }
    pugi::xml_document load(
        const std::vector<OfficePart>& parts, const std::string& path, std::size_t* workbook_nodes = nullptr)
    {
        pugi::xml_document xml;
        const auto source = part(parts, path);
        if (!source || source->bytes.size() > maximum_spreadsheet_xml_bytes)
            throw std::runtime_error("表格对象缺少有效 XML 部件。");
        const auto normalized = lower(source->bytes);
        if (normalized.find("<!doctype") != std::string::npos ||
            normalized.find("<!entity") != std::string::npos ||
            !xml.load_buffer(source->bytes.data(), source->bytes.size()))
            throw std::runtime_error("表格对象缺少有效 XML 部件。");
        std::vector<std::pair<Node, std::size_t>> pending{{xml.document_element(), 1}};
        std::size_t nodes = 0;
        while (!pending.empty())
        {
            const auto [node, depth] = pending.back();
            pending.pop_back();
            if (++nodes > maximum_spreadsheet_xml_nodes || depth > maximum_spreadsheet_xml_depth)
                throw std::runtime_error("表格 XML 结构超过限制。");
            if (workbook_nodes && ++*workbook_nodes > maximum_spreadsheet_xml_nodes)
                throw std::runtime_error("工作簿 XML 节点总数超过限制。");
            for (auto item : node.children())
                pending.emplace_back(item, depth + 1);
        }
        return xml;
    }
    std::string lower(std::string text)
    {
        for (auto& c : text)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return text;
    }
    bool color(const std::string& text)
    {
        return text.size() == 7 && text.front() == '#' &&
            text.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
    }
    bool contains(SpreadsheetRange range, SpreadsheetAddress address)
    {
        return address.row >= range.first.row && address.row <= range.last.row &&
            address.column >= range.first.column && address.column <= range.last.column;
    }
    bool overlaps(SpreadsheetRange a, SpreadsheetRange b)
    {
        return a.first.row <= b.last.row && b.first.row <= a.last.row && a.first.column <= b.last.column &&
            b.first.column <= a.last.column;
    }
    std::string reference(SpreadsheetRange range)
    {
        return spreadsheet_address(range.first) + ":" + spreadsheet_address(range.last);
    }
    std::optional<SpreadsheetRange> range(const std::string& text)
    {
        const auto colon = text.find(':');
        const auto first = parse_spreadsheet_address(text.substr(0, colon));
        const auto last =
            colon == std::string::npos ? first : parse_spreadsheet_address(text.substr(colon + 1));
        if (!first || !last || first->row > last->row || first->column > last->column)
            return {};
        return SpreadsheetRange{*first, *last};
    }
    bool valid_name(const std::string& name)
    {
        if (name.empty() || name.size() > 64 || lower(name) == "r" || lower(name) == "c" ||
            (name.front() >= '0' && name.front() <= '9') ||
            name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
                std::string::npos ||
            parse_spreadsheet_address(name))
            return false;
        // Reserve R1C1-style names as well as A1-style addresses.
        const auto text = lower(name);
        const auto c = text.find('c', 1);
        return !(text.front() == 'r' && c != std::string::npos && c > 1 && c + 1 < text.size() &&
            text.substr(1, c - 1).find_first_not_of("0123456789") == std::string::npos &&
            text.substr(c + 1).find_first_not_of("0123456789") == std::string::npos);
    }
    bool valid_header(const SpreadsheetValue& value)
    {
        if (value.kind != SpreadsheetValueKind::Text || value.text.empty() || value.text.size() > 1020 ||
            !utf8::is_valid(value.text.begin(), value.text.end()) ||
            utf8::distance(value.text.begin(), value.text.end()) > 255 ||
            value.text.find_first_not_of(" \t\r\n") == std::string::npos)
            return false;
        return std::none_of(value.text.begin(), value.text.end(), [](unsigned char c)
        {
            return c < 32;
        });
    }
    bool valid_style(const Style& style)
    {
        static const std::set<std::string> elements{
            "wholeTable", "headerRow", "firstRowStripe", "firstColumnStripe", "firstColumn", "lastColumn"};
        if (style.empty() || style.size() > elements.size())
            return false;
        for (const auto& [name, format] : style)
        {
            if (!elements.count(name) || format.empty() || format.size() > 3)
                return false;
            for (const auto& [key, value] : format)
                if (!((key == "bold" && (value == "0" || value == "1")) ||
                        ((key == "fill" || key == "text") && color(value))))
                    return false;
        }
        return true;
    }
    std::string style_key(const Style& style)
    {
        std::string result;
        for (const auto& [name, format] : style)
        {
            result += name + ':';
            for (const auto& [key, value] : format)
                result += key + ':' + value + ';';
        }
        return result;
    }
    std::optional<Style> read_style(
        Node node, const std::vector<Node>& dxfs, const mirrorfly::SpreadsheetColors& colors)
    {
        Style result;
        for (auto element : node.children())
        {
            const auto id = element.attribute("dxfId").as_uint(0xffffffff);
            const std::string type = element.attribute("type").value();
            if (local(element.name()) != "tableStyleElement" || id >= dxfs.size() || result.count(type) ||
                element.attribute("size").as_uint(1) != 1)
                return {};
            auto& format = result[type];
            for (auto component : dxfs[id].children())
            {
                const auto name = local(component.name());
                if (name == "font")
                {
                    for (auto value : component.children())
                    {
                        if (local(value.name()) == "b")
                            format["bold"] = value.attribute("val").as_bool(true) ? "1" : "0";
                        else if (local(value.name()) == "color")
                            format["text"] = colors.resolve(value);
                        else
                            return {};
                    }
                }
                else if (name == "fill")
                {
                    const auto pattern = child(component, "patternFill");
                    if (count(component) != 1 ||
                        std::string(pattern.attribute("patternType").value()) != "solid")
                        return {};
                    format["fill"] = colors.resolve(child(pattern, "fgColor"));
                }
                else
                    return {};
            }
        }
        return valid_style(result) ? std::optional<Style>(result) : std::nullopt;
    }
    std::map<std::string, Style> read_styles(Node root, const mirrorfly::SpreadsheetColors& colors)
    {
        std::vector<Node> dxfs;
        for (auto dxf : child(root, "dxfs").children())
            dxfs.push_back(dxf);
        std::map<std::string, Style> styles;
        for (auto node : child(root, "tableStyles").children())
            if (const auto style = read_style(node, dxfs, colors))
                styles.emplace(node.attribute("name").value(), *style);
        return styles;
    }
    Node ordered_style_child(Node root, const char* name)
    {
        if (auto found = child(root, name))
            return found;
        for (const auto* before : {"tableStyles", "colors", "extLst"})
            if (auto target = child(root, before))
                return root.insert_child_before(qualified(root, name).c_str(), target);
        return add(root, name);
    }
    std::string append_style(Node root, const Style& style)
    {
        for (const auto& [name, existing] : read_styles(root, mirrorfly::SpreadsheetColors(root)))
            if (existing == style)
                return name;
        const auto dxfs = ordered_style_child(root, "dxfs");
        const auto styles = ordered_style_child(root, "tableStyles");
        if (count(dxfs) + style.size() > 8192 || count(styles) >= 256)
            throw std::runtime_error("表格样式数量超过限制。");
        std::set<std::string> names;
        for (auto node : styles.children())
            names.insert(lower(node.attribute("name").value()));
        unsigned suffix = 1;
        std::string name;
        do
        {
            name = "MirrorflyTable" + std::to_string(suffix++);
        } while (names.count(lower(name)));
        const auto target = add(styles, "tableStyle");
        set(target, "name", name);
        set(target, "pivot", "0");
        set(target, "table", "1");
        set(target, "count", std::to_string(style.size()));
        unsigned index = count(dxfs);
        for (const auto& [type, format] : style)
        {
            auto element = add(target, "tableStyleElement");
            set(element, "type", type);
            set(element, "dxfId", std::to_string(index++));
            if (type == "firstRowStripe" || type == "firstColumnStripe")
                set(element, "size", "1");
            const auto dxf = add(dxfs, "dxf");
            if (format.count("bold") || format.count("text"))
            {
                const auto font = add(dxf, "font");
                if (format.count("bold"))
                    set(add(font, "b"), "val", format.at("bold"));
                if (format.count("text"))
                    set(add(font, "color"), "rgb", "FF" + format.at("text").substr(1));
            }
            if (format.count("fill"))
            {
                const auto pattern = add(add(dxf, "fill"), "patternFill");
                set(pattern, "patternType", "solid");
                set(add(pattern, "fgColor"), "rgb", "FF" + format.at("fill").substr(1));
                set(add(pattern, "bgColor"), "indexed", "64");
            }
        }
        set(dxfs, "count", std::to_string(count(dxfs)));
        set(styles, "count", std::to_string(count(styles)));
        return name;
    }
    std::string relations_path(const std::string& sheet)
    {
        const auto slash = sheet.rfind('/');
        return sheet.substr(0, slash + 1) + "_rels/" + sheet.substr(slash + 1) + ".rels";
    }
    bool has_unsupported_formulas(const SpreadsheetDocument& document)
    {
        for (std::size_t index = 0; index < document.sheets.size(); ++index)
            for (const auto& [address, cell] : document.sheets[index].cells)
                if (cell.formula_cell &&
                    !spreadsheet_cell_properties(document, index, address).formula_supported)
                    return true;
        return false;
    }
}

namespace mirrorfly
{
    std::string spreadsheet_tables_key(const std::vector<SpreadsheetTable>& tables)
    {
        std::ostringstream out;
        for (const auto& table : tables)
            out << reference(table.range) << ':' << table.name.size() << ':' << table.name << ':'
                << table.path.size() << ':' << table.path << ':' << table.id << ':' << table.supported
                << table.row_stripes << table.column_stripes << table.first_column << table.last_column << ':'
                << style_key(table.style) << '|';
        return out.str();
    }
    bool valid_spreadsheet_table_headers(const SpreadsheetDocument& document, std::size_t sheet,
        const SpreadsheetTable& table,
        const std::map<std::size_t, std::map<SpreadsheetAddress, SpreadsheetValue>>* edits)
    {
        std::set<std::string> names;
        bool renamed = false;
        for (auto column = table.range.first.column; column <= table.range.last.column; ++column)
        {
            const SpreadsheetAddress address{table.range.first.row, column};
            auto value = spreadsheet_source_value(document, sheet, address);
            if (edits)
            {
                // The proposed map replaces all current edits, including restorations to the source.
                const auto original = document.sheets[sheet].cells.find(address);
                value = {};
                if (original != document.sheets[sheet].cells.end())
                    value = original->second.value;
                if (original != document.sheets[sheet].cells.end() && original->second.formula_cell)
                    value.kind = SpreadsheetValueKind::Formula;
                const auto changes = edits->find(sheet);
                if (changes != edits->end())
                {
                    const auto found = changes->second.find(address);
                    if (found != changes->second.end())
                        value = found->second;
                }
            }
            if (!valid_header(value) || !names.insert(lower(value.text)).second)
                return false;
            renamed = renamed || value.text != spreadsheet_source_value(document, sheet, address).text;
        }
        return !renamed || !has_unsupported_formulas(document);
    }
    SpreadsheetEditResult validate_spreadsheet_tables(
        const SpreadsheetDocument& document, std::size_t sheet, const SpreadsheetFeatures& features)
    {
        const auto& original = document.sheets[sheet].features;
        if (features.tables_supported != original.tables_supported)
            return {SpreadsheetError::InvalidValue, "不能修改表格导入能力标记。", false};
        if (!original.tables_supported)
        {
            if (spreadsheet_tables_key(original.tables) != spreadsheet_tables_key(features.tables))
                return {SpreadsheetError::ReadOnly, "导入表格含未支持的结构，请保留原表格。", false};
            for (auto merge : features.merges)
                for (const auto& table : features.tables)
                    if (overlaps(merge, table.range))
                        return {SpreadsheetError::ReadOnly, "不能在导入表格内合并单元格。", false};
            return {};
        }
        if (features.tables.size() > 32)
            return {SpreadsheetError::TooLarge, "每张工作表最多 32 个表格对象。", false};
        if (!features.tables.empty() && document.styles_path.empty())
            return {
                SpreadsheetError::InvalidValue, "此工作簿没有样式表，请先设置并保存一次单元格格式。", false};
        std::set<std::string> names;
        for (std::size_t index = 0; index < document.sheets.size(); ++index)
            if (index != sheet)
                for (const auto& table : spreadsheet_features(document, index).tables)
                    names.insert(lower(table.name));
        if (document.original_parts && !document.workbook_path.empty())
        {
            const auto xml = load(*document.original_parts, document.workbook_path);
            for (auto node : child(xml.document_element(), "definedNames").children())
                names.insert(lower(node.attribute("name").value()));
        }
        std::set<std::string> paths;
        for (std::size_t i = 0; i < features.tables.size(); ++i)
        {
            const auto& table = features.tables[i];
            const auto r = table.range;
            if (!table.supported || !valid_name(table.name) || !names.insert(lower(table.name)).second ||
                r.first.row >= r.last.row || r.first.column > r.last.column ||
                r.last.row >= maximum_spreadsheet_rows || r.last.column >= maximum_spreadsheet_columns ||
                std::uint64_t(r.last.row - r.first.row + 1) * (r.last.column - r.first.column + 1) >
                    maximum_spreadsheet_batch_cells ||
                !valid_style(table.style))
                return {SpreadsheetError::InvalidValue,
                    "表格需要唯一名称、有效样式及至少两行；最多 4096 格。", false};
            if (!table.path.empty())
            {
                const auto found =
                    std::find_if(original.tables.begin(), original.tables.end(), [&](const auto& source)
                {
                    return source.path == table.path && source.id == table.id;
                });
                if (found == original.tables.end() || !paths.insert(table.path).second)
                    return {SpreadsheetError::InvalidValue, "不能改变导入表格的部件标识。", false};
            }
            else if (table.id)
                return {SpreadsheetError::InvalidValue, "新表格不能指定导入标识。", false};
            if (!valid_spreadsheet_table_headers(document, sheet, table))
                return {SpreadsheetError::InvalidValue,
                    "表格首行需为非空、互不重复的文字表头（每列最多 255 字）。", false};
            for (auto merge : features.merges)
                if (overlaps(r, merge))
                    return {SpreadsheetError::InvalidValue, "表格不能与合并单元格重叠。", false};
            for (auto protected_range : document.sheets[sheet].protected_ranges)
                if (overlaps(r, protected_range))
                    return {SpreadsheetError::ReadOnly, "表格不能与受保护区域重叠。", false};
            for (std::size_t j = 0; j < i; ++j)
                if (overlaps(r, features.tables[j].range))
                    return {SpreadsheetError::InvalidValue, "表格对象不能互相重叠。", false};
            if (features.filter && overlaps(r, *features.filter) &&
                reference(r) != reference(*features.filter))
                return {SpreadsheetError::InvalidValue, "表格内的筛选范围必须覆盖整个表格。", false};
        }
        const auto& before = spreadsheet_features(document, sheet).tables;
        if (spreadsheet_tables_key(before) != spreadsheet_tables_key(features.tables) &&
            has_unsupported_formulas(document))
            for (const auto& table : before)
            {
                const auto unchanged =
                    std::find_if(features.tables.begin(), features.tables.end(), [&](const auto& proposed)
                {
                    return proposed.name == table.name && reference(proposed.range) == reference(table.range);
                });
                if (unchanged == features.tables.end())
                    return {SpreadsheetError::ReadOnly, "存在未支持的公式，暂不能移除、改名或调整表格范围。",
                        false};
            }
        return {};
    }
    SpreadsheetFormat spreadsheet_table_format(
        const SpreadsheetFeatures& features, SpreadsheetAddress address)
    {
        SpreadsheetFormat result;
        for (const auto& table : features.tables)
        {
            if (!table.supported || !contains(table.range, address))
                continue;
            const auto apply = [&](const char* name)
            {
                const auto found = table.style.find(name);
                if (found != table.style.end())
                    for (const auto& item : found->second)
                        result[item.first] = item.second;
            };
            apply("wholeTable");
            if (table.column_stripes && (address.column - table.range.first.column) % 2 == 0)
                apply("firstColumnStripe");
            if (table.row_stripes && address.row > table.range.first.row &&
                (address.row - table.range.first.row - 1) % 2 == 0)
                apply("firstRowStripe");
            if (table.last_column && address.column == table.range.last.column)
                apply("lastColumn");
            if (table.first_column && address.column == table.range.first.column)
                apply("firstColumn");
            if (address.row == table.range.first.row)
                apply("headerRow");
            break;
        }
        return result;
    }
    bool spreadsheet_filter_in_table(const SpreadsheetFeatures& features)
    {
        return features.filter &&
            std::any_of(features.tables.begin(), features.tables.end(), [&](const auto& table)
        {
            return table.supported && reference(table.range) == reference(*features.filter);
        });
    }
    void read_spreadsheet_tables(SpreadsheetSheet& sheet, const std::vector<std::string>& paths,
        const std::vector<OfficePart>& parts, const std::string& styles_path, std::size_t& xml_nodes,
        const SpreadsheetColors& colors)
    {
        if (paths.empty())
            return;
        if (paths.size() > 32)
            throw std::runtime_error("工作表的表格数量超过 32 个。");
        const auto styles = styles_path.empty()
            ? std::map<std::string, Style>{}
            : read_styles(load(parts, styles_path).document_element(), colors);
        auto& features = sheet.features;
        for (const auto& path : paths)
        {
            auto xml = load(parts, path, &xml_nodes);
            const auto root = xml.document_element();
            const auto bounds = range(root.attribute("ref").value());
            if (local(root.name()) != "table" || !bounds)
                throw std::runtime_error("表格范围或根节点无效。");
            SpreadsheetTable table;
            table.path = path;
            table.range = *bounds;
            table.id = root.attribute("id").as_uint();
            table.name = root.attribute("displayName").value();
            table.supported = table.id && valid_name(table.name) &&
                table.name == root.attribute("name").value() &&
                root.attribute("headerRowCount").as_uint(1) == 1 &&
                root.attribute("totalsRowCount").as_uint() == 0 &&
                std::string(root.attribute("tableType").as_string("worksheet")) == "worksheet" &&
                bounds->first.row < bounds->last.row &&
                std::uint64_t(bounds->last.row - bounds->first.row + 1) *
                        (bounds->last.column - bounds->first.column + 1) <=
                    maximum_spreadsheet_batch_cells;
            if (part(parts, relations_path(path)))
                table.supported = false;
            const std::set<std::string> attributes{"xmlns", "id", "name", "displayName", "ref",
                "headerRowCount", "totalsRowCount", "totalsRowShown", "tableType"};
            for (auto attribute : root.attributes())
                if (!attributes.count(attribute.name()))
                    table.supported = false;
            for (auto node : root.children())
                if (local(node.name()) != "autoFilter" && local(node.name()) != "tableColumns" &&
                    local(node.name()) != "tableStyleInfo")
                    table.supported = false;
            const auto info = child(root, "tableStyleInfo");
            if (count(info) ||
                !only_attributes(info,
                    {"name", "showRowStripes", "showColumnStripes", "showFirstColumn", "showLastColumn"}) ||
                !only_attributes(child(root, "tableColumns"), {"count"}))
                table.supported = false;
            table.row_stripes = info.attribute("showRowStripes").as_bool();
            table.column_stripes = info.attribute("showColumnStripes").as_bool();
            table.first_column = info.attribute("showFirstColumn").as_bool();
            table.last_column = info.attribute("showLastColumn").as_bool();
            const auto style = styles.find(info.attribute("name").value());
            if (style != styles.end())
                table.style = style->second;
            else
                table.supported = false;
            auto column = bounds->first.column;
            std::set<std::string> headers;
            std::set<unsigned> ids;
            for (auto node : child(root, "tableColumns").children())
            {
                const std::string name = node.attribute("name").value();
                const auto cell = sheet.cells.find({bounds->first.row, column++});
                if (local(node.name()) != "tableColumn" || count(node) ||
                    !valid_header({SpreadsheetValueKind::Text, name}) ||
                    !headers.insert(lower(name)).second || !node.attribute("id").as_uint() ||
                    !ids.insert(node.attribute("id").as_uint()).second || cell == sheet.cells.end() ||
                    cell->second.formula_cell || cell->second.value.kind != SpreadsheetValueKind::Text ||
                    cell->second.value.text != name)
                    table.supported = false;
                for (auto attribute : node.attributes())
                    if (local(attribute.name()) != "id" && local(attribute.name()) != "name")
                        table.supported = false;
            }
            if (column != bounds->last.column + 1)
                table.supported = false;
            const auto filter = child(root, "autoFilter");
            if (filter &&
                (!only_attributes(filter, {"ref"}) ||
                    std::string(filter.attribute("ref").value()) != reference(*bounds)))
                table.supported = false;
            if (filter && count(filter))
            {
                SpreadsheetFeatures criteria;
                criteria.filter = *bounds;
                read_spreadsheet_filters(filter, criteria);
                if (!table.supported || features.filter || !criteria.filter_supported ||
                    std::string(filter.attribute("ref").value()) != reference(*bounds))
                {
                    table.supported = false;
                    features.filter_supported = false;
                }
                else
                {
                    features.filter = *bounds;
                    features.filters = std::move(criteria.filters);
                    for (auto row = bounds->first.row + 1; row <= bounds->last.row; ++row)
                        for (const auto& criterion : features.filters)
                        {
                            const auto cell = sheet.cells.find({row, criterion.column});
                            if (!spreadsheet_filter_matches(
                                    cell == sheet.cells.end() ? SpreadsheetCell{} : cell->second, criterion))
                                features.hidden_rows.erase(row);
                        }
                }
            }
            features.tables_supported = features.tables_supported && table.supported;
            sheet.rows = std::max(sheet.rows, bounds->last.row + 1);
            sheet.columns = std::max(sheet.columns, bounds->last.column + 1);
            features.tables.push_back(std::move(table));
        }
    }
    SpreadsheetPackageResult write_spreadsheet_tables(
        const SpreadsheetDocument& document, std::vector<OfficePart> parts)
    {
        try
        {
            const auto tables_changed = [&](std::size_t index)
            {
                const auto& source = document.sheets[index].features;
                const auto& current = spreadsheet_features(document, index);
                if (spreadsheet_tables_key(source.tables) != spreadsheet_tables_key(current.tables) ||
                    document.feature_edits.count(index))
                    return true;
                const auto edits = document.edits.find(index);
                if (edits == document.edits.end())
                    return false;
                for (const auto& table : current.tables)
                    for (const auto& [address, value] : edits->second)
                        if (address.row == table.range.first.row &&
                            address.column >= table.range.first.column &&
                            address.column <= table.range.last.column)
                            return true;
                return false;
            };
            bool needs_write = false;
            for (std::size_t index = 0; index < document.sheets.size(); ++index)
            {
                const auto& source = document.sheets[index].features;
                const auto& current = spreadsheet_features(document, index);
                if (current.tables_supported && (!source.tables.empty() || !current.tables.empty()) &&
                    tables_changed(index))
                    needs_write = true;
            }
            if (!needs_write)
                return {SpreadsheetError::None, {}, std::move(parts)};
            std::set<unsigned> ids;
            for (const auto& sheet : document.sheets)
                for (const auto& table : sheet.features.tables)
                    ids.insert(table.id);
            pugi::xml_document styles;
            if (!document.styles_path.empty())
                styles = load(parts, document.styles_path);
            auto types = load(parts, "[Content_Types].xml");
            bool wrote = false;
            for (std::size_t index = 0; index < document.sheets.size(); ++index)
            {
                const auto& source = document.sheets[index];
                const auto& features = spreadsheet_features(document, index);
                if (!features.tables_supported || (features.tables.empty() && source.features.tables.empty()))
                    continue;
                const auto checked = validate_spreadsheet_tables(document, index, features);
                if (checked.error != SpreadsheetError::None)
                    return {checked.error, checked.message, {}};
                if (!tables_changed(index))
                    continue;
                if (!styles.document_element())
                    return {SpreadsheetError::InvalidValue, "创建表格需要工作簿样式表。", {}};
                wrote = true;
                auto sheet = load(parts, source.path);
                auto root = sheet.document_element();
                const auto relpath = relations_path(source.path);
                pugi::xml_document rels;
                if (part(parts, relpath))
                    rels = load(parts, relpath);
                else
                    set(rels.append_child("Relationships"), "xmlns",
                        "http://schemas.openxmlformats.org/package/2006/relationships");
                std::set<std::string> old_relations;
                for (auto node : child(root, "tableParts").children())
                    for (auto attr : node.attributes())
                        if (local(attr.name()) == "id")
                            old_relations.insert(attr.value());
                std::set<std::string> used_relations;
                for (auto node = rels.document_element().first_child(); node;)
                {
                    const auto next = node.next_sibling();
                    if (old_relations.count(node.attribute("Id").value()))
                        rels.document_element().remove_child(node);
                    else
                        used_relations.insert(node.attribute("Id").value());
                    node = next;
                }
                root.remove_child(child(root, "tableParts"));
                Node container;
                if (!features.tables.empty())
                {
                    const auto ext = child(root, "extLst");
                    container = ext ? root.insert_child_before(qualified(root, "tableParts").c_str(), ext)
                                    : add(root, "tableParts");
                    set(container, "count", std::to_string(features.tables.size()));
                }
                for (const auto& table : source.features.tables)
                {
                    const auto kept =
                        std::any_of(features.tables.begin(), features.tables.end(), [&](const auto& current)
                    {
                        return current.path == table.path;
                    });
                    if (kept)
                        continue;
                    parts.erase(std::remove_if(parts.begin(), parts.end(),
                                    [&](const auto& item)
                    {
                        return item.path == table.path;
                    }),
                        parts.end());
                    for (auto node = types.document_element().first_child(); node;)
                    {
                        const auto next = node.next_sibling();
                        if (std::string(node.attribute("PartName").value()) == "/" + table.path)
                            types.document_element().remove_child(node);
                        node = next;
                    }
                }
                for (const auto& table : features.tables)
                {
                    auto path = table.path;
                    auto id = table.id;
                    if (path.empty())
                    {
                        id = 1;
                        while (ids.count(id) ||
                            part(parts, "xl/tables/mirrorflyTable" + std::to_string(id) + ".xml"))
                            ++id;
                        ids.insert(id);
                        path = "xl/tables/mirrorflyTable" + std::to_string(id) + ".xml";
                        const auto type = add(types.document_element(), "Override");
                        set(type, "PartName", "/" + path);
                        set(type, "ContentType",
                            "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml");
                    }
                    std::string relation = "MirrorflyTable" + std::to_string(id);
                    while (used_relations.count(relation))
                        relation += '_';
                    used_relations.insert(relation);
                    auto link = add(rels.document_element(), "Relationship");
                    set(link, "Id", relation);
                    set(link, "Type", std::string(relationship_namespace) + "/table");
                    set(link, "Target", "/" + path);
                    auto table_part = add(container, "tablePart");
                    set(table_part, "xmlns:r", relationship_namespace);
                    set(table_part, "r:id", relation);
                    pugi::xml_document xml;
                    const auto target = xml.append_child("table");
                    set(target, "xmlns", spreadsheet_namespace);
                    set(target, "id", std::to_string(id));
                    set(target, "name", table.name);
                    set(target, "displayName", table.name);
                    set(target, "ref", reference(table.range));
                    set(target, "totalsRowShown", "0");
                    const auto filter = add(target, "autoFilter");
                    set(filter, "ref", reference(table.range));
                    if (features.filter && reference(*features.filter) == reference(table.range))
                        write_spreadsheet_filters(filter, features);
                    const auto columns = add(target, "tableColumns");
                    set(columns, "count",
                        std::to_string(table.range.last.column - table.range.first.column + 1));
                    for (auto column = table.range.first.column; column <= table.range.last.column; ++column)
                    {
                        const auto item = add(columns, "tableColumn");
                        set(item, "id", std::to_string(column - table.range.first.column + 1));
                        set(item, "name",
                            spreadsheet_source_value(document, index, {table.range.first.row, column}).text);
                    }
                    const auto info = add(target, "tableStyleInfo");
                    set(info, "name", append_style(styles.document_element(), table.style));
                    set(info, "showFirstColumn", table.first_column ? "1" : "0");
                    set(info, "showLastColumn", table.last_column ? "1" : "0");
                    set(info, "showRowStripes", table.row_stripes ? "1" : "0");
                    set(info, "showColumnStripes", table.column_stripes ? "1" : "0");
                    store(parts, path, xml);
                }
                store(parts, source.path, sheet);
                store(parts, relpath, rels);
            }
            if (wrote)
            {
                store(parts, document.styles_path, styles);
                store(parts, "[Content_Types].xml", types);
            }
            return {SpreadsheetError::None, {}, std::move(parts)};
        }
        catch (const std::exception& error)
        {
            return {SpreadsheetError::InvalidValue, error.what(), {}};
        }
    }
}
