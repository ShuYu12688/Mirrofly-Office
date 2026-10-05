#include "spreadsheet_format.hpp"
#include "spreadsheet_colors.hpp"
#include "spreadsheet_tables.hpp"

#include <utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace
{
    using Node = pugi::xml_node;
    using Format = mirrorfly::SpreadsheetFormat;

    std::string local(const char* name)
    {
        const std::string text(name);
        const auto colon = text.find(':');
        return colon == std::string::npos ? text : text.substr(colon + 1);
    }

    Node child(Node parent, const char* name)
    {
        for (auto node : parent.children())
        {
            if (local(node.name()) == name)
                return node;
        }
        return {};
    }

    std::string qualified(Node parent, const char* name)
    {
        const std::string text(parent.name());
        const auto colon = text.find(':');
        return colon == std::string::npos ? name : text.substr(0, colon + 1) + name;
    }

    Node ensure(Node parent, const char* name)
    {
        const auto found = child(parent, name);
        return found ? found : parent.append_child(qualified(parent, name).c_str());
    }

    void set(Node node, const char* name, const std::string& value)
    {
        auto attr = node.attribute(name);
        if (!attr)
            attr = node.append_attribute(name);
        attr.set_value(value.c_str());
    }

    double number(const std::string& text, double fallback)
    {
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        return !text.empty() && end == text.c_str() + text.size() && std::isfinite(value) ? value : fallback;
    }

    bool color(const std::string& value)
    {
        return value.size() == 7 && value[0] == '#' &&
            value.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
    }

    bool format_editable(const mirrorfly::SpreadsheetDocument& document, std::size_t sheet,
        mirrorfly::SpreadsheetAddress address)
    {
        const auto cell = mirrorfly::spreadsheet_cell_properties(document, sheet, address);
        return cell.editable || cell.merged_covered;
    }

    bool valid_format(const Format& format)
    {
        if (format.size() > 32)
            return false;
        for (const auto& [key, value] : format)
        {
            if (key == "font")
            {
                if (value.empty() || value.size() > 256 || !utf8::is_valid(value.begin(), value.end()) ||
                    std::any_of(value.begin(), value.end(), [](unsigned char c)
                {
                    return c < 32;
                }))
                    return false;
            }
            else if (key == "size")
            {
                const double size = number(value, -1);
                if (size < 6 || size > 96)
                    return false;
            }
            else if (key == "fill" || key == "text" || key == "borderLeftColor" ||
                key == "borderRightColor" || key == "borderTopColor" || key == "borderBottomColor")
            {
                if (!color(value) && !(key == "fill" && value == "none"))
                    return false;
            }
            else if (key == "borderLeft" || key == "borderRight" || key == "borderTop" ||
                key == "borderBottom")
            {
                const std::set<std::string> styles{"none", "thin", "medium", "thick", "double", "dotted",
                    "dashed", "hair", "dashDot", "dashDotDot", "mediumDashed", "mediumDashDot",
                    "mediumDashDotDot", "slantDashDot"};
                if (!styles.count(value))
                    return false;
            }
            else if (key == "bold" || key == "italic" || key == "underline" || key == "strike" ||
                key == "wrap" || key == "shrinkToFit" || key == "border" || key == "reset")
            {
                if (value != "0" && value != "1")
                    return false;
            }
            else if (key == "align")
            {
                if (value != "general" && value != "left" && value != "center" && value != "right" &&
                    value != "justify" && value != "distributed")
                    return false;
            }
            else if (key == "valign")
            {
                if (value != "top" && value != "center" && value != "bottom")
                    return false;
            }
            else if (key == "indent" || key == "decimals")
            {
                const auto n = number(value, -1);
                if (n < 0 || n > (key == "indent" ? 15 : 10) || std::floor(n) != n)
                    return false;
            }
            else if (key == "textRotation")
            {
                const auto angle = number(value, -1);
                if ((angle < 0 || angle > 180 || std::floor(angle) != angle) && angle != 255)
                    return false;
            }
            else if (key == "number")
            {
                if (value != "0" && value != "1" && value != "2" && value != "3" && value != "4" &&
                    value != "9" && value != "10" && value != "14" && value != "49" && value != "currency")
                    return false;
            }
            else
                return false;
        }
        return true;
    }

    std::vector<Node> children(Node parent)
    {
        std::vector<Node> result;
        for (auto node : parent.children())
        {
            if (node.type() == pugi::node_element)
                result.push_back(node);
        }
        return result;
    }

    Node at(const std::vector<Node>& list, unsigned index)
    {
        return index < list.size() ? list[index] : Node{};
    }

    std::string xml(const pugi::xml_document& document)
    {
        std::ostringstream stream;
        document.save(stream, "", pugi::format_raw, pugi::encoding_utf8);
        return stream.str();
    }

    mirrorfly::OfficePart* part(std::vector<mirrorfly::OfficePart>& parts, const std::string& path)
    {
        const auto found = std::find_if(parts.begin(), parts.end(), [&](const auto& item)
        {
            return item.path == path;
        });
        return found == parts.end() ? nullptr : &*found;
    }

    pugi::xml_document load(const std::vector<mirrorfly::OfficePart>& parts, const std::string& path)
    {
        const auto found = std::find_if(parts.begin(), parts.end(), [&](const auto& item)
        {
            return item.path == path;
        });
        pugi::xml_document result;
        if (found == parts.end() || found->bytes.size() > mirrorfly::maximum_spreadsheet_xml_bytes ||
            !result.load_buffer(found->bytes.data(), found->bytes.size()))
            throw std::runtime_error("样式部件无法读取。");
        return result;
    }

    Node ensure_row(Node sheet_data, unsigned index)
    {
        for (auto row : sheet_data.children())
        {
            const auto current = row.attribute("r").as_uint();
            if (current == index + 1)
                return row;
            if (current > index + 1)
            {
                auto next = sheet_data.insert_child_before(qualified(sheet_data, "row").c_str(), row);
                set(next, "r", std::to_string(index + 1));
                return next;
            }
        }
        auto next = sheet_data.append_child(qualified(sheet_data, "row").c_str());
        set(next, "r", std::to_string(index + 1));
        return next;
    }

    Node ensure_cell(Node data, mirrorfly::SpreadsheetAddress address)
    {
        auto row = ensure_row(data, address.row);
        for (auto cell : row.children())
        {
            const auto parsed = mirrorfly::parse_spreadsheet_address(cell.attribute("r").value());
            if (parsed && parsed->column == address.column)
                return cell;
            if (parsed && parsed->column > address.column)
            {
                auto next = row.insert_child_before(qualified(row, "c").c_str(), cell);
                set(next, "r", mirrorfly::spreadsheet_address(address));
                return next;
            }
        }
        auto next = row.append_child(qualified(row, "c").c_str());
        set(next, "r", mirrorfly::spreadsheet_address(address));
        return next;
    }

    void set_rgb(Node target, const std::string& value)
    {
        target.remove_attributes();
        set(target, "rgb", "FF" + value.substr(1));
    }

    unsigned append_style(Node root, unsigned base, const Format& patch)
    {
        if (patch.count("reset") && patch.at("reset") == "1")
            base = 0;
        if (!child(root, "fonts") || !child(root, "fills") || !child(root, "borders"))
            throw std::runtime_error("样式表缺少必要集合，无法安全修改。");
        for (const auto* collection : {"fonts", "fills", "borders"})
            if (children(child(root, collection)).size() >= 8192)
                throw std::runtime_error("样式资源数量超过限制。");
        auto xfs = child(root, "cellXfs");
        const auto originals = children(xfs);
        if (base >= originals.size() || originals.size() >= 8192)
            throw std::runtime_error("样式数量或索引超过支持范围。");
        auto xf = xfs.append_copy(originals[base]);
        const auto fonts = children(child(root, "fonts"));
        if (patch.count("font") || patch.count("size") || patch.count("bold") || patch.count("italic") ||
            patch.count("text") || patch.count("underline") || patch.count("strike"))
        {
            auto container = child(root, "fonts");
            auto source = at(fonts, xf.attribute("fontId").as_uint());
            Node font;
            if (source)
                font = container.append_copy(source);
            else
                font = container.append_child(qualified(container, "font").c_str());
            for (const auto& [key, value] : patch)
            {
                if (key == "font")
                {
                    set(ensure(font, "name"), "val", value);
                    font.remove_child(child(font, "scheme"));
                }
                if (key == "size")
                    set(ensure(font, "sz"), "val", value);
                if (key == "bold")
                    set(ensure(font, "b"), "val", value);
                if (key == "italic")
                    set(ensure(font, "i"), "val", value);
                if (key == "underline")
                    set(ensure(font, "u"), "val", value == "1" ? "single" : "none");
                if (key == "strike")
                    set(ensure(font, "strike"), "val", value);
                if (key == "text")
                    set_rgb(ensure(font, "color"), value);
            }
            set(xf, "fontId", std::to_string(fonts.size()));
            set(xf, "applyFont", "1");
            set(container, "count", std::to_string(fonts.size() + 1));
        }
        if (patch.count("fill"))
        {
            auto container = child(root, "fills");
            const auto index = children(container).size();
            auto fill = container.append_child(qualified(container, "fill").c_str());
            auto pattern = ensure(fill, "patternFill");
            const auto value = patch.at("fill");
            set(pattern, "patternType", value == "none" ? "none" : "solid");
            if (value != "none")
                set_rgb(ensure(pattern, "fgColor"), value);
            set(xf, "fillId", std::to_string(index));
            set(xf, "applyFill", "1");
            set(container, "count", std::to_string(index + 1));
        }
        const std::vector<std::string> sides{"Left", "Right", "Top", "Bottom"};
        bool border_changed = patch.count("border") != 0;
        for (const auto& side : sides)
            border_changed =
                border_changed || patch.count("border" + side) || patch.count("border" + side + "Color");
        if (border_changed)
        {
            auto container = child(root, "borders");
            const auto original_borders = children(container);
            const auto index = original_borders.size();
            const auto original = at(original_borders, xf.attribute("borderId").as_uint());
            auto border = container.append_copy(original);
            if (!border)
                border = container.append_child(qualified(container, "border").c_str());
            for (const auto& side : sides)
            {
                auto name = side;
                name[0] += 'a' - 'A';
                const auto key = "border" + side;
                if (!patch.count("border") && !patch.count(key) && !patch.count(key + "Color"))
                    continue;
                auto edge = ensure(border, name.c_str());
                std::string style = edge.attribute("style").as_string("none");
                if (patch.count("border"))
                    style = patch.at("border") == "1" ? "thin" : "none";
                if (patch.count(key))
                    style = patch.at(key);
                edge.remove_attribute("style");
                if (style != "none")
                {
                    set(edge, "style", style);
                    if (patch.count(key + "Color"))
                        set_rgb(ensure(edge, "color"), patch.at(key + "Color"));
                    else if (!child(edge, "color"))
                        set(ensure(edge, "color"), "auto", "1");
                }
                else
                    edge.remove_child(child(edge, "color"));
            }
            ensure(border, "diagonal");
            for (const auto* name :
                {"start", "end", "left", "right", "top", "bottom", "diagonal", "vertical", "horizontal"})
                if (auto edge = child(border, name))
                    border.append_move(edge);
            set(xf, "borderId", std::to_string(index));
            set(xf, "applyBorder", "1");
            set(container, "count", std::to_string(index + 1));
        }
        if (patch.count("align") || patch.count("wrap") || patch.count("valign") || patch.count("indent") ||
            patch.count("shrinkToFit") || patch.count("textRotation"))
        {
            auto align = ensure(xf, "alignment");
            if (patch.count("align"))
                set(align, "horizontal", patch.at("align"));
            if (patch.count("valign"))
                set(align, "vertical", patch.at("valign"));
            if (patch.count("indent"))
                set(align, "indent", patch.at("indent"));
            if (patch.count("wrap"))
                set(align, "wrapText", patch.at("wrap"));
            if (patch.count("shrinkToFit"))
                set(align, "shrinkToFit", patch.at("shrinkToFit"));
            if (patch.count("textRotation"))
                set(align, "textRotation",
                    std::to_string(static_cast<int>(number(patch.at("textRotation"), 0))));
            set(xf, "applyAlignment", "1");
        }
        if (patch.count("number") || patch.count("decimals"))
        {
            const auto kind = patch.count("number") ? patch.at("number") : "2";
            std::string id = kind;
            if (kind == "currency" || patch.count("decimals"))
            {
                const int digits = patch.count("decimals") ? std::stoi(patch.at("decimals")) : 2;
                std::string code =
                    kind == "currency" ? "\"¥\"#,##0" : (kind == "3" || kind == "4" ? "#,##0" : "0");
                if (digits)
                    code += "." + std::string(digits, '0');
                if (kind == "9" || kind == "10")
                    code += "%";
                auto formats = child(root, "numFmts");
                if (!formats)
                    formats = root.prepend_child(qualified(root, "numFmts").c_str());
                unsigned next = 164;
                for (auto node : formats.children())
                    next = std::max(next, node.attribute("numFmtId").as_uint() + 1);
                if (next >= 65535)
                    throw std::runtime_error("数字格式数量超过限制。");
                auto node = formats.append_child(qualified(formats, "numFmt").c_str());
                set(node, "numFmtId", std::to_string(next));
                set(node, "formatCode", code);
                set(formats, "count", std::to_string(children(formats).size()));
                id = std::to_string(next);
            }
            set(xf, "numFmtId", id);
            set(xf, "applyNumberFormat", "1");
        }
        set(xfs, "count", std::to_string(originals.size() + 1));
        return static_cast<unsigned>(originals.size());
    }
}

