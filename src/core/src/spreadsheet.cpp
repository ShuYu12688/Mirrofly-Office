#include <mirrorfly/spreadsheet.hpp>

#include "spreadsheet_colors.hpp"
#include "spreadsheet_features.hpp"
#include "spreadsheet_format.hpp"
#include "spreadsheet_structure.hpp"
#include "spreadsheet_tables.hpp"

#include <pugixml.hpp>
#include <utf8.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace
{
    using Node = pugi::xml_node;

    struct Failure
    {
        mirrorfly::SpreadsheetError error;
        std::string message;
    };

    struct Relationship
    {
        std::string type;
        std::string target;
        bool external = false;
    };

    struct Package
    {
        std::map<std::string, const mirrorfly::OfficePart*> parts;
        std::size_t xml_nodes = 0;
    };

    std::string ascii_lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
        {
            return static_cast<char>(character >= 'A' && character <= 'Z' ? character + 32 : character);
        });
        return text;
    }

    bool valid_part_path(const std::string& path)
    {
        if (path.empty() || path.size() > mirrorfly::maximum_spreadsheet_path_bytes || path.front() == '/' ||
            path.find('\\') != std::string::npos || path.find(':') != std::string::npos ||
            path.find('?') != std::string::npos || path.find('#') != std::string::npos)
        {
            return false;
        }
        std::size_t start = 0;
        while (start < path.size())
        {
            const auto end = path.find('/', start);
            const auto component = path.substr(start, end - start);
            if (component.empty() || component == "." || component == "..")
            {
                return false;
            }
            for (const unsigned char character : component)
            {
                if (character < 32 || character == 127)
                {
                    return false;
                }
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        return true;
    }

    bool has_suffix(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool xml_part(const std::string& path)
    {
        const auto lower = ascii_lower(path);
        return has_suffix(lower, ".xml") || has_suffix(lower, ".rels");
    }

    std::string local_name(const char* name)
    {
        const std::string value = name ? name : "";
        const auto colon = value.find(':');
        return colon == std::string::npos ? value : value.substr(colon + 1);
    }

    Node child(Node parent, const std::string& name)
    {
        for (auto item : parent.children())
        {
            if (local_name(item.name()) == name)
            {
                return item;
            }
        }
        return {};
    }

    std::string attribute(Node node, const std::string& name)
    {
        for (auto item : node.attributes())
        {
            if (local_name(item.name()) == name)
            {
                return item.value();
            }
        }
        return {};
    }

    std::string qualified_name(Node reference, const std::string& name)
    {
        const std::string current = reference.name();
        const auto colon = current.find(':');
        return colon == std::string::npos ? name : current.substr(0, colon + 1) + name;
    }

    std::string relationships_path(const std::string& source)
    {
        const auto slash = source.rfind('/');
        if (slash == std::string::npos)
        {
            return "_rels/" + source + ".rels";
        }
        return source.substr(0, slash + 1) + "_rels/" + source.substr(slash + 1) + ".rels";
    }

    std::string resolve_target(const std::string& source, const std::string& target)
    {
        if (target.empty() || target.find('\\') != std::string::npos ||
            target.find(':') != std::string::npos || target.find('?') != std::string::npos ||
            target.find('#') != std::string::npos)
        {
            throw Failure{mirrorfly::SpreadsheetError::InvalidPackage, "工作簿包含非法关联路径。"};
        }
        std::string joined;
        if (target.front() == '/')
        {
            joined = target.substr(1);
        }
        else
        {
            const auto slash = source.rfind('/');
            joined = (slash == std::string::npos ? std::string{} : source.substr(0, slash + 1)) + target;
        }
        std::vector<std::string> components;
        std::size_t start = 0;
        while (start <= joined.size())
        {
            const auto end = joined.find('/', start);
            const auto component = joined.substr(start, end - start);
            if (component == "..")
            {
                if (components.empty())
                {
                    throw Failure{mirrorfly::SpreadsheetError::InvalidPackage, "工作簿关联越过包边界。"};
                }
                components.pop_back();
            }
            else if (!component.empty() && component != ".")
            {
                components.push_back(component);
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        std::string result;
        for (const auto& component : components)
        {
            if (!result.empty())
            {
                result += '/';
            }
            result += component;
        }
        if (!valid_part_path(result))
        {
            throw Failure{mirrorfly::SpreadsheetError::InvalidPackage, "工作簿包含非法关联路径。"};
        }
        return result;
    }

    const mirrorfly::OfficePart& part(const Package& package, const std::string& path)
    {
        const auto found = package.parts.find(ascii_lower(path));
        if (found == package.parts.end())
        {
            throw Failure{mirrorfly::SpreadsheetError::MissingPart, "工作簿缺少必需内容。"};
        }
        return *found->second;
    }

    void count_xml(Node node, std::size_t depth, Package& package)
    {
        if (depth > mirrorfly::maximum_spreadsheet_xml_depth)
        {
            throw Failure{mirrorfly::SpreadsheetError::TooLarge, "工作簿 XML 嵌套过深。"};
        }
        for (auto current : node.children())
        {
            if (current.type() == pugi::node_element)
            {
                if (++package.xml_nodes > mirrorfly::maximum_spreadsheet_xml_nodes)
                {
                    throw Failure{mirrorfly::SpreadsheetError::TooLarge, "工作簿 XML 结构过于复杂。"};
                }
                count_xml(current, depth + 1, package);
            }
        }
    }

    pugi::xml_document load_xml(Package& package, const std::string& path)
    {
        const auto& source = part(package, path);
        if (source.bytes.size() > mirrorfly::maximum_spreadsheet_xml_bytes)
        {
            throw Failure{mirrorfly::SpreadsheetError::TooLarge, "工作簿 XML 内容超过限制。"};
        }
        const auto lower = ascii_lower(source.bytes);
        if (lower.find("<!doctype") != std::string::npos || lower.find("<!entity") != std::string::npos)
        {
            throw Failure{mirrorfly::SpreadsheetError::InvalidXml, "工作簿 XML 不允许 DTD 或实体声明。"};
        }
        pugi::xml_document document;
        const auto parsed = document.load_buffer(
            source.bytes.data(), source.bytes.size(), pugi::parse_default, pugi::encoding_utf8);
        if (!parsed || !document.document_element())
        {
            throw Failure{mirrorfly::SpreadsheetError::InvalidXml, "工作簿 XML 已损坏。"};
        }
        ++package.xml_nodes;
        count_xml(document.document_element(), 1, package);
        return document;
    }

    std::map<std::string, Relationship> relationships(Package& package, const std::string& source)
    {
        const auto path = source.empty() ? "_rels/.rels" : relationships_path(source);
        auto document = load_xml(package, path);
        std::map<std::string, Relationship> result;
        for (auto node : document.document_element().children())
        {
            if (local_name(node.name()) != "Relationship")
            {
                continue;
            }
            const auto id = attribute(node, "Id");
            const auto type = attribute(node, "Type");
            const auto target = attribute(node, "Target");
            const bool external = ascii_lower(attribute(node, "TargetMode")) == "external";
            if (id.empty() || type.empty() || target.empty() || result.count(id))
            {
                throw Failure{mirrorfly::SpreadsheetError::InvalidPackage, "工作簿关联无效。"};
            }
            result.emplace(
                id, Relationship{type, external ? std::string{} : resolve_target(source, target), external});
        }
        return result;
    }

    bool relationship_type(const std::string& type, const std::string& suffix)
    {
        return has_suffix(type, "/" + suffix);
    }

    bool valid_xml_text(const std::string& text)
    {
        if (!utf8::is_valid(text.begin(), text.end()))
        {
            return false;
        }
        for (const unsigned char character : text)
        {
            if (character < 32 && character != '\t' && character != '\n' && character != '\r')
            {
                return false;
            }
        }
        return true;
    }

    std::size_t add_size(std::size_t left, std::size_t right)
    {
        return right > std::numeric_limits<std::size_t>::max() - left
            ? std::numeric_limits<std::size_t>::max()
            : left + right;
    }

    void append_text_nodes(Node node, std::string& text)
    {
        if (local_name(node.name()) == "t")
        {
            text += node.child_value();
        }
        for (auto current : node.children())
        {
            if (current.type() == pugi::node_element)
            {
                append_text_nodes(current, text);
            }
        }
    }

    std::optional<mirrorfly::SpreadsheetRange> parse_range(const std::string& text)
    {
        const auto colon = text.find(':');
        const auto first = mirrorfly::parse_spreadsheet_address(text.substr(0, colon));
        const auto last =
            colon == std::string::npos ? first : mirrorfly::parse_spreadsheet_address(text.substr(colon + 1));
        if (!first || !last)
        {
            return std::nullopt;
        }
        mirrorfly::SpreadsheetRange result;
        result.first.row = std::min(first->row, last->row);
        result.first.column = std::min(first->column, last->column);
        result.last.row = std::max(first->row, last->row);
        result.last.column = std::max(first->column, last->column);
        return result;
    }

    bool contains(const mirrorfly::SpreadsheetRange& range, mirrorfly::SpreadsheetAddress address)
    {
        return address.row >= range.first.row && address.row <= range.last.row &&
            address.column >= range.first.column && address.column <= range.last.column;
    }

    bool active_flag(Node node, const char* name)
    {
        const auto value = ascii_lower(attribute(node, name));
        if (value.empty() || value == "0" || value == "false" || value == "off")
        {
            return false;
        }
        if (value == "1" || value == "true" || value == "on")
        {
            return true;
        }
        throw Failure{mirrorfly::SpreadsheetError::InvalidPackage, "工作簿包含无效保护标志。"};
    }

    std::string cell_text(Node cell, const std::string& type, const std::vector<std::string>& shared)
    {
        if (type == "inlineStr")
        {
            std::string text;
            append_text_nodes(child(cell, "is"), text);
            return text;
        }
        if (type == "s")
        {
            const std::string index_text = child(cell, "v").child_value();
            if (index_text.empty())
            {
                throw Failure{mirrorfly::SpreadsheetError::InvalidPackage, "共享字符串索引无效。"};
            }
            char* end = nullptr;
            const auto index = std::strtoull(index_text.c_str(), &end, 10);
            if (!end || *end != '\0' || index >= shared.size())
            {
                throw Failure{mirrorfly::SpreadsheetError::InvalidPackage, "共享字符串索引无效。"};
            }
            return shared[static_cast<std::size_t>(index)];
        }
        return child(cell, "v").child_value();
    }

    mirrorfly::SpreadsheetValue read_value(
        Node cell, const std::string& type, const std::vector<std::string>& shared, bool formula)
    {
        mirrorfly::SpreadsheetValue result;
        if (formula && !child(cell, "v"))
        {
            return result;
        }
        result.text = cell_text(cell, type, shared);
        if (!child(cell, "v") && type != "inlineStr" && type != "s")
        {
            return result;
        }
        if (type == "s" || type == "inlineStr" || type == "str" || type == "d")
        {
            result.kind = mirrorfly::SpreadsheetValueKind::Text;
        }
        else if (type == "b")
        {
            result.kind = mirrorfly::SpreadsheetValueKind::Boolean;
        }
        else if (type == "e")
        {
            result.kind = mirrorfly::SpreadsheetValueKind::Error;
        }
        else
        {
            result.kind = mirrorfly::SpreadsheetValueKind::Number;
        }
        return result;
    }

    std::vector<std::string> read_shared_strings(
        Package& package, const std::map<std::string, Relationship>& workbook_relationships)
    {
        std::optional<std::string> path;
        for (const auto& item : workbook_relationships)
        {
            if (!item.second.external && relationship_type(item.second.type, "sharedStrings"))
            {
                if (path)
                {
                    throw Failure{
                        mirrorfly::SpreadsheetError::InvalidPackage, "工作簿包含多个共享字符串部件。"};
                }
                path = item.second.target;
            }
        }
        if (!path)
        {
            return {};
        }
        auto document = load_xml(package, *path);
        std::vector<std::string> result;
        for (auto node : document.document_element().children())
        {
            if (local_name(node.name()) == "si")
            {
                std::string text;
                append_text_nodes(node, text);
                if (!valid_xml_text(text))
                {
                    throw Failure{mirrorfly::SpreadsheetError::InvalidXml, "共享字符串包含无效文本。"};
                }
                result.push_back(std::move(text));
            }
        }
        return result;
    }

    void mark_read_only(mirrorfly::SpreadsheetDocument& document, const std::string& reason)
    {
        document.read_only = true;
        if (document.read_only_reason.empty())
        {
            document.read_only_reason = reason;
        }
    }

    mirrorfly::SpreadsheetSheet read_sheet(Package& package, const std::string& path, const std::string& name,
        const std::vector<std::string>& shared, std::size_t& cell_count, std::size_t& text_bytes)
    {
        using namespace mirrorfly;
        auto document = load_xml(package, path);
        const auto root = document.document_element();
        if (local_name(root.name()) != "worksheet")
        {
            throw Failure{SpreadsheetError::InvalidPackage, "工作表内容类型无效。"};
        }
        SpreadsheetSheet sheet;
        sheet.name = name;
        sheet.path = path;
        read_spreadsheet_dimensions(sheet, root);
        const auto protection = child(root, "sheetProtection");
        if (protection && active_flag(protection, "sheet"))
        {
            sheet.editable = false;
            sheet.read_only_reason = "工作表受保护，当前仅支持只读查看。";
        }
        for (auto merge : child(root, "mergeCells").children())
        {
            if (local_name(merge.name()) == "mergeCell")
            {
                const auto range = parse_range(attribute(merge, "ref"));
                if (!range)
                {
                    throw Failure{SpreadsheetError::InvalidPackage, "合并单元格范围无效。"};
                }
                // Merge geometry is handled by the public worksheet feature model.
            }
        }
        std::map<std::string, SpreadsheetRange> shared_formula_ranges;
        std::set<std::string> shared_formula_indices;
        for (auto row : child(root, "sheetData").children())
        {
            if (local_name(row.name()) != "row")
            {
                continue;
            }
            for (auto node : row.children())
            {
                if (local_name(node.name()) != "c")
                {
                    continue;
                }
                if (++cell_count > maximum_spreadsheet_cells)
                {
                    throw Failure{SpreadsheetError::TooLarge, "工作簿实际单元格数量超过限制。"};
                }
                const auto address = parse_spreadsheet_address(attribute(node, "r"));
                if (!address || sheet.cells.count(*address))
                {
                    throw Failure{SpreadsheetError::InvalidPackage, "工作表包含重复或无效单元格。"};
                }
                SpreadsheetCell cell;
                if (!attribute(node, "cm").empty() || !attribute(node, "vm").empty())
                {
                    sheet.editable = false;
                    sheet.read_only_reason = "工作表包含未支持的动态数组或值元数据，当前仅支持只读查看。";
                }
                const auto style = attribute(node, "s");
                if (!style.empty())
                {
                    char* end = nullptr;
                    const auto value = std::strtoull(style.c_str(), &end, 10);
                    if (!end || *end != '\0' || value > std::numeric_limits<std::uint32_t>::max())
                    {
                        throw Failure{SpreadsheetError::InvalidPackage, "单元格样式索引无效。"};
                    }
                    cell.style_index = static_cast<std::uint32_t>(value);
                }
                const auto formula = child(node, "f");
                cell.formula_cell = static_cast<bool>(formula);
                if (formula)
                {
                    cell.formula = formula.child_value();
                    cell.editable = false;
                    cell.read_only_reason = "公式单元格由工作簿计算，当前不可直接编辑。";
                    const auto formula_type = attribute(formula, "t");
                    const auto formula_reference = attribute(formula, "ref");
                    cell.formula_supported = (formula_type.empty() || formula_type == "normal") &&
                        formula_reference.empty() && spreadsheet_formula_supported(cell.formula);
                    if (cell.formula_supported)
                    {
                        cell.editable = true;
                        cell.read_only_reason.clear();
                    }
                    const auto formula_range = parse_range(formula_reference);
                    if (!formula_reference.empty() && !formula_range)
                    {
                        throw Failure{SpreadsheetError::InvalidPackage, "公式影响范围无效。"};
                    }
                    if (formula_range)
                    {
                        sheet.protected_ranges.push_back(*formula_range);
                    }
                    if (formula_type == "shared")
                    {
                        const auto shared_index = attribute(formula, "si");
                        if (shared_index.empty())
                        {
                            throw Failure{SpreadsheetError::InvalidPackage, "共享公式标识无效。"};
                        }
                        shared_formula_indices.insert(shared_index);
                        if (formula_range)
                        {
                            if (!shared_formula_ranges.emplace(shared_index, *formula_range).second)
                            {
                                sheet.editable = false;
                                sheet.read_only_reason = "工作表包含多个共享公式主范围，当前仅支持只读查看。";
                            }
                        }
                    }
                    else if (formula_type == "dataTable")
                    {
                        sheet.editable = false;
                        sheet.read_only_reason = "工作表包含数据表公式，当前仅支持只读查看。";
                    }
                    else if (formula_type == "array" && !formula_range)
                    {
                        sheet.editable = false;
                        sheet.read_only_reason = "工作表包含未声明影响范围的数组公式，当前仅支持只读查看。";
                    }
                    else if (!formula_type.empty() && formula_type != "normal" && formula_type != "array")
                    {
                        sheet.editable = false;
                        sheet.read_only_reason = "工作表包含未支持的公式结构，当前仅支持只读查看。";
                    }
                }
                const auto type = attribute(node, "t");
                cell.value = read_value(node, type, shared, cell.formula_cell);
                if (!type.empty() && type != "n" && type != "s" && type != "inlineStr" && type != "str" &&
                    type != "b" && type != "e")
                {
                    cell.editable = false;
                    cell.read_only_reason = "单元格使用未支持的值类型，当前不可编辑。";
                }
                if (!valid_xml_text(cell.value.text) || !valid_xml_text(cell.formula))
                {
                    throw Failure{SpreadsheetError::InvalidXml, "单元格包含无效文本。"};
                }
                text_bytes = add_size(text_bytes, add_size(cell.value.text.size(), cell.formula.size()));
                if (text_bytes > maximum_spreadsheet_text_bytes)
                {
                    throw Failure{SpreadsheetError::TooLarge, "工作簿文本内容超过限制。"};
                }
                sheet.rows = std::max(sheet.rows, address->row + 1);
                sheet.columns = std::max(sheet.columns, address->column + 1);
                sheet.cells.emplace(*address, std::move(cell));
            }
        }
        for (const auto& item : shared_formula_ranges)
        {
            sheet.protected_ranges.push_back(item.second);
        }
        for (const auto& index : shared_formula_indices)
        {
            if (!shared_formula_ranges.count(index))
            {
                sheet.editable = false;
                sheet.read_only_reason = "工作表包含未声明主范围的共享公式，当前仅支持只读查看。";
                break;
            }
        }
        return sheet;
    }

    Package make_package(const std::vector<mirrorfly::OfficePart>& parts)
    {
        using namespace mirrorfly;
        if (parts.empty())
        {
            throw Failure{SpreadsheetError::InvalidPackage, "工作簿没有包内容。"};
        }
        if (parts.size() > maximum_spreadsheet_parts)
        {
            throw Failure{SpreadsheetError::TooLarge, "工作簿条目数量超过限制。"};
        }
        Package package;
        std::size_t expanded = 0;
        for (const auto& item : parts)
        {
            expanded = add_size(expanded, item.bytes.size());
            const auto limit =
                xml_part(item.path) ? maximum_spreadsheet_xml_bytes : maximum_spreadsheet_part_bytes;
            if (item.bytes.size() > limit || expanded > maximum_spreadsheet_expanded_bytes)
            {
                throw Failure{SpreadsheetError::TooLarge, "工作簿解压内容超过限制。"};
            }
            if (!valid_part_path(item.path) || !package.parts.emplace(ascii_lower(item.path), &item).second)
            {
                throw Failure{SpreadsheetError::InvalidPackage, "工作簿包含重复、非法或超限条目。"};
            }
        }
        return package;
    }

    std::vector<mirrorfly::OfficePart> blank_package()
    {
        return {{"[Content_Types].xml",
                    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package."
                    "relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                    "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd."
                    "openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                    "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd."
                    "openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
                    "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd."
                    "openxmlformats-officedocument.spreadsheetml.styles+xml\"/></Types>"},
            {"_rels/.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
                "2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>"},
            {"xl/workbook.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                "<sheets><sheet name=\"工作表1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
                "<calcPr calcMode=\"auto\"/></workbook>"},
            {"xl/_rels/workbook.xml.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
                "2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
                "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
                "2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>"},
            {"xl/worksheets/sheet1.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
                "<sheetData/></worksheet>"},
            {"xl/styles.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
                "<fonts count=\"1\"><font><sz val=\"11\"/><color theme=\"1\"/><name val=\"Calibri\"/>"
                "<family val=\"2\"/><scheme val=\"minor\"/></font></fonts>"
                "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill "
                "patternType=\"gray125\"/></fill></fills><borders count=\"1\"><border><left/><right/>"
                "<top/><bottom/><diagonal/></border></borders><cellStyleXfs count=\"1\"><xf numFmtId=\"0\" "
                "fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs><cellXfs count=\"1\"><xf "
                "numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/></cellXfs>"
                "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/>"
                "</cellStyles></styleSheet>"}};
    }

    bool valid_number(const std::string& text)
    {
        if (text.empty())
        {
            return false;
        }
        std::size_t index = (text.front() == '+' || text.front() == '-') ? 1 : 0;
        bool digit = false;
        while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])))
        {
            digit = true;
            ++index;
        }
        if (index < text.size() && text[index] == '.')
        {
            ++index;
            while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])))
            {
                digit = true;
                ++index;
            }
        }
        if (!digit)
        {
            return false;
        }
        if (index < text.size() && (text[index] == 'e' || text[index] == 'E'))
        {
            ++index;
            if (index < text.size() && (text[index] == '+' || text[index] == '-'))
            {
                ++index;
            }
            const auto exponent = index;
            while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])))
            {
                ++index;
            }
            if (index == exponent)
            {
                return false;
            }
        }
        return index == text.size();
    }

    bool valid_error(const std::string& text)
    {
        static const std::set<std::string> errors{
            "#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A", "#GETTING_DATA"};
        return errors.count(text) != 0;
    }

    bool valid_value(const mirrorfly::SpreadsheetValue& value)
    {
        using mirrorfly::SpreadsheetValueKind;
        return valid_xml_text(value.text) &&
            (value.kind != SpreadsheetValueKind::Empty || value.text.empty()) &&
            (value.kind != SpreadsheetValueKind::Number || valid_number(value.text)) &&
            (value.kind != SpreadsheetValueKind::Boolean || value.text == "0" || value.text == "1" ||
                value.text == "true" || value.text == "false") &&
            (value.kind != SpreadsheetValueKind::Error || valid_error(value.text)) &&
            (value.kind != SpreadsheetValueKind::Formula ||
                mirrorfly::spreadsheet_formula_supported(value.text)) &&
            value.kind >= SpreadsheetValueKind::Empty && value.kind <= SpreadsheetValueKind::Formula;
    }

    bool same_value(const mirrorfly::SpreadsheetValue& left, const mirrorfly::SpreadsheetValue& right)
    {
        return left.kind == right.kind && left.text == right.text;
    }

    mirrorfly::SpreadsheetPackageResult package_failure(
        mirrorfly::SpreadsheetError error, const std::string& message)
    {
        mirrorfly::SpreadsheetPackageResult result;
        result.error = error;
        result.message = message;
        return result;
    }

    Node find_cell(Node sheet_data, mirrorfly::SpreadsheetAddress address)
    {
        const auto wanted = mirrorfly::spreadsheet_address(address);
        for (auto row : sheet_data.children())
        {
            for (auto cell : row.children())
            {
                if (local_name(cell.name()) == "c" &&
                    ascii_lower(attribute(cell, "r")) == ascii_lower(wanted))
                {
                    return cell;
                }
            }
        }
        return {};
    }

    Node ensure_row(Node sheet_data, std::uint32_t row_index)
    {
        const auto wanted = static_cast<unsigned long long>(row_index) + 1;
        for (auto row : sheet_data.children())
        {
            if (local_name(row.name()) != "row")
            {
                continue;
            }
            const auto current = std::strtoull(attribute(row, "r").c_str(), nullptr, 10);
            if (current == wanted)
            {
                return row;
            }
            if (current > wanted)
            {
                auto inserted = sheet_data.insert_child_before(qualified_name(row, "row").c_str(), row);
                inserted.append_attribute("r").set_value(wanted);
                return inserted;
            }
        }
        auto inserted = sheet_data.append_child(qualified_name(sheet_data, "row").c_str());
        inserted.append_attribute("r").set_value(wanted);
        return inserted;
    }

    Node ensure_cell(Node sheet_data, mirrorfly::SpreadsheetAddress address)
    {
        if (const auto existing = find_cell(sheet_data, address))
        {
            return existing;
        }
        auto row = ensure_row(sheet_data, address.row);
        const auto wanted = mirrorfly::spreadsheet_address(address);
        for (auto cell : row.children())
        {
            const auto current = mirrorfly::parse_spreadsheet_address(attribute(cell, "r"));
            if (current && current->column > address.column)
            {
                auto inserted = row.insert_child_before(qualified_name(cell, "c").c_str(), cell);
                inserted.append_attribute("r").set_value(wanted.c_str());
                return inserted;
            }
        }
        auto inserted = row.append_child(qualified_name(row, "c").c_str());
        inserted.append_attribute("r").set_value(wanted.c_str());
        return inserted;
    }

    void remove_value_nodes(Node cell)
    {
        for (auto node = cell.first_child(); node;)
        {
            const auto next = node.next_sibling();
            const auto name = local_name(node.name());
            if (name == "v" || name == "is")
            {
                cell.remove_child(node);
            }
            node = next;
        }
    }

    void set_type(Node cell, const char* type)
    {
        auto current = cell.attribute("t");
        if (!current)
        {
            for (auto item : cell.attributes())
            {
                if (local_name(item.name()) == "t")
                {
                    current = item;
                    break;
                }
            }
        }
        if (!type || !*type)
        {
            if (current)
            {
                cell.remove_attribute(current);
            }
        }
        else if (current)
        {
            current.set_value(type);
        }
        else
        {
            cell.append_attribute("t").set_value(type);
        }
    }

    void set_attribute(Node node, const char* name, const char* value)
    {
        auto current = node.attribute(name);
        if (current)
        {
            current.set_value(value);
        }
        else
        {
            node.append_attribute(name).set_value(value);
        }
    }

    void write_value(Node cell, const mirrorfly::SpreadsheetValue& value)
    {
        remove_value_nodes(cell);
        while (auto formula = child(cell, "f"))
            cell.remove_child(formula);
        if (value.kind == mirrorfly::SpreadsheetValueKind::Formula)
        {
            set_type(cell, nullptr);
            cell.append_child(qualified_name(cell, "f").c_str()).text().set(value.text.c_str());
            return;
        }
        if (value.kind == mirrorfly::SpreadsheetValueKind::Empty)
        {
            set_type(cell, nullptr);
            return;
        }
        if (value.kind == mirrorfly::SpreadsheetValueKind::Text)
        {
            set_type(cell, "inlineStr");
            auto inline_string = cell.append_child(qualified_name(cell, "is").c_str());
            auto text = inline_string.append_child(qualified_name(cell, "t").c_str());
            if (!value.text.empty() &&
                (std::isspace(static_cast<unsigned char>(value.text.front())) ||
                    std::isspace(static_cast<unsigned char>(value.text.back()))))
            {
                text.append_attribute("xml:space").set_value("preserve");
            }
            text.text().set(value.text.c_str());
            return;
        }
        const char* type = nullptr;
        std::string stored = value.text;
        if (value.kind == mirrorfly::SpreadsheetValueKind::Boolean)
        {
            type = "b";
            stored = value.text == "true" ? "1" : value.text == "false" ? "0" : value.text;
        }
        else if (value.kind == mirrorfly::SpreadsheetValueKind::Error)
        {
            type = "e";
        }
        set_type(cell, type);
        auto raw = cell.append_child(qualified_name(cell, "v").c_str());
        raw.text().set(stored.c_str());
    }

    std::string save_xml(const pugi::xml_document& document)
    {
        std::ostringstream output;
        document.save(output, "", pugi::format_raw, pugi::encoding_utf8);
        return output.str();
    }

    mirrorfly::OfficePart* mutable_part(std::vector<mirrorfly::OfficePart>& parts, const std::string& path)
    {
        const auto wanted = ascii_lower(path);
        for (auto& item : parts)
        {
            if (ascii_lower(item.path) == wanted)
            {
                return &item;
            }
        }
        return nullptr;
    }
}

