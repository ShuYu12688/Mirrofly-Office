#include "spreadsheet_features.hpp"
#include "spreadsheet_colors.hpp"
#include "spreadsheet_filters.hpp"
#include "spreadsheet_tables.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <utf8.h>

namespace
{
    using namespace mirrorfly;
    using Node = pugi::xml_node;
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
        std::string prefix = parent.name();
        const auto colon = prefix.find(':');
        return colon == std::string::npos ? name : prefix.substr(0, colon + 1) + name;
    }
    Node add(Node parent, const char* name)
    {
        return parent.append_child(qualified(parent, name).c_str());
    }
    void set(Node node, const char* key, const std::string& value)
    {
        auto attr = node.attribute(key);
        if (!attr)
            attr = node.append_attribute(key);
        attr = value.c_str();
    }
    Node ensure(Node parent, const char* name)
    {
        auto node = child(parent, name);
        return node ? node : add(parent, name);
    }
    bool contains(SpreadsheetRange r, SpreadsheetAddress a)
    {
        return a.row >= r.first.row && a.row <= r.last.row && a.column >= r.first.column &&
            a.column <= r.last.column;
    }
    std::string reference(SpreadsheetRange r)
    {
        return spreadsheet_address(r.first) + ":" + spreadsheet_address(r.last);
    }
    std::optional<SpreadsheetRange> range(const std::string& text)
    {
        const auto colon = text.find(':');
        auto first = parse_spreadsheet_address(text.substr(0, colon));
        auto last = colon == std::string::npos ? first : parse_spreadsheet_address(text.substr(colon + 1));
        if (!first || !last || last->row < first->row || last->column < first->column)
            return {};
        return SpreadsheetRange{*first, *last};
    }
    bool bounded(SpreadsheetRange r)
    {
        return r.first.row <= r.last.row && r.first.column <= r.last.column &&
            r.last.row < maximum_spreadsheet_rows && r.last.column < maximum_spreadsheet_columns &&
            std::uint64_t(r.last.row - r.first.row + 1) * (r.last.column - r.first.column + 1) <=
            maximum_spreadsheet_batch_cells;
    }
    bool color(const std::string& text)
    {
        return text.size() == 7 && text.front() == '#' &&
            text.find_first_not_of("0123456789ABCDEFabcdef", 1) == std::string::npos;
    }
    std::optional<double> number(const std::string& text)
    {
        char* end = nullptr;
        auto value = std::strtod(text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value))
            return {};
        return value;
    }
    std::string numeric(double value)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(17);
        out << value;
        return out.str();
    }
    std::string key(const SpreadsheetFeatures& f, const std::string& category)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(17);
        if (category == "merges")
            for (auto r : f.merges)
                out << reference(r) << ';';
        if (category == "conditions")
            for (const auto& c : f.conditions)
                out << reference(c.range) << ':' << c.comparison << ':' << c.value << ':' << c.fill << ':'
                    << c.stop_if_true << ';';
        if (category == "filter")
        {
            out << (f.filter ? reference(*f.filter) : "") << ':';
            for (const auto& filter : f.filters)
            {
                out << filter.column << ':' << filter.comparison << ':' << filter.values.size() << ':';
                for (const auto& value : filter.values)
                    out << value.size() << ':' << value;
            }
        }
        if (category == "panes")
            out << f.frozen_rows << ':' << f.frozen_columns;
        if (category == "tables")
            out << spreadsheet_tables_key(f.tables);
        if (category == "hidden")
        {
            for (auto row : f.hidden_rows)
                out << 'r' << row << ';';
            for (auto column : f.hidden_columns)
                out << 'c' << column << ';';
        }
        return out.str();
    }
    OfficePart& part(std::vector<OfficePart>& parts, const std::string& path)
    {
        for (auto& item : parts)
            if (item.path == path)
                return item;
        throw std::runtime_error("工作簿缺少必要部件。");
    }
    pugi::xml_document load(const std::vector<OfficePart>& parts, const std::string& path)
    {
        for (const auto& item : parts)
        {
            if (item.path != path)
                continue;
            pugi::xml_document xml;
            if (item.bytes.size() <= maximum_spreadsheet_xml_bytes &&
                xml.load_buffer(item.bytes.data(), item.bytes.size()))
                return xml;
        }
        throw std::runtime_error("工作表设置无法读取。");
    }
    std::string save(const pugi::xml_document& xml)
    {
        std::ostringstream out;
        xml.save(out, "", pugi::format_raw, pugi::encoding_utf8);
        return out.str();
    }
    Node row(Node data, std::uint32_t index)
    {
        for (auto item : data.children())
        {
            const auto r = item.attribute("r").as_uint();
            if (r == index + 1)
                return item;
            if (r > index + 1)
            {
                auto node = data.insert_child_before(qualified(data, "row").c_str(), item);
                set(node, "r", std::to_string(index + 1));
                return node;
            }
        }
        auto node = add(data, "row");
        set(node, "r", std::to_string(index + 1));
        return node;
    }
    void order_sheet(Node root)
    {
        const std::vector<std::string> names = {"sheetPr", "dimension", "sheetViews", "sheetFormatPr", "cols",
            "sheetData", "sheetCalcPr", "sheetProtection", "protectedRanges", "scenarios", "autoFilter",
            "sortState", "dataConsolidate", "customSheetViews", "mergeCells", "phoneticPr",
            "conditionalFormatting", "dataValidations", "hyperlinks", "printOptions", "pageMargins",
            "pageSetup", "headerFooter", "rowBreaks", "colBreaks", "customProperties", "cellWatches",
            "ignoredErrors", "smartTags", "drawing", "legacyDrawing", "legacyDrawingHF", "picture",
            "oleObjects", "controls", "webPublishItems", "tableParts", "extLst"};
        std::vector<Node> nodes;
        for (auto node : root.children())
            if (node.type() == pugi::node_element)
                nodes.push_back(node);
        std::stable_sort(nodes.begin(), nodes.end(), [&](Node a, Node b)
        {
            return std::find(names.begin(), names.end(), local(a.name())) <
                std::find(names.begin(), names.end(), local(b.name()));
        });
        for (auto node : nodes)
            root.append_move(node);
    }
}