namespace mirrorfly
{
    SpreadsheetEditResult apply_spreadsheet_transaction(SpreadsheetDocument& document,
        const std::vector<SpreadsheetEditCommand>& cells,
        const std::vector<SpreadsheetFormatCommand>& formats,
        const std::vector<SpreadsheetDimensionCommand>& dimensions)
    {
        if (formats.empty() && dimensions.empty())
            return apply_spreadsheet_edits(document, cells);
        if (cells.empty() && dimensions.empty())
            return apply_spreadsheet_formats(document, formats);
        if (cells.empty() && formats.empty())
            return apply_spreadsheet_dimensions(document, dimensions);
        try
        {
            auto values_before = document.edits;
            auto formats_before = document.format_edits;
            auto columns_before = document.column_edits;
            auto rows_before = document.row_edits;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> extents;
            for (const auto& sheet : document.sheets)
                extents.emplace_back(sheet.rows, sheet.columns);
            const bool stale_before = document.caches_stale;
            const auto rollback = [&]()
            {
                document.edits.swap(values_before);
                document.format_edits.swap(formats_before);
                document.column_edits.swap(columns_before);
                document.row_edits.swap(rows_before);
                document.caches_stale = stale_before;
                for (std::size_t index = 0; index < extents.size(); ++index)
                {
                    document.sheets[index].rows = extents[index].first;
                    document.sheets[index].columns = extents[index].second;
                }
            };
            try
            {
                const auto values = apply_spreadsheet_edits(document, cells);
                if (values.error != SpreadsheetError::None)
                {
                    rollback();
                    return values;
                }
                const auto styles = apply_spreadsheet_formats(document, formats);
                if (styles.error != SpreadsheetError::None)
                {
                    rollback();
                    return styles;
                }
                const auto sizes = apply_spreadsheet_dimensions(document, dimensions);
                if (sizes.error != SpreadsheetError::None)
                {
                    rollback();
                    return sizes;
                }
                return {SpreadsheetError::None, {}, values.changed || styles.changed || sizes.changed};
            }
            catch (...)
            {
                rollback();
                throw;
            }
        }
        catch (const std::bad_alloc&)
        {
            return {SpreadsheetError::TooLarge, "编辑超过内存预算，原内容已保留。", false};
        }
    }