namespace mirrorfly
{
    bool is_spreadsheet_path(const std::string& path)
    {
        if (!valid_xml_text(path) || path.find('\0') != std::string::npos)
        {
            return false;
        }
        return has_suffix(ascii_lower(path), ".xlsx");
    }

    std::optional<SpreadsheetAddress> parse_spreadsheet_address(const std::string& text)
    {
        if (text.empty())
        {
            return std::nullopt;
        }
        std::uint64_t column = 0;
        std::size_t index = 0;
        while (index < text.size() && std::isalpha(static_cast<unsigned char>(text[index])))
        {
            const auto character =
                static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(text[index])));
            column = column * 26 + character - 'A' + 1;
            if (column > maximum_spreadsheet_columns)
            {
                return std::nullopt;
            }
            ++index;
        }
        if (index == 0 || index == text.size() || text[index] == '0')
        {
            return std::nullopt;
        }
        std::uint64_t row = 0;
        while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])))
        {
            row = row * 10 + text[index] - '0';
            if (row > maximum_spreadsheet_rows)
            {
                return std::nullopt;
            }
            ++index;
        }
        if (index != text.size() || row == 0)
        {
            return std::nullopt;
        }
        return SpreadsheetAddress{
            static_cast<std::uint32_t>(row - 1), static_cast<std::uint32_t>(column - 1)};
    }

    std::string spreadsheet_address(SpreadsheetAddress address)
    {
        if (address.row >= maximum_spreadsheet_rows || address.column >= maximum_spreadsheet_columns)
        {
            return {};
        }
        std::string column;
        auto value = static_cast<std::uint64_t>(address.column) + 1;
        while (value)
        {
            const auto remainder = (value - 1) % 26;
            column.push_back(static_cast<char>('A' + remainder));
            value = (value - 1) / 26;
        }
        std::reverse(column.begin(), column.end());
        return column + std::to_string(static_cast<std::uint64_t>(address.row) + 1);
    }

    static SpreadsheetEditResult edit_sheet_structure(
        SpreadsheetDocument& document, const SpreadsheetSheetCommand& command)
    {
        if (document.read_only || !document.original_parts)
            return {SpreadsheetError::ReadOnly, "此工作簿不允许修改结构。", false};
        if (command.action == SpreadsheetSheetAction::RemoveAdded)
        {
            if (document.sheets.size() < 2 || command.index + 1 != document.sheets.size())
                return {SpreadsheetError::InvalidValue, "只能撤销末尾新建的空白工作表。", false};
            const auto& sheet = document.sheets.back();
            for (const auto& part : *document.original_parts)
                if (part.path == sheet.path)
                    return {SpreadsheetError::ReadOnly, "不能通过此操作删除导入工作表。", false};
            for (const auto& [address, cell] : sheet.cells)
                if (cell.formula_cell || cell.value.kind != SpreadsheetValueKind::Empty)
                    return {SpreadsheetError::InvalidValue, "工作表仍有内容。", false};
            const auto edits = document.edits.find(command.index);
            if (edits != document.edits.end())
                for (const auto& [address, value] : edits->second)
                    if (value.kind != SpreadsheetValueKind::Empty)
                        return {SpreadsheetError::InvalidValue, "工作表仍有内容。", false};
            document.sheets.pop_back();
            document.edits.erase(command.index);
            document.format_edits.erase(command.index);
            document.column_edits.erase(command.index);
            document.row_edits.erase(command.index);
            document.feature_edits.erase(command.index);
            return {SpreadsheetError::None, {}, true};
        }
        if (command.action == SpreadsheetSheetAction::Delete ||
            command.action == SpreadsheetSheetAction::Move ||
            command.action == SpreadsheetSheetAction::Hide || command.action == SpreadsheetSheetAction::Show)
            return edit_spreadsheet_workbook(document, command);
        if (command.action != SpreadsheetSheetAction::Add &&
            command.action != SpreadsheetSheetAction::Rename &&
            command.action != SpreadsheetSheetAction::Copy)
            return {SpreadsheetError::InvalidValue, "未知工作表操作。", false};
        if (!valid_xml_text(command.name) || command.name.empty() || command.name.front() == '\'' ||
            command.name.back() == '\'' || command.name.find_first_of("[]:*?/\\") != std::string::npos)
            return {SpreadsheetError::InvalidValue, "名称须为 1–31 个字符，不能包含 []:*?/\\。", false};
        std::size_t characters = 0;
        auto character = command.name.begin();
        while (character != command.name.end())
        {
            const auto code = utf8::next(character, command.name.end());
            if (code < 32 || code == 127)
                return {SpreadsheetError::InvalidValue, "名称不能包含控制字符。", false};
            characters += code > 0xffff ? 2 : 1;
        }
        if (characters > 31)
            return {SpreadsheetError::InvalidValue, "工作表名称最多 31 个字符。", false};
        for (std::size_t index = 0; index < document.sheets.size(); ++index)
            if ((command.action != SpreadsheetSheetAction::Rename || index != command.index) &&
                ascii_lower(document.sheets[index].name) == ascii_lower(command.name))
                return {SpreadsheetError::InvalidValue, "工作表名称已存在。", false};
        if (command.action == SpreadsheetSheetAction::Rename ||
            command.action == SpreadsheetSheetAction::Copy)
        {
            if (command.index >= document.sheets.size())
                return {SpreadsheetError::InvalidValue, "工作表不存在。", false};
            if (document.sheets[command.index].name == command.name)
                return {};
            return edit_spreadsheet_workbook(document, command);
        }
        else
        {
            if (document.sheets.size() >= maximum_spreadsheet_sheets ||
                command.index != document.sheets.size())
                return {SpreadsheetError::TooLarge, "最多支持 64 个工作表，只能在末尾新建。", false};
            SpreadsheetSheet sheet;
            sheet.name = command.name;
            std::size_t identifier = 1;
            for (;; ++identifier)
            {
                sheet.path = "xl/worksheets/mirrorfly" + std::to_string(identifier) + ".xml";
                const bool in_package = std::any_of(document.original_parts->begin(),
                    document.original_parts->end(), [&](const OfficePart& part)
                {
                    return ascii_lower(part.path) == ascii_lower(sheet.path);
                });
                const bool in_sheets = std::any_of(document.sheets.begin(), document.sheets.end(),
                    [&](const SpreadsheetSheet& existing)
                {
                    return existing.path == sheet.path;
                });
                if (!in_package && !in_sheets)
                    break;
            }
            document.sheets.push_back(std::move(sheet));
        }
        return {SpreadsheetError::None, {}, true};
    }

    SpreadsheetEditResult apply_spreadsheet_sheet_command(
        SpreadsheetDocument& document, const SpreadsheetSheetCommand& command)
    {
        try
        {
            return edit_sheet_structure(document, command);
        }
        catch (const Failure& failure)
        {
            return {failure.error, failure.message, false};
        }
        catch (const std::bad_alloc&)
        {
            return {SpreadsheetError::TooLarge, "工作表结构操作内存不足。", false};
        }
    }

    SpreadsheetDocument make_spreadsheet()
    {
        auto result = parse_spreadsheet(blank_package());
        return result.error == SpreadsheetError::None ? std::move(result.document) : SpreadsheetDocument{};
    }

    SpreadsheetResult parse_spreadsheet(std::vector<OfficePart> parts)
    {
        SpreadsheetResult result;
        try
        {
            auto package = make_package(parts);
            auto content_types = load_xml(package, "[Content_Types].xml");
            std::set<std::string> workbook_content_paths;
            bool unsafe_type = false;
            for (auto node : content_types.document_element().children())
            {
                const auto type = attribute(node, "ContentType");
                if (type == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml")
                {
                    auto path = attribute(node, "PartName");
                    if (!path.empty() && path.front() == '/')
                    {
                        path.erase(path.begin());
                    }
                    if (valid_part_path(path))
                    {
                        workbook_content_paths.insert(ascii_lower(path));
                    }
                }
                unsafe_type = unsafe_type || type.find("macroEnabled") != std::string::npos ||
                    type.find("vbaProject") != std::string::npos;
            }
            if (workbook_content_paths.empty())
            {
                throw Failure{SpreadsheetError::InvalidPackage, "文件不是受支持的 XLSX 工作簿。"};
            }
            const auto root_relationships = relationships(package, "");
            std::string workbook_path;
            for (const auto& item : root_relationships)
            {
                if (relationship_type(item.second.type, "officeDocument"))
                {
                    if (item.second.external)
                    {
                        throw Failure{SpreadsheetError::InvalidPackage, "工作簿入口不能是外部关联。"};
                    }
                    if (!workbook_path.empty())
                    {
                        throw Failure{SpreadsheetError::InvalidPackage, "工作簿包含多个入口关联。"};
                    }
                    workbook_path = item.second.target;
                }
            }
            if (workbook_path.empty())
            {
                throw Failure{SpreadsheetError::MissingPart, "工作簿缺少入口关联。"};
            }
            if (!workbook_content_paths.count(ascii_lower(workbook_path)))
            {
                throw Failure{SpreadsheetError::InvalidPackage, "工作簿入口内容类型不匹配。"};
            }
            auto workbook = load_xml(package, workbook_path);
            if (local_name(workbook.document_element().name()) != "workbook")
            {
                throw Failure{SpreadsheetError::InvalidPackage, "工作簿入口内容无效。"};
            }
            const auto workbook_relationships = relationships(package, workbook_path);
            const auto shared = read_shared_strings(package, workbook_relationships);
            SpreadsheetDocument document;
            document.workbook_path = workbook_path;
            pugi::xml_document theme;
            SpreadsheetColors colors;
            for (const auto& item : workbook_relationships)
                if (!item.second.external && relationship_type(item.second.type, "theme"))
                {
                    if (theme.document_element())
                        throw Failure{SpreadsheetError::InvalidPackage, "工作簿包含多个主题。"};
                    theme = load_xml(package, item.second.target);
                }

            for (const auto& item : workbook_relationships)
            {
                if (!item.second.external && relationship_type(item.second.type, "styles"))
                {
                    if (!document.styles_path.empty())
                    {
                        throw Failure{SpreadsheetError::InvalidPackage, "工作簿包含多个样式表。"};
                    }
                    document.styles_path = item.second.target;
                    auto styles = load_xml(package, document.styles_path);
                    colors = SpreadsheetColors(styles.document_element(), theme.document_element());
                    read_spreadsheet_styles(document, styles.document_element(), colors);
                }
            }
            if (unsafe_type)
            {
                mark_read_only(document, "工作簿包含宏或未支持的可执行结构，当前仅支持只读查看。");
            }
            const auto workbook_protection = child(workbook.document_element(), "workbookProtection");
            document.date_1904 = active_flag(child(workbook.document_element(), "workbookPr"), "date1904");
            if (workbook_protection &&
                (active_flag(workbook_protection, "lockStructure") ||
                    active_flag(workbook_protection, "lockWindows") ||
                    active_flag(workbook_protection, "lockRevision")))
            {
                mark_read_only(document, "工作簿结构受保护，当前仅支持只读查看。");
            }
            for (const auto& item : package.parts)
            {
                if (item.first.find("_xmlsignatures/") == 0)
                {
                    mark_read_only(document, "工作簿带有数字签名，编辑会使签名失效。");
                }
            }
            for (const auto& item : workbook_relationships)
            {
                if (item.second.external)
                {
                    mark_read_only(document, "工作簿包含外部关联，当前不会访问外部资源并仅支持只读查看。");
                }
                if (relationship_type(item.second.type, "externalLink"))
                {
                    mark_read_only(document, "工作簿包含外部链接，当前不会访问外部资源并仅支持只读查看。");
                }
                if (relationship_type(item.second.type, "sheetMetadata") ||
                    relationship_type(item.second.type, "metadata"))
                {
                    mark_read_only(document, "工作簿包含未支持的动态数组或值元数据，当前仅支持只读查看。");
                }
            }
            std::size_t cell_count = 0;
            std::size_t text_bytes = 0;
            std::set<std::string> sheet_names;
            for (auto sheet : child(workbook.document_element(), "sheets").children())
            {
                if (local_name(sheet.name()) != "sheet")
                {
                    continue;
                }
                if (document.sheets.size() >= maximum_spreadsheet_sheets)
                {
                    throw Failure{SpreadsheetError::TooLarge, "工作表数量超过限制。"};
                }
                const auto name = attribute(sheet, "name");
                const auto relationship_id = attribute(sheet, "id");
                const auto relation = workbook_relationships.find(relationship_id);
                if (name.empty() || name.size() > 124 || !valid_xml_text(name) ||
                    !sheet_names.insert(ascii_lower(name)).second ||
                    relation == workbook_relationships.end() || relation->second.external ||
                    !relationship_type(relation->second.type, "worksheet"))
                {
                    throw Failure{SpreadsheetError::InvalidPackage, "工作表名称或关联无效。"};
                }
                document.sheets.push_back(
                    read_sheet(package, relation->second.target, name, shared, cell_count, text_bytes));
                const auto visibility = attribute(sheet, "state");
                if (!visibility.empty() && visibility != "visible" && visibility != "hidden" &&
                    visibility != "veryHidden")
                    throw Failure{SpreadsheetError::InvalidPackage, "工作表可见性无效。"};
                document.sheets.back().hidden = visibility == "hidden" || visibility == "veryHidden";
                auto settings_xml = load_xml(package, relation->second.target);
                read_spreadsheet_features(document.sheets.back(), settings_xml.document_element(), parts,
                    document.styles_path, colors);
                std::vector<std::string> table_paths;
                const auto sheet_relationships = child(settings_xml.document_element(), "tableParts")
                    ? relationships(package, relation->second.target)
                    : std::map<std::string, Relationship>{};
                for (auto table : child(settings_xml.document_element(), "tableParts").children())
                {
                    const auto found = sheet_relationships.find(attribute(table, "id"));
                    if (found == sheet_relationships.end() || found->second.external ||
                        !relationship_type(found->second.type, "table"))
                        throw Failure{SpreadsheetError::InvalidPackage, "表格部件关联无效。"};
                    table_paths.push_back(found->second.target);
                }
                read_spreadsheet_tables(document.sheets.back(), table_paths, parts, document.styles_path,
                    package.xml_nodes, colors);
            }
            if (document.sheets.empty())
            {
                throw Failure{SpreadsheetError::MissingPart, "工作簿没有可显示的工作表。"};
            }
            if (std::all_of(document.sheets.begin(), document.sheets.end(), [](const auto& sheet)
            {
                return sheet.hidden;
            }))
                throw Failure{SpreadsheetError::InvalidPackage, "工作簿至少需要一个可见工作表。"};
            std::set<std::string> table_names, table_paths;
            std::set<std::uint32_t> table_ids;
            for (const auto& sheet : document.sheets)
                for (const auto& table : sheet.features.tables)
                    if (!table_names.insert(ascii_lower(table.name)).second ||
                        !table_paths.insert(ascii_lower(table.path)).second ||
                        !table_ids.insert(table.id).second)
                        throw Failure{SpreadsheetError::InvalidPackage, "表格名称或部件标识重复。"};
            document.original_parts = std::make_shared<const std::vector<OfficePart>>(std::move(parts));
            result.document = std::move(document);
        }
        catch (const Failure& failure)
        {
            result.error = failure.error;
            result.message = failure.message;
        }
        catch (const std::bad_alloc&)
        {
            result.error = SpreadsheetError::TooLarge;
            result.message = "工作簿内容超过内存预算。";
        }
        catch (const std::exception& error)
        {
            result.error = SpreadsheetError::InvalidXml;
            result.message = error.what();
        }
        return result;
    }

    SpreadsheetCell spreadsheet_cell_properties(
        const SpreadsheetDocument& document, std::size_t sheet_index, SpreadsheetAddress address)
    {
        SpreadsheetCell result;
        if (sheet_index >= document.sheets.size() || address.row >= maximum_spreadsheet_rows ||
            address.column >= maximum_spreadsheet_columns)
        {
            result.editable = false;
            result.read_only_reason = "单元格地址无效。";
            return result;
        }
        const auto& sheet = document.sheets[sheet_index];
        const auto found = sheet.cells.find(address);
        if (found != sheet.cells.end())
        {
            result = found->second;
        }
        const auto sheet_edits = document.edits.find(sheet_index);
        if (sheet_edits != document.edits.end())
        {
            const auto edit = sheet_edits->second.find(address);
            if (edit != sheet_edits->second.end())
            {
                result.value = edit->second;
                result.formula_cell = edit->second.kind == SpreadsheetValueKind::Formula;
                result.formula = result.formula_cell ? edit->second.text : std::string{};
                result.formula_supported = result.formula_cell;
            }
        }
        if (document.read_only)
        {
            result.editable = false;
            result.read_only_reason = document.read_only_reason;
        }
        else if (!sheet.editable)
        {
            result.editable = false;
            result.read_only_reason = sheet.read_only_reason;
        }
        else
        {
            for (const auto& range : sheet.protected_ranges)
            {
                if (contains(range, address))
                {
                    result.editable = false;
                    if (result.read_only_reason.empty())
                    {
                        result.read_only_reason = "单元格属于合并或公式影响范围，当前不可编辑。";
                    }
                    break;
                }
            }
        }
        if (result.editable)
            for (const auto& table : spreadsheet_features(document, sheet_index).tables)
                if (!table.supported && address.row == table.range.first.row &&
                    address.column >= table.range.first.column && address.column <= table.range.last.column)
                {
                    result.editable = false;
                    result.read_only_reason = "此导入表格结构较复杂，暂不能修改其表头。";
                }
        if (result.editable)
            for (auto r : spreadsheet_features(document, sheet_index).merges)
                if (contains(r, address) && (address.row != r.first.row || address.column != r.first.column))
                {
                    result.editable = false;
                    result.merged_covered = true;
                    result.read_only_reason = "请编辑合并区域的左上单元格。";
                    break;
                }
        return result;
    }

    SpreadsheetCell spreadsheet_cell(const SpreadsheetDocument& document, std::size_t sheet,
        SpreadsheetAddress address, const SpreadsheetCalculation* calculation)
    {
        auto result = spreadsheet_cell_properties(document, sheet, address);
        if (result.formula_supported)
        {
            if (!calculation)
                result.value = spreadsheet_calculate(document, sheet, address);
            else
            {
                const auto values = calculation->values.find(sheet);
                result.value = {SpreadsheetValueKind::Error, "#NUM!"};
                if (values != calculation->values.end())
                {
                    const auto value = values->second.find(address);
                    if (value != values->second.end())
                        result.value = value->second;
                }
            }
        }
        return result;
    }

    SpreadsheetEditResult apply_spreadsheet_edit(
        SpreadsheetDocument& document, const SpreadsheetEditCommand& command)
    {
        return apply_spreadsheet_edits(document, {command});
    }

    SpreadsheetEditResult apply_spreadsheet_edits(
        SpreadsheetDocument& document, const std::vector<SpreadsheetEditCommand>& commands)
    {
        if (commands.size() > maximum_spreadsheet_batch_cells)
        {
            return {SpreadsheetError::TooLarge, "单次最多修改 4096 个单元格。", false};
        }
        try
        {
            auto proposed = document.edits;
            std::map<std::size_t, std::set<SpreadsheetAddress>> seen;
            bool changed = false;
            for (const auto& command : commands)
            {
                if (command.sheet_index >= document.sheets.size() ||
                    command.address.row >= maximum_spreadsheet_rows ||
                    command.address.column >= maximum_spreadsheet_columns ||
                    !seen[command.sheet_index].insert(command.address).second)
                {
                    return {SpreadsheetError::InvalidValue, "批量编辑包含无效或重复地址。", false};
                }
                const auto current =
                    spreadsheet_cell_properties(document, command.sheet_index, command.address);
                if (!current.editable &&
                    !(current.merged_covered && command.value.kind == SpreadsheetValueKind::Empty))
                {
                    return {SpreadsheetError::ReadOnly, current.read_only_reason, false};
                }
                for (auto r : spreadsheet_features(document, command.sheet_index).merges)
                    if (contains(r, command.address) &&
                        (command.address.row != r.first.row || command.address.column != r.first.column) &&
                        command.value.kind != SpreadsheetValueKind::Empty)
                        return {SpreadsheetError::ReadOnly, "请编辑合并区域的左上单元格。", false};
                if (!valid_value(command.value))
                {
                    return {SpreadsheetError::InvalidValue, "单元格值无效。", false};
                }
                if (same_value(spreadsheet_source_value(document, command.sheet_index, command.address),
                        command.value))
                    continue;
                changed = true;
                const auto& cells = document.sheets[command.sheet_index].cells;
                const auto original = cells.find(command.address);
                auto original_value = original == cells.end() ? SpreadsheetValue{} : original->second.value;
                if (original != cells.end() && original->second.formula_cell)
                    original_value = {SpreadsheetValueKind::Formula, original->second.formula};
                auto& edits = proposed[command.sheet_index];
                if (same_value(original_value, command.value))
                    edits.erase(command.address);
                else
                    edits[command.address] = command.value;
                if (edits.empty())
                    proposed.erase(command.sheet_index);
            }
            if (!changed)
                return {};
            for (std::size_t sheet = 0; sheet < document.sheets.size(); ++sheet)
                for (const auto& table : spreadsheet_features(document, sheet).tables)
                    if (table.supported &&
                        !valid_spreadsheet_table_headers(document, sheet, table, &proposed))
                        return {SpreadsheetError::InvalidValue,
                            "表格表头必须为非空、互不重复的文字；未支持的公式引用表头时需保留原名。", false};
            std::size_t cells = 0;
            std::size_t text_bytes = 0;
            for (std::size_t index = 0; index < document.sheets.size(); ++index)
            {
                const auto& originals = document.sheets[index].cells;
                const auto edits = proposed.find(index);
                for (const auto& item : originals)
                {
                    ++cells;
                    const SpreadsheetValue* value = &item.second.value;
                    if (edits != proposed.end())
                    {
                        const auto edit = edits->second.find(item.first);
                        if (edit != edits->second.end())
                            value = &edit->second;
                    }
                    text_bytes =
                        add_size(text_bytes, add_size(value->text.size(), item.second.formula.size()));
                }
                if (edits != proposed.end())
                {
                    for (const auto& edit : edits->second)
                    {
                        if (originals.count(edit.first) == 0)
                        {
                            ++cells;
                            text_bytes = add_size(text_bytes, edit.second.text.size());
                        }
                    }
                }
            }
            for (const auto& [sheet_index, formats] : document.format_edits)
            {
                if (sheet_index >= document.sheets.size())
                    return {SpreadsheetError::InvalidValue, "样式工作表索引无效。", false};
                const auto values_patch = proposed.find(sheet_index);
                for (const auto& [address, format] : formats)
                    if (!document.sheets[sheet_index].cells.count(address) &&
                        (values_patch == proposed.end() || !values_patch->second.count(address)))
                        ++cells;
            }
            if (cells > maximum_spreadsheet_cells || text_bytes > maximum_spreadsheet_text_bytes)
            {
                return {SpreadsheetError::TooLarge, "编辑会超过工作簿单元格或文本预算。", false};
            }
            document.edits = std::move(proposed);
            document.caches_stale = document.caches_stale || !document.edits.empty();
            for (const auto& command : commands)
            {
                auto& sheet = document.sheets[command.sheet_index];
                sheet.rows = std::max(sheet.rows, command.address.row + 1);
                sheet.columns = std::max(sheet.columns, command.address.column + 1);
            }
            return {SpreadsheetError::None, {}, true};
        }
        catch (const std::bad_alloc&)
        {
            return {SpreadsheetError::TooLarge, "批量编辑超过内存预算，原内容已保留。", false};
        }
    }

    SpreadsheetPackageResult serialize_spreadsheet(const SpreadsheetDocument& document)
    {
        if (!document.original_parts || document.sheets.empty())
        {
            return package_failure(SpreadsheetError::InvalidPackage, "工作簿缺少可保存的原始包。");
        }
        try
        {
            for (const auto& sheet_edits : document.edits)
            {
                if (sheet_edits.first >= document.sheets.size())
                {
                    throw Failure{SpreadsheetError::InvalidValue, "编辑包含无效工作表。"};
                }
                for (const auto& edit : sheet_edits.second)
                {
                    if (edit.first.row >= maximum_spreadsheet_rows ||
                        edit.first.column >= maximum_spreadsheet_columns || !valid_value(edit.second))
                    {
                        throw Failure{SpreadsheetError::InvalidValue, "编辑包含无效单元格值。"};
                    }
                    const auto cell = spreadsheet_cell_properties(document, sheet_edits.first, edit.first);
                    if (!cell.editable &&
                        !(cell.merged_covered && edit.second.kind == SpreadsheetValueKind::Empty))
                    {
                        throw Failure{SpreadsheetError::ReadOnly,
                            cell.read_only_reason.empty() ? "编辑包含只读单元格。" : cell.read_only_reason};
                    }
                }
            }
            std::vector<OfficePart> parts = *document.original_parts;
            bool structure_changed = false;
            {
                auto package = make_package(parts);
                auto workbook = load_xml(package, document.workbook_path);
                auto relations = load_xml(package, relationships_path(document.workbook_path));
                auto types = load_xml(package, "[Content_Types].xml");
                auto sheets = child(workbook.document_element(), "sheets");
                const auto original_relations = relationships(package, document.workbook_path);
                std::set<std::string> relation_ids;
                unsigned long long next_sheet_id = 1;
                for (auto node : sheets.children())
                {
                    const auto sheet_id = node.attribute("sheetId").as_ullong();
                    if (sheet_id > 0xffffffffULL)
                        throw Failure{SpreadsheetError::InvalidValue, "工作表编号无效。"};
                    next_sheet_id = std::max(next_sheet_id, sheet_id + 1);
                }
                for (const auto& [id, relation] : original_relations)
                    relation_ids.insert(id);
                for (const auto& sheet : document.sheets)
                {
                    Node existing;
                    for (auto node : sheets.children())
                    {
                        const auto relation = original_relations.find(attribute(node, "id"));
                        if (relation != original_relations.end() && relation->second.target == sheet.path)
                        {
                            existing = node;
                            break;
                        }
                    }
                    if (existing)
                    {
                        if (attribute(existing, "name") != sheet.name)
                        {
                            set_attribute(existing, "name", sheet.name.c_str());
                            structure_changed = true;
                        }
                        continue;
                    }
                    if (!valid_part_path(sheet.path) || mutable_part(parts, sheet.path))
                        throw Failure{SpreadsheetError::InvalidPackage, "新增工作表路径冲突。"};
                    auto blank = blank_package();
                    const auto blank_sheet =
                        std::find_if(blank.begin(), blank.end(), [](const OfficePart& part)
                    {
                        return part.path == "xl/worksheets/sheet1.xml";
                    });
                    if (blank_sheet == blank.end())
                        throw Failure{SpreadsheetError::InvalidPackage, "缺少空白工作表。"};
                    parts.push_back({sheet.path, blank_sheet->bytes});
                    std::size_t number = 1;
                    while (relation_ids.count("mirrorflySheet" + std::to_string(number)))
                        ++number;
                    const auto id = "mirrorflySheet" + std::to_string(number);
                    relation_ids.insert(id);
                    auto relation = relations.document_element().append_child(
                        qualified_name(relations.document_element(), "Relationship").c_str());
                    relation.append_attribute("Id") = id.c_str();
                    relation.append_attribute("Type") =
                        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet";
                    relation.append_attribute("Target") = ("/" + sheet.path).c_str();
                    auto node = sheets.append_child(qualified_name(sheets, "sheet").c_str());
                    node.append_attribute("name") = sheet.name.c_str();
                    if (next_sheet_id > 0xffffffffULL)
                        throw Failure{SpreadsheetError::TooLarge, "工作表编号已达上限。"};
                    node.append_attribute("sheetId").set_value(next_sheet_id++);
                    node.append_attribute("xmlns:mfrel") =
                        "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
                    node.append_attribute("mfrel:id") = id.c_str();
                    auto type = types.document_element().append_child(
                        qualified_name(types.document_element(), "Override").c_str());
                    type.append_attribute("PartName") = ("/" + sheet.path).c_str();
                    type.append_attribute("ContentType") =
                        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml";
                    structure_changed = true;
                }
                if (structure_changed)
                {
                    mutable_part(parts, document.workbook_path)->bytes = save_xml(workbook);
                    mutable_part(parts, relationships_path(document.workbook_path))->bytes =
                        save_xml(relations);
                    mutable_part(parts, "[Content_Types].xml")->bytes = save_xml(types);
                }
            }
            const bool changed =
                std::any_of(document.edits.begin(), document.edits.end(), [](const auto& item)
            {
                return !item.second.empty();
            });
            SpreadsheetCalculation calculation;
            if (changed || document.caches_stale || !document.feature_edits.empty())
            {
                calculation = spreadsheet_calculate_all(document);
                if (!calculation.complete)
                    return package_failure(SpreadsheetError::TooLarge,
                        "公式计算超过整表工作量限制，文件尚未写入；请简化重复的大范围公式后重试。");
            }
            for (std::size_t sheet_index = 0; sheet_index < document.sheets.size(); ++sheet_index)
            {
                auto* output = mutable_part(parts, document.sheets[sheet_index].path);
                if (!output)
                {
                    throw Failure{SpreadsheetError::MissingPart, "工作簿缺少工作表内容。"};
                }
                if (!changed && !document.caches_stale)
                {
                    continue;
                }
                Package package = make_package(parts);
                auto sheet = load_xml(package, output->path);
                auto sheet_data = child(sheet.document_element(), "sheetData");
                const auto edits = document.edits.find(sheet_index);
                const bool has_edits = edits != document.edits.end() && !edits->second.empty();
                if (has_edits && !sheet_data)
                {
                    sheet_data = sheet.document_element().append_child(
                        qualified_name(sheet.document_element(), "sheetData").c_str());
                }
                bool sheet_changed = has_edits;
                if (has_edits)
                {
                    for (const auto& edit : edits->second)
                    {
                        write_value(ensure_cell(sheet_data, edit.first), edit.second);
                    }
                }
                for (auto row : sheet_data.children())
                {
                    for (auto cell : row.children())
                    {
                        if (child(cell, "f"))
                        {
                            sheet_changed = sheet_changed || child(cell, "v") || child(cell, "is");
                            remove_value_nodes(cell);
                            const auto address = parse_spreadsheet_address(attribute(cell, "r"));
                            if (address &&
                                spreadsheet_cell_properties(document, sheet_index, *address)
                                    .formula_supported)
                            {
                                const auto value =
                                    spreadsheet_cell(document, sheet_index, *address, &calculation).value;
                                const char* type = nullptr;
                                if (value.kind == SpreadsheetValueKind::Error)
                                    type = "e";
                                else if (value.kind == SpreadsheetValueKind::Text)
                                    type = "str";
                                else if (value.kind == SpreadsheetValueKind::Boolean)
                                    type = "b";
                                set_type(cell, type);
                                auto cached = value.text;
                                if (value.kind == SpreadsheetValueKind::Boolean)
                                    cached = value.text == "1" || value.text == "true" ? "1" : "0";
                                cell.append_child(qualified_name(cell, "v").c_str())
                                    .text()
                                    .set(cached.c_str());
                                sheet_changed = true;
                            }
                        }
                    }
                }
                if (sheet_changed)
                {
                    output->bytes = save_xml(sheet);
                }
            }
            if (changed || document.caches_stale)
            {
                Package package = make_package(parts);
                const auto root_relationships = relationships(package, "");
                std::string workbook_path;
                for (const auto& item : root_relationships)
                {
                    if (!item.second.external && relationship_type(item.second.type, "officeDocument"))
                    {
                        workbook_path = item.second.target;
                        break;
                    }
                }
                auto* workbook_part = mutable_part(parts, workbook_path);
                auto* relations_part = mutable_part(parts, relationships_path(workbook_path));
                auto* types_part = mutable_part(parts, "[Content_Types].xml");
                if (!workbook_part || !relations_part || !types_part)
                {
                    throw Failure{SpreadsheetError::MissingPart, "工作簿缺少计算设置内容。"};
                }
                package = make_package(parts);
                auto workbook = load_xml(package, workbook_part->path);
                auto calculation_properties = child(workbook.document_element(), "calcPr");
                if (!calculation_properties)
                {
                    calculation_properties = workbook.document_element().append_child(
                        qualified_name(workbook.document_element(), "calcPr").c_str());
                }
                set_attribute(calculation_properties, "calcMode", "auto");
                set_attribute(calculation_properties, "fullCalcOnLoad", "1");
                set_attribute(calculation_properties, "forceFullCalc", "1");
                workbook_part->bytes = save_xml(workbook);

                package = make_package(parts);
                auto relations_document = load_xml(package, relations_part->path);
                std::set<std::string> calc_paths;
                bool relations_changed = false;
                for (auto node = relations_document.document_element().first_child(); node;)
                {
                    const auto next = node.next_sibling();
                    if (local_name(node.name()) == "Relationship" &&
                        relationship_type(attribute(node, "Type"), "calcChain"))
                    {
                        if (ascii_lower(attribute(node, "TargetMode")) != "external")
                        {
                            calc_paths.insert(
                                ascii_lower(resolve_target(workbook_path, attribute(node, "Target"))));
                        }
                        relations_document.document_element().remove_child(node);
                        relations_changed = true;
                    }
                    node = next;
                }
                if (relations_changed)
                {
                    relations_part->bytes = save_xml(relations_document);
                }

                package = make_package(parts);
                auto types = load_xml(package, types_part->path);
                bool types_changed = false;
                for (auto node = types.document_element().first_child(); node;)
                {
                    const auto next = node.next_sibling();
                    if (local_name(node.name()) == "Override" &&
                        attribute(node, "ContentType").find("calcChain") != std::string::npos)
                    {
                        const auto path = attribute(node, "PartName");
                        calc_paths.insert(
                            ascii_lower(path.empty() || path.front() != '/' ? path : path.substr(1)));
                        types.document_element().remove_child(node);
                        types_changed = true;
                    }
                    node = next;
                }
                if (types_changed)
                {
                    types_part->bytes = save_xml(types);
                }
                parts.erase(std::remove_if(parts.begin(), parts.end(),
                                [&calc_paths](const OfficePart& item)
                {
                    return calc_paths.count(ascii_lower(item.path)) != 0;
                }),
                    parts.end());
            }
            auto formatted = write_spreadsheet_format_parts(document, std::move(parts));
            if (formatted.error != SpreadsheetError::None)
            {
                return formatted;
            }
            auto configured = write_spreadsheet_features(document, std::move(formatted.parts), &calculation);
            if (configured.error != SpreadsheetError::None)
                return configured;
            auto tables = write_spreadsheet_tables(document, std::move(configured.parts));
            if (tables.error != SpreadsheetError::None)
                return tables;
            parts = std::move(tables.parts);
            make_package(parts);
            if (structure_changed || changed || document.caches_stale || !document.format_edits.empty() ||
                !document.column_edits.empty() || !document.row_edits.empty() ||
                !document.feature_edits.empty())
            {
                auto validation_parts = parts;
                const auto validation = parse_spreadsheet(std::move(validation_parts));
                if (validation.error != SpreadsheetError::None)
                {
                    return package_failure(validation.error,
                        validation.message.empty() ? "保存后的工作簿无法重新读取。" : validation.message);
                }
            }
            SpreadsheetPackageResult result;
            result.parts = std::move(parts);
            return result;
        }
        catch (const Failure& failure)
        {
            return package_failure(failure.error, failure.message);
        }
        catch (const std::bad_alloc&)
        {
            return package_failure(SpreadsheetError::TooLarge, "工作簿保存内容超过内存预算。");
        }
    }
}