namespace mirrorfly
{
    const SpreadsheetFeatures& spreadsheet_features(const SpreadsheetDocument& document, std::size_t sheet)
    {
        static const SpreadsheetFeatures empty;
        const auto found = document.feature_edits.find(sheet);
        if (found != document.feature_edits.end())
            return found->second;
        if (sheet < document.sheets.size())
            return document.sheets[sheet].features;
        return empty;
    }
    SpreadsheetEditResult apply_spreadsheet_features(
        SpreadsheetDocument& document, std::size_t sheet, const SpreadsheetFeatures& features)
    {
        if (document.read_only || sheet >= document.sheets.size() || !document.sheets[sheet].editable)
            return {SpreadsheetError::ReadOnly, "工作表设置不可修改。", false};
        try
        {
            const auto& original = document.sheets[sheet].features;
            const auto& before = spreadsheet_features(document, sheet);
            const auto tables = validate_spreadsheet_tables(document, sheet, features);
            if (tables.error != SpreadsheetError::None)
                return tables;
            if (features.conditions_supported != original.conditions_supported ||
                features.filter_supported != original.filter_supported ||
                features.panes_supported != original.panes_supported)
                return {SpreadsheetError::InvalidValue, "不能修改导入能力标记。", false};
            if (features.merges.size() > 256 || features.conditions.size() > 32 ||
                features.hidden_rows.size() > 4096 || features.hidden_columns.size() > 4096 ||
                features.frozen_rows > 16 || features.frozen_columns > 8)
                return {SpreadsheetError::TooLarge, "合并、规则、隐藏或冻结范围超过限制。", false};
            if ((!original.conditions_supported &&
                    key(features, "conditions") != key(original, "conditions")) ||
                (!original.filter_supported && key(features, "filter") != key(original, "filter")) ||
                (!original.panes_supported && key(features, "panes") != key(original, "panes")))
                return {SpreadsheetError::ReadOnly, "此导入文件包含复杂设置，请保留原设置。", false};
            if (!features.conditions.empty() && document.styles_path.empty())
                return {SpreadsheetError::InvalidValue, "条件格式需要样式表，请先保存基本样式。", false};
            for (auto r : features.merges)
                if ((r.first.row < features.frozen_rows && r.last.row >= features.frozen_rows) ||
                    (r.first.column < features.frozen_columns && r.last.column >= features.frozen_columns))
                    return {SpreadsheetError::InvalidValue, "冻结线不能穿过合并区域。", false};
            for (std::size_t i = 0; i < features.merges.size(); ++i)
            {
                const auto r = features.merges[i];
                const bool imported =
                    std::any_of(original.merges.begin(), original.merges.end(), [&](auto source)
                {
                    return reference(source) == reference(r);
                });
                if (!bounded(r) && !imported)
                    return {SpreadsheetError::TooLarge, "一次合并最多 4096 格。", false};
                for (std::size_t j = 0; j < i; ++j)
                {
                    const auto a = features.merges[j];
                    if (r.first.row <= a.last.row && a.first.row <= r.last.row &&
                        r.first.column <= a.last.column && a.first.column <= r.last.column)
                        return {SpreadsheetError::InvalidValue, "合并范围不能重叠。", false};
                }
                if (imported)
                {
                    const auto edits = document.edits.find(sheet);
                    if (edits != document.edits.end())
                        for (const auto& [address, value] : edits->second)
                            if (contains(r, address) &&
                                (address.row != r.first.row || address.column != r.first.column) &&
                                value.kind != SpreadsheetValueKind::Empty)
                                return {SpreadsheetError::InvalidValue, "合并区域内有新增内容，请先清空。",
                                    false};
                    continue;
                }
                for (auto y = r.first.row; y <= r.last.row; ++y)
                    for (auto x = r.first.column; x <= r.last.column; ++x)
                    {
                        const auto cell = spreadsheet_cell_properties(document, sheet, {y, x});
                        if ((cell.formula_cell && !cell.formula_supported) ||
                            ((y != r.first.row || x != r.first.column) &&
                                (cell.formula_cell || cell.value.kind != SpreadsheetValueKind::Empty)))
                            return {SpreadsheetError::InvalidValue,
                                "合并仅保留左上格；请先清空其他格，公式区域不可合并。", false};
                        for (auto p : document.sheets[sheet].protected_ranges)
                            if (contains(p, {y, x}))
                                return {SpreadsheetError::ReadOnly, "合并范围与受保护区域重叠。", false};
                    }
            }
            for (const auto& c : features.conditions)
                if (!bounded(c.range) || !std::isfinite(c.value) || !color(c.fill) ||
                    (c.comparison != "greaterThan" && c.comparison != "lessThan" && c.comparison != "equal"))
                    return {SpreadsheetError::InvalidValue, "条件格式参数无效。", false};
            if ((features.filter && !bounded(*features.filter)) || !valid_spreadsheet_filters(features))
                return {SpreadsheetError::InvalidValue, "筛选区域或文本无效（最多 4096 格）。", false};
            if ((!features.hidden_rows.empty() &&
                    *features.hidden_rows.rbegin() >= maximum_spreadsheet_rows) ||
                (!features.hidden_columns.empty() &&
                    *features.hidden_columns.rbegin() >= maximum_spreadsheet_columns))
                return {SpreadsheetError::InvalidValue, "隐藏行列索引无效。", false};
            bool changed = false;
            for (auto category : {"merges", "conditions", "filter", "panes", "hidden", "tables"})
                changed = changed || key(features, category) != key(before, category);
            if (changed)
            {
                const bool visibility_changed = key(features, "filter") != key(before, "filter") ||
                    features.hidden_rows != before.hidden_rows;
                auto proposed = document.feature_edits;
                proposed[sheet] = features;
                document.feature_edits.swap(proposed);
                document.caches_stale = document.caches_stale || visibility_changed;
                for (const auto& table : features.tables)
                {
                    document.sheets[sheet].rows =
                        std::max(document.sheets[sheet].rows, table.range.last.row + 1);
                    document.sheets[sheet].columns =
                        std::max(document.sheets[sheet].columns, table.range.last.column + 1);
                }
            }
            return {SpreadsheetError::None, {}, changed};
        }
        catch (const std::bad_alloc&)
        {
            return {SpreadsheetError::TooLarge, "设置超出内存预算。", false};
        }
    }
    bool spreadsheet_row_visible(const SpreadsheetDocument& document, std::size_t sheet, std::uint32_t row,
        const SpreadsheetCalculation* calculation)
    {
        const auto& f = spreadsheet_features(document, sheet);
        if (f.hidden_rows.count(row))
            return false;
        if (!f.filter_supported || !f.filter || row <= f.filter->first.row || row > f.filter->last.row)
            return true;
        for (const auto& filter : f.filters)
            if (!spreadsheet_filter_matches(
                    spreadsheet_cell(document, sheet, {row, filter.column}, calculation), filter))
                return false;
        return true;
    }
    std::string spreadsheet_conditional_fill(const SpreadsheetDocument& document, std::size_t sheet,
        SpreadsheetAddress address, const SpreadsheetCalculation* calculation)
    {
        const auto cell = spreadsheet_cell(document, sheet, address, calculation);
        if ((cell.formula_cell && !cell.formula_supported) || cell.value.kind != SpreadsheetValueKind::Number)
            return {};
        const auto value = number(cell.value.text);
        if (!value)
            return {};
        for (const auto& c : spreadsheet_features(document, sheet).conditions)
            if (contains(c.range, address) &&
                ((c.comparison == "greaterThan" && *value > c.value) ||
                    (c.comparison == "lessThan" && *value < c.value) ||
                    (c.comparison == "equal" && *value == c.value)))
                return c.fill;
        return {};
    }
    void read_spreadsheet_features(SpreadsheetSheet& sheet, Node root, const std::vector<OfficePart>& parts,
        const std::string& styles_path, const SpreadsheetColors& colors)
    {
        auto& f = sheet.features;
        for (auto node : child(root, "mergeCells").children())
            if (auto r = range(node.attribute("ref").value()))
                f.merges.push_back(*r);
        if (f.merges.size() > 256)
            throw std::runtime_error("工作表合并区域超过 256 个。");
        for (auto r : f.merges)
        {
            sheet.rows = std::max(sheet.rows, r.last.row + 1);
            sheet.columns = std::max(sheet.columns, r.last.column + 1);
        }
        auto pane = child(child(child(root, "sheetViews"), "sheetView"), "pane");
        if (pane)
        {
            f.panes_supported = std::string(pane.attribute("state").value()) == "frozen" &&
                pane.attribute("xSplit").as_double() <= 8 && pane.attribute("ySplit").as_double() <= 16;
            if (f.panes_supported)
            {
                f.frozen_columns = pane.attribute("xSplit").as_uint();
                f.frozen_rows = pane.attribute("ySplit").as_uint();
            }
        }
        auto filter = child(root, "autoFilter");
        if (filter)
        {
            auto r = range(filter.attribute("ref").value());
            f.filter_supported = r && bounded(*r);
            if (f.filter_supported)
            {
                f.filter = *r;
                read_spreadsheet_filters(filter, f);
            }
        }
        for (auto node : child(root, "sheetData").children())
        {
            const auto index = node.attribute("r").as_uint();
            if (index && node.attribute("hidden").as_bool())
            {
                bool filtered_out = false;
                if (f.filter_supported && f.filter && index - 1 > f.filter->first.row &&
                    index - 1 <= f.filter->last.row)
                {
                    for (const auto& criterion : f.filters)
                    {
                        auto cell = sheet.cells.find({index - 1, criterion.column});
                        if (!spreadsheet_filter_matches(
                                cell == sheet.cells.end() ? SpreadsheetCell{} : cell->second, criterion))
                            filtered_out = true;
                    }
                }
                if (!filtered_out)
                    f.hidden_rows.insert(index - 1);
            }
        }
        for (auto node : child(root, "cols").children())
            if (node.attribute("hidden").as_bool())
            {
                auto first = node.attribute("min").as_uint();
                auto last = node.attribute("max").as_uint();
                if (first && last >= first && last <= maximum_spreadsheet_columns)
                    for (auto i = first; i <= last; ++i)
                        f.hidden_columns.insert(i - 1);
            }
        pugi::xml_document styles;
        if (!styles_path.empty())
            styles = load(parts, styles_path);
        std::vector<Node> dxfs;
        for (auto node : child(styles.document_element(), "dxfs").children())
            dxfs.push_back(node);
        std::map<unsigned, SpreadsheetCondition> ordered_conditions;
        for (auto node : root.children())
        {
            if (local(node.name()) != "conditionalFormatting")
                continue;
            const auto r = range(node.attribute("sqref").value());
            for (auto rule : node.children())
            {
                const auto op = std::string(rule.attribute("operator").value());
                const auto value = number(child(rule, "formula").child_value());
                const auto index = rule.attribute("dxfId").as_uint(0xffffffff);
                const auto priority = rule.attribute("priority").as_uint();
                std::string fill;
                if (index < dxfs.size())
                {
                    auto pattern = child(child(dxfs[index], "fill"), "patternFill");
                    fill = colors.resolve(child(pattern, "fgColor"));
                    if (std::string(pattern.attribute("patternType").value()) != "solid" ||
                        child(dxfs[index], "font") || child(dxfs[index], "border") ||
                        child(dxfs[index], "numFmt") || child(dxfs[index], "alignment"))
                        f.conditions_supported = false;
                    for (auto item : dxfs[index].children())
                        if (local(item.name()) != "fill")
                            f.conditions_supported = false;
                }
                if (!r || !bounded(*r) || !value || std::string(rule.attribute("type").value()) != "cellIs" ||
                    (op != "greaterThan" && op != "lessThan" && op != "equal") || !color(fill) || !priority ||
                    ordered_conditions.count(priority))
                {
                    f.conditions_supported = false;
                    continue;
                }
                for (auto item : rule.children())
                    if (local(item.name()) != "formula" || item != child(rule, "formula"))
                        f.conditions_supported = false;
                for (auto item : rule.attributes())
                {
                    const auto name = local(item.name());
                    if (name != "type" && name != "dxfId" && name != "priority" && name != "stopIfTrue" &&
                        name != "operator")
                        f.conditions_supported = false;
                }
                ordered_conditions.emplace(priority,
                    SpreadsheetCondition{*r, op, *value, fill, rule.attribute("stopIfTrue").as_bool()});
            }
        }
        for (const auto& [priority, condition] : ordered_conditions)
        {
            (void)priority;
            f.conditions.push_back(condition);
        }
        if (f.conditions.size() > 32)
            f.conditions_supported = false;
        if (!f.conditions_supported)
            f.conditions.clear();
    }
    SpreadsheetPackageResult write_spreadsheet_features(const SpreadsheetDocument& document,
        std::vector<OfficePart> parts, const SpreadsheetCalculation* calculation)
    {
        try
        {
            if (!document.feature_edits.empty())
            {
                auto validated = document;
                for (const auto& item : document.feature_edits)
                {
                    const auto checked = apply_spreadsheet_features(validated, item.first, item.second);
                    if (checked.error != SpreadsheetError::None)
                        return {checked.error, checked.message, {}};
                }
            }
            for (std::size_t index = 0; index < document.sheets.size(); ++index)
            {
                const auto& f = spreadsheet_features(document, index);
                const auto& original = document.sheets[index].features;
                if (!document.feature_edits.count(index) &&
                    (!f.filter || (document.edits.empty() && !document.caches_stale)))
                    continue;
                auto xml = load(parts, document.sheets[index].path);
                auto root = xml.document_element();
                if (key(f, "merges") != key(original, "merges"))
                {
                    root.remove_child(child(root, "mergeCells"));
                    if (!f.merges.empty())
                    {
                        auto merges = add(root, "mergeCells");
                        set(merges, "count", std::to_string(f.merges.size()));
                        for (auto r : f.merges)
                            set(add(merges, "mergeCell"), "ref", reference(r));
                    }
                }
                if (key(f, "panes") != key(original, "panes"))
                {
                    auto view = ensure(ensure(root, "sheetViews"), "sheetView");
                    set(view, "workbookViewId", "0");
                    view.remove_child(child(view, "pane"));
                    while (child(view, "selection"))
                        view.remove_child(child(view, "selection"));
                    if (f.frozen_rows || f.frozen_columns)
                    {
                        auto pane = view.prepend_child(qualified(view, "pane").c_str());
                        set(pane, "state", "frozen");
                        set(pane, "xSplit", std::to_string(f.frozen_columns));
                        set(pane, "ySplit", std::to_string(f.frozen_rows));
                        const auto active =
                            f.frozen_columns ? (f.frozen_rows ? "bottomRight" : "topRight") : "bottomLeft";
                        set(pane, "activePane", active);
                        set(pane, "topLeftCell", spreadsheet_address({f.frozen_rows, f.frozen_columns}));
                        set(add(view, "selection"), "pane", active);
                    }
                }
                if (key(f, "filter") != key(original, "filter") ||
                    key(f, "tables") != key(original, "tables"))
                {
                    root.remove_child(child(root, "autoFilter"));
                    if (f.filter && !spreadsheet_filter_in_table(f))
                    {
                        auto filter = add(root, "autoFilter");
                        set(filter, "ref", reference(*f.filter));
                        write_spreadsheet_filters(filter, f);
                    }
                }
                if ((f.filter_supported && (f.filter || original.filter)) ||
                    key(f, "hidden") != key(original, "hidden"))
                {
                    std::set<std::uint32_t> rows = f.hidden_rows;
                    rows.insert(original.hidden_rows.begin(), original.hidden_rows.end());
                    for (auto r : {f.filter, original.filter})
                        if (r && f.filter_supported)
                            for (auto y = r->first.row + 1; y <= r->last.row; ++y)
                                rows.insert(y);
                    for (auto y : rows)
                        set(row(ensure(root, "sheetData"), y), "hidden",
                            spreadsheet_row_visible(document, index, y, calculation) ? "0" : "1");
                }
                if (key(f, "hidden") != key(original, "hidden"))
                {
                    auto cols = ensure(root, "cols");
                    std::set<std::uint32_t> changed = f.hidden_columns;
                    changed.insert(original.hidden_columns.begin(), original.hidden_columns.end());
                    for (auto x : changed)
                    {
                        Node target;
                        for (auto node = cols.first_child(); node; node = node.next_sibling())
                        {
                            const auto first = node.attribute("min").as_uint();
                            const auto last = node.attribute("max").as_uint();
                            if (x + 1 < first || x + 1 > last)
                                continue;
                            target = cols.insert_copy_before(node, node);
                            if (first < x + 1)
                            {
                                auto left = cols.insert_copy_before(node, node);
                                set(left, "max", std::to_string(x));
                            }
                            if (last > x + 1)
                            {
                                auto right = cols.insert_copy_before(node, node);
                                set(right, "min", std::to_string(x + 2));
                            }
                            cols.remove_child(node);
                            break;
                        }
                        if (!target)
                            target = add(cols, "col");
                        set(target, "min", std::to_string(x + 1));
                        set(target, "max", std::to_string(x + 1));
                        set(target, "hidden", f.hidden_columns.count(x) ? "1" : "0");
                    }
                    std::vector<Node> nodes;
                    for (auto node : cols.children())
                        nodes.push_back(node);
                    std::sort(nodes.begin(), nodes.end(), [](Node a, Node b)
                    {
                        return a.attribute("min").as_uint() < b.attribute("min").as_uint();
                    });
                    for (auto node : nodes)
                        cols.append_move(node);
                }
                if (key(f, "conditions") != key(original, "conditions"))
                {
                    while (child(root, "conditionalFormatting"))
                        root.remove_child(child(root, "conditionalFormatting"));
                    if (!f.conditions.empty())
                    {
                        if (document.styles_path.empty())
                            throw std::runtime_error("请先保存一次单元格样式，再设置条件格式。");
                        auto styles = load(parts, document.styles_path);
                        auto style_root = styles.document_element();
                        auto dxfs = child(style_root, "dxfs");
                        if (!dxfs)
                        {
                            auto before = child(style_root, "tableStyles");
                            if (!before)
                                before = child(style_root, "colors");
                            if (!before)
                                before = child(style_root, "extLst");
                            if (before)
                                dxfs = style_root.insert_child_before(
                                    qualified(style_root, "dxfs").c_str(), before);
                            else
                                dxfs = add(style_root, "dxfs");
                        }
                        unsigned count = 0;
                        for (auto n : dxfs.children())
                        {
                            (void)n;
                            ++count;
                        }
                        unsigned priority = 1;
                        for (const auto& c : f.conditions)
                        {
                            if (count >= 8192)
                                throw std::runtime_error("条件样式数量超过限制。");
                            auto pattern = add(add(add(dxfs, "dxf"), "fill"), "patternFill");
                            set(pattern, "patternType", "solid");
                            set(add(pattern, "fgColor"), "rgb", "FF" + c.fill.substr(1));
                            set(add(pattern, "bgColor"), "indexed", "64");
                            auto container = add(root, "conditionalFormatting");
                            set(container, "sqref", reference(c.range));
                            auto rule = add(container, "cfRule");
                            set(rule, "type", "cellIs");
                            set(rule, "operator", c.comparison);
                            set(rule, "dxfId", std::to_string(count++));
                            set(rule, "priority", std::to_string(priority++));
                            set(rule, "stopIfTrue", c.stop_if_true ? "1" : "0");
                            add(rule, "formula").text().set(numeric(c.value).c_str());
                        }
                        set(dxfs, "count", std::to_string(count));
                        part(parts, document.styles_path).bytes = save(styles);
                    }
                }
                order_sheet(root);
                part(parts, document.sheets[index].path).bytes = save(xml);
            }
            return {SpreadsheetError::None, {}, std::move(parts)};
        }
        catch (const std::exception& e)
        {
            return {SpreadsheetError::InvalidValue, e.what(), {}};
        }
    }
}