    void read_spreadsheet_styles(
        SpreadsheetDocument& document, pugi::xml_node root, const SpreadsheetColors& colors)
    {
        if (local(root.name()) != "styleSheet")
            throw std::runtime_error("样式表根节点无效。");
        const auto fonts = children(child(root, "fonts"));
        const auto fills = children(child(root, "fills"));
        const auto borders = children(child(root, "borders"));
        const auto formats = children(child(root, "cellXfs"));
        if (formats.size() > 8192 || fonts.size() > 8192 || fills.size() > 8192 || borders.size() > 8192)
            throw std::runtime_error("工作簿样式数量超过限制。");
        for (auto xf : formats)
        {
            Format format;
            auto font = at(fonts, xf.attribute("fontId").as_uint());
            auto fill = at(fills, xf.attribute("fillId").as_uint());
            auto border = at(borders, xf.attribute("borderId").as_uint());
            if (auto name = child(font, "name"))
                format["font"] = name.attribute("val").value();
            if (auto size = child(font, "sz"))
                format["size"] = size.attribute("val").value();
            format["bold"] = child(font, "b") && child(font, "b").attribute("val").as_bool(true) ? "1" : "0";
            format["italic"] =
                child(font, "i") && child(font, "i").attribute("val").as_bool(true) ? "1" : "0";
            format["underline"] = child(font, "u") &&
                    std::string(child(font, "u").attribute("val").as_string("single")) != "none"
                ? "1"
                : "0";
            format["strike"] =
                child(font, "strike") && child(font, "strike").attribute("val").as_bool(true) ? "1" : "0";
            const auto foreground = colors.resolve(child(font, "color"));
            const auto background = colors.resolve(child(child(fill, "patternFill"), "fgColor"));
            if (!foreground.empty())
                format["text"] = foreground;
            if (!background.empty())
                format["fill"] = background;
            format["border"] = "0";
            for (const auto& side : std::vector<std::string>{"Left", "Right", "Top", "Bottom"})
            {
                auto name = side;
                name[0] += 'a' - 'A';
                const auto edge = child(border, name.c_str());
                const auto key = "border" + side;
                format[key] = edge.attribute("style").as_string("none");
                auto ink = colors.resolve(child(edge, "color"));
                format[key + "Color"] = ink.empty() ? "#68717A" : ink;
                if (format[key] != "none")
                    format["border"] = "1";
            }
            auto align = child(xf, "alignment");
            format["align"] = align.attribute("horizontal").as_string("general");
            format["wrap"] = align.attribute("wrapText").as_bool() ? "1" : "0";
            format["shrinkToFit"] = align.attribute("shrinkToFit").as_bool() ? "1" : "0";
            const auto angle = number(align.attribute("textRotation").as_string("0"), -1);
            format["textRotation"] = std::to_string(static_cast<int>(
                ((angle >= 0 && angle <= 180 && std::floor(angle) == angle) || angle == 255) ? angle : 0));
            format["valign"] = align.attribute("vertical").as_string("bottom");
            format["indent"] = align.attribute("indent").as_string("0");
            format["number"] = xf.attribute("numFmtId").as_string("0");
            for (auto num : child(root, "numFmts").children())
            {
                if (num.attribute("numFmtId").as_uint() != xf.attribute("numFmtId").as_uint())
                    continue;
                std::string code = num.attribute("formatCode").value();
                const bool currency = code.rfind("\"¥\"", 0) == 0;
                if (currency)
                    code.erase(0, std::string("\"¥\"").size());
                const bool percent = !code.empty() && code.back() == '%';
                if (percent)
                    code.pop_back();
                const auto dot = code.find('.');
                const auto base = code.substr(0, dot);
                const auto digits = dot == std::string::npos ? 0 : code.size() - dot - 1;
                if ((base == "0" || base == "#,##0") && digits <= 10 &&
                    (dot == std::string::npos || code.substr(dot + 1) == std::string(digits, '0')))
                {
                    format["number"] = currency ? "currency" : percent ? "10" : base == "#,##0" ? "4" : "2";
                    format["decimals"] = std::to_string(digits);
                }
            }
            document.styles.push_back(std::move(format));
            std::set<std::string> properties;
            if (xf.attribute("fontId").as_uint() || xf.attribute("applyFont").as_bool())
            {
                if (child(font, "b"))
                    properties.insert("bold");
                const auto color = child(font, "color");
                const auto default_color = child(at(fonts, 0), "color");
                bool inherited_color = static_cast<bool>(color) == static_cast<bool>(default_color);
                for (const auto* key : {"rgb", "theme", "indexed", "auto", "tint"})
                    inherited_color = inherited_color &&
                        static_cast<bool>(color.attribute(key)) ==
                            static_cast<bool>(default_color.attribute(key)) &&
                        std::string(color.attribute(key).value()) == default_color.attribute(key).value();
                // A cloned default theme font with only a size change must not mask table text colors.
                if (!foreground.empty() && !inherited_color)
                    properties.insert("text");
            }
            if (xf.attribute("fillId").as_uint() || xf.attribute("applyFill").as_bool())
                properties.insert("fill");
            document.style_properties.push_back(std::move(properties));
        }
    }

    void read_spreadsheet_dimensions(SpreadsheetSheet& sheet, pugi::xml_node root)
    {
        std::size_t dimension_work = 0;
        for (auto col : child(root, "cols").children())
        {
            const auto first = col.attribute("min").as_uint();
            const auto last = col.attribute("max").as_uint();
            const auto width = col.attribute("width").as_double();
            if (first == 0 || last < first || last > maximum_spreadsheet_columns || !std::isfinite(width))
                continue;
            dimension_work += last - first + 1;
            if (dimension_work > 32768)
                throw std::runtime_error("列尺寸结构过于复杂。");
            if (width > 0 && width <= 255)
                for (auto i = first; i <= last; ++i)
                    sheet.column_widths[i - 1] = width;
        }
        for (auto row : child(root, "sheetData").children())
        {
            const auto index = row.attribute("r").as_uint();
            const auto height = row.attribute("ht").as_double();
            if (index > 0 && index <= maximum_spreadsheet_rows && std::isfinite(height) && height > 0 &&
                height <= 409)
                sheet.row_heights[index - 1] = height;
        }
    }

    SpreadsheetFormat spreadsheet_cell_format(
        const SpreadsheetDocument& document, std::size_t sheet, SpreadsheetAddress address)
    {
        Format result{{"size", "11"}, {"bold", "0"}, {"italic", "0"}, {"fill", "none"}, {"align", "general"},
            {"wrap", "0"}, {"border", "0"}, {"number", "0"}, {"underline", "0"}, {"strike", "0"},
            {"valign", "bottom"}, {"indent", "0"}, {"shrinkToFit", "0"}, {"textRotation", "0"}};
        const std::vector<std::string> sides{"Left", "Right", "Top", "Bottom"};
        for (const auto& side : sides)
        {
            result["border" + side] = "none";
            result["border" + side + "Color"] = "#68717A";
        }
        const auto cell = spreadsheet_cell_properties(document, sheet, address);
        const auto patches = document.format_edits.find(sheet);
        const bool reset = patches != document.format_edits.end() && patches->second.count(address) &&
            patches->second.at(address).count("reset") && patches->second.at(address).at("reset") == "1";
        const auto style = reset ? 0 : cell.style_index;
        if (style < document.styles.size())
            for (const auto& item : document.styles[style])
                result[item.first] = item.second;
        for (const auto& item : spreadsheet_table_format(spreadsheet_features(document, sheet), address))
            if (style >= document.style_properties.size() ||
                !document.style_properties[style].count(item.first))
                result[item.first] = item.second;
        const auto found = document.format_edits.find(sheet);
        if (found != document.format_edits.end())
        {
            const auto current = found->second.find(address);
            if (current != found->second.end())
            {
                if (current->second.count("number"))
                    result.erase("decimals");
                if (current->second.count("border"))
                    for (const auto& side : sides)
                        result["border" + side] = current->second.at("border") == "1" ? "thin" : "none";
                for (const auto& item : current->second)
                    if (item.first != "reset")
                        result[item.first] = item.second;
            }
        }
        result["border"] = "0";
        for (const auto& side : sides)
            if (result["border" + side] != "none")
                result["border"] = "1";
        return result;
    }

    SpreadsheetFormat spreadsheet_cell_direct_format(
        const SpreadsheetDocument& document, std::size_t sheet, SpreadsheetAddress address)
    {
        auto result = spreadsheet_cell_format(document, sheet, address);
        const auto changes = document.format_edits.find(sheet);
        const SpreadsheetFormat* patch = nullptr;
        if (changes != document.format_edits.end())
        {
            const auto found = changes->second.find(address);
            if (found != changes->second.end())
                patch = &found->second;
        }
        const auto cell = spreadsheet_cell_properties(document, sheet, address);
        const auto style = patch && patch->count("reset") && patch->at("reset") == "1" ? 0 : cell.style_index;
        for (const auto* key : {"bold", "text", "fill"})
            if ((!patch || !patch->count(key)) &&
                (style >= document.style_properties.size() || !document.style_properties[style].count(key)))
                result.erase(key);
        result["reset"] = "1";
        return result;
    }

    SpreadsheetEditResult apply_spreadsheet_formats(
        SpreadsheetDocument& document, const std::vector<SpreadsheetFormatCommand>& commands)
    {
        if (commands.empty())
            return {};
        if (commands.size() > maximum_spreadsheet_batch_cells)
            return {SpreadsheetError::TooLarge, "单次最多设置 4096 格样式。", false};
        try
        {
            auto proposed = document.format_edits;
            std::set<std::tuple<std::size_t, unsigned, unsigned>> seen;
            bool changed = false;
            for (const auto& command : commands)
            {
                const auto cell = spreadsheet_cell_properties(document, command.sheet_index, command.address);
                if (!cell.editable && !cell.merged_covered)
                    return {SpreadsheetError::ReadOnly, cell.read_only_reason, false};
                if (!valid_format(command.format) ||
                    !seen.emplace(command.sheet_index, command.address.row, command.address.column).second)
                    return {SpreadsheetError::InvalidValue, "样式参数或批量地址无效。", false};
                auto& patch = proposed[command.sheet_index][command.address];
                changed = changed || patch != command.format;
                patch = command.format;
                if (patch.empty())
                    proposed[command.sheet_index].erase(command.address);
                if (proposed[command.sheet_index].empty())
                    proposed.erase(command.sheet_index);
            }
            std::size_t bytes = 0;
            std::size_t cells = 0;
            for (const auto& sheet : document.sheets)
                cells += sheet.cells.size();
            for (const auto& [sheet, edits] : document.edits)
            {
                if (sheet >= document.sheets.size())
                    return {SpreadsheetError::InvalidValue, "工作表索引无效。", false};
                for (const auto& [address, value] : edits)
                    if (document.sheets[sheet].cells.count(address) == 0)
                        ++cells;
            }
            for (const auto& [sheet, formats] : proposed)
            {
                if (sheet >= document.sheets.size())
                    return {SpreadsheetError::InvalidValue, "工作表索引无效。", false};
                const auto values = document.edits.find(sheet);
                for (const auto& [address, format] : formats)
                {
                    if (!valid_format(format) || !format_editable(document, sheet, address))
                        return {SpreadsheetError::InvalidValue, "样式数据无效。", false};
                    if (document.sheets[sheet].cells.count(address) == 0 &&
                        (values == document.edits.end() || !values->second.count(address)))
                        ++cells;
                    for (const auto& [key, value] : format)
                        bytes += key.size() + value.size() + 64;
                }
            }
            if (bytes > 4 * 1024 * 1024 || cells > maximum_spreadsheet_cells)
                return {SpreadsheetError::TooLarge, "样式数量超过预算，请缩小区域。", false};
            if (changed)
                document.format_edits = std::move(proposed);
            return {SpreadsheetError::None, {}, changed};
        }
        catch (const std::bad_alloc&)
        {
            return {SpreadsheetError::TooLarge, "样式编辑超过内存预算。", false};
        }
    }

    double spreadsheet_dimension(
        const SpreadsheetDocument& document, std::size_t sheet, bool column, std::uint32_t index)
    {
        if (sheet >= document.sheets.size())
            return column ? 18 : 25.5;
        const auto& edits = column ? document.column_edits : document.row_edits;
        const auto changed = edits.find(sheet);
        if (changed != edits.end())
        {
            const auto value = changed->second.find(index);
            if (value != changed->second.end())
                return value->second;
        }
        const auto& originals =
            column ? document.sheets[sheet].column_widths : document.sheets[sheet].row_heights;
        const auto original = originals.find(index);
        return original != originals.end() ? original->second : (column ? 18 : 25.5);
    }

    SpreadsheetEditResult apply_spreadsheet_dimensions(
        SpreadsheetDocument& document, const std::vector<SpreadsheetDimensionCommand>& commands)
    {
        if (commands.empty())
            return {};
        if (commands.size() > maximum_spreadsheet_batch_cells)
            return {SpreadsheetError::TooLarge, "单次最多调整 4096 行或列。", false};
        try
        {
            auto columns = document.column_edits;
            auto rows = document.row_edits;
            std::set<std::tuple<std::size_t, bool, unsigned>> seen;
            bool changed = false;
            for (const auto& command : commands)
            {
                if (command.sheet_index >= document.sheets.size() || document.read_only ||
                    !document.sheets[command.sheet_index].editable)
                    return {SpreadsheetError::ReadOnly, "工作表不可修改尺寸。", false};
                if (!std::isfinite(command.size) ||
                    (command.size != 0 &&
                        (command.size < (command.column ? 3 : 12) ||
                            command.size > (command.column ? 80 : 300))) ||
                    command.index >=
                        (command.column ? maximum_spreadsheet_columns : maximum_spreadsheet_rows) ||
                    !seen.emplace(command.sheet_index, command.column, command.index).second)
                    return {SpreadsheetError::InvalidValue, "行高、列宽或索引无效。", false};
                auto& edits = command.column ? columns : rows;
                const double before = edits[command.sheet_index].count(command.index)
                    ? edits[command.sheet_index][command.index]
                    : 0;
                changed = changed || before != command.size;
                if (command.size == 0)
                    edits[command.sheet_index].erase(command.index);
                else
                    edits[command.sheet_index][command.index] = command.size;
                if (edits[command.sheet_index].empty())
                    edits.erase(command.sheet_index);
            }
            std::size_t count = 0;
            for (const auto& item : columns)
                count += item.second.size();
            for (const auto& item : rows)
                count += item.second.size();
            if (count > 20000)
                return {SpreadsheetError::TooLarge, "尺寸设置超过 20000 项。", false};
            if (changed)
            {
                document.column_edits = std::move(columns);
                document.row_edits = std::move(rows);
            }
            return {SpreadsheetError::None, {}, changed};
        }
        catch (const std::bad_alloc&)
        {
            return {SpreadsheetError::TooLarge, "尺寸编辑超过内存预算。", false};
        }
    }

    SpreadsheetPackageResult write_spreadsheet_format_parts(
        const SpreadsheetDocument& document, std::vector<OfficePart> parts)
    {
        try
        {
            std::size_t style_bytes = 0;
            std::size_t dimensions = 0;
            for (const auto& [sheet, formats] : document.format_edits)
            {
                for (const auto& [address, format] : formats)
                {
                    if (!format_editable(document, sheet, address) || !valid_format(format))
                        throw std::runtime_error("样式数据包含无效参数或只读地址。");
                    for (const auto& [key, value] : format)
                        style_bytes += key.size() + value.size() + 64;
                }
            }
            for (bool column : {true, false})
                for (const auto& [sheet, edits] : (column ? document.column_edits : document.row_edits))
                {
                    if (document.read_only || sheet >= document.sheets.size() ||
                        !document.sheets[sheet].editable)
                        throw std::runtime_error("尺寸数据包含只读工作表。");
                    dimensions += edits.size();
                    for (const auto& [index, size] : edits)
                        if (index >= (column ? maximum_spreadsheet_columns : maximum_spreadsheet_rows) ||
                            !std::isfinite(size) || size < (column ? 3 : 12) || size > (column ? 80 : 300))
                            throw std::runtime_error("尺寸数据无效。");
                }
            if (style_bytes > 4 * 1024 * 1024 || dimensions > 20000)
                throw std::runtime_error("样式或尺寸数量超过限制。");
            pugi::xml_document styles;
            std::string style_path = document.styles_path;
            if (!document.format_edits.empty())
            {
                if (style_path.empty())
                {
                    const auto directory =
                        document.workbook_path.substr(0, document.workbook_path.rfind('/') + 1);
                    style_path = directory + "mirrorfly-styles.xml";
                    if (part(parts, style_path))
                        throw std::runtime_error("新样式表路径已被其他部件使用。");
                    const auto blank = make_spreadsheet();
                    styles = load(*blank.original_parts, blank.styles_path);
                    parts.push_back({style_path, xml(styles)});
                    const auto name = document.workbook_path.substr(directory.size());
                    const auto relpath = directory + "_rels/" + name + ".rels";
                    auto rels = load(parts, relpath);
                    std::set<std::string> ids;
                    for (auto item : rels.document_element().children())
                        ids.insert(item.attribute("Id").value());
                    std::string id = "MirrorflyStyles";
                    while (ids.count(id))
                        id += "_";
                    auto rel = rels.document_element().append_child(
                        qualified(rels.document_element(), "Relationship").c_str());
                    set(rel, "Id", id);
                    set(rel, "Type",
                        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles");
                    set(rel, "Target", "mirrorfly-styles.xml");
                    part(parts, relpath)->bytes = xml(rels);
                    auto types = load(parts, "[Content_Types].xml");
                    auto type = types.document_element().append_child(
                        qualified(types.document_element(), "Override").c_str());
                    set(type, "PartName", "/" + style_path);
                    set(type, "ContentType",
                        "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml");
                    part(parts, "[Content_Types].xml")->bytes = xml(types);
                }
                else
                    styles = load(parts, style_path);
            }
            std::map<std::string, unsigned> cache;
            for (std::size_t index = 0; index < document.sheets.size(); ++index)
            {
                const auto formats = document.format_edits.find(index);
                const auto columns = document.column_edits.find(index);
                const auto rows = document.row_edits.find(index);
                if (formats == document.format_edits.end() && columns == document.column_edits.end() &&
                    rows == document.row_edits.end())
                    continue;
                auto sheet = load(parts, document.sheets[index].path);
                auto root = sheet.document_element();
                auto data = child(root, "sheetData");
                if (!data)
                    throw std::runtime_error("工作表缺少数据节点。");
                if (formats != document.format_edits.end())
                {
                    for (const auto& [address, patch] : formats->second)
                    {
                        const auto original = document.sheets[index].cells.find(address);
                        const unsigned base =
                            original == document.sheets[index].cells.end() ? 0 : original->second.style_index;
                        std::string key = std::to_string(base) + ":";
                        for (const auto& [name, value] : patch)
                            key += name + ":" + std::to_string(value.size()) + ":" + value;
                        const auto found = cache.find(key);
                        const auto style = found != cache.end()
                            ? found->second
                            : append_style(styles.document_element(), base, patch);
                        cache[key] = style;
                        set(ensure_cell(data, address), "s", std::to_string(style));
                    }
                }
                if (rows != document.row_edits.end())
                    for (const auto& [row, size] : rows->second)
                    {
                        auto node = ensure_row(data, row);
                        set(node, "ht", std::to_string(size));
                        set(node, "customHeight", "1");
                    }
                if (columns != document.column_edits.end())
                {
                    auto container = child(root, "cols");
                    if (!container)
                        container = root.insert_child_before(qualified(root, "cols").c_str(), data);
                    for (const auto& [column, size] : columns->second)
                    {
                        Node original;
                        for (auto col : children(container))
                        {
                            const auto first = col.attribute("min").as_uint();
                            const auto last = col.attribute("max").as_uint();
                            if (column + 1 < first || column + 1 > last)
                                continue;
                            original = container.insert_copy_before(col, col);
                            if (first < column + 1)
                            {
                                auto left = container.insert_copy_before(col, col);
                                set(left, "max", std::to_string(column));
                            }
                            if (last > column + 1)
                            {
                                auto right = container.insert_copy_before(col, col);
                                set(right, "min", std::to_string(column + 2));
                            }
                            container.remove_child(col);
                            break;
                        }
                        if (!original)
                            original = container.append_child(qualified(container, "col").c_str());
                        set(original, "min", std::to_string(column + 1));
                        set(original, "max", std::to_string(column + 1));
                        set(original, "width", std::to_string(size));
                        set(original, "customWidth", "1");
                    }
                    const auto old = children(container);
                    auto sorted = old;
                    std::stable_sort(sorted.begin(), sorted.end(), [](Node a, Node b)
                    {
                        return a.attribute("min").as_uint() < b.attribute("min").as_uint();
                    });
                    for (auto node : sorted)
                        container.append_copy(node);
                    for (auto node : old)
                        container.remove_child(node);
                }
                part(parts, document.sheets[index].path)->bytes = xml(sheet);
            }
            if (!document.format_edits.empty())
                part(parts, style_path)->bytes = xml(styles);
            return {SpreadsheetError::None, {}, std::move(parts)};
        }
        catch (const std::exception& error)
        {
            return {SpreadsheetError::InvalidValue, error.what(), {}};
        }
    }
}
