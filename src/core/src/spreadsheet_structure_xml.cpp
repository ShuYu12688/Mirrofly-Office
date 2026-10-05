#include "spreadsheet_structure.hpp"
#include "spreadsheet_tables.hpp"

#include <algorithm>
#include <sstream>

namespace mirrorfly::spreadsheet_structure
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw Failure{SpreadsheetError::ReadOnly, message};
    }
    std::string local(const char* name)
    {
        const std::string text(name);
        const auto colon = text.find(':');
        return colon == std::string::npos ? text : text.substr(colon + 1);
    }
    Node child(Node parent, const char* name)
    {
        for (auto node : parent.children())
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
    void set(Node node, const char* name, const std::string& text)
    {
        auto attr = node.attribute(name);
        if (!attr)
            attr = node.append_attribute(name);
        attr = text.c_str();
    }
    OfficePart* part(std::vector<OfficePart>& parts, const std::string& path)
    {
        for (auto& item : parts)
            if (item.path == path)
                return &item;
        return nullptr;
    }
    pugi::xml_document load(std::vector<OfficePart>& parts, const std::string& path)
    {
        const auto source = part(parts, path);
        pugi::xml_document xml;
        require(source && source->bytes.size() <= maximum_spreadsheet_xml_bytes &&
                xml.load_buffer(source->bytes.data(), source->bytes.size()),
            "结构操作需要有效的工作簿部件。");
        return xml;
    }
    void store(std::vector<OfficePart>& parts, const std::string& path, const pugi::xml_document& xml)
    {
        const auto target = part(parts, path);
        require(target != nullptr, "结构操作缺少目标部件。");
        std::ostringstream out;
        xml.save(out, "", pugi::format_raw, pugi::encoding_utf8);
        target->bytes = out.str();
    }
    std::string reference(SpreadsheetRange range)
    {
        return spreadsheet_address(range.first) + ':' + spreadsheet_address(range.last);
    }
    SpreadsheetRange parse_range(const std::string& text)
    {
        const auto colon = text.find(':');
        const auto first = parse_spreadsheet_address(text.substr(0, colon));
        const auto last =
            colon == std::string::npos ? first : parse_spreadsheet_address(text.substr(colon + 1));
        require(first && last && first->row <= last->row && first->column <= last->column,
            "此区域地址暂不能安全调整。");
        return {*first, *last};
    }
    void require_supported(const SpreadsheetDocument& document, const std::vector<OfficePart>& parts)
    {
        require(!document.read_only, "此工作簿不允许修改结构。");
        for (std::size_t index = 0; index < document.sheets.size(); ++index)
        {
            const auto& sheet = document.sheets[index];
            require(sheet.editable, "工作簿含受保护的工作表，暂不能调整引用。");
            const auto& features = spreadsheet_features(document, index);
            require(features.conditions_supported && features.filter_supported && features.panes_supported &&
                    features.tables_supported,
                "工作簿含暂未支持的区域设置，无法安全调整结构。");
            const auto tables = validate_spreadsheet_tables(document, index, features);
            if (tables.error != SpreadsheetError::None)
                throw Failure{tables.error, tables.message};
            for (const auto& [address, cell] : sheet.cells)
                require(!cell.formula_cell || cell.formula_supported,
                    "工作簿含未支持的公式，暂不能自动调整其引用。");
        }
        for (const auto& item : parts)
        {
            auto path = item.path;
            std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c)
            {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c);
            });
            require(path.find("/charts/") == std::string::npos && path.find("/pivot") == std::string::npos &&
                    path.find("/querytables/") == std::string::npos &&
                    path.find("/externallinks/") == std::string::npos,
                "工作簿含图表、数据透视表或外部区域引用，暂不能安全调整结构。");
        }
    }
    void require_axis_sheet(Node root)
    {
        const std::set<std::string> allowed{"sheetPr", "dimension", "sheetViews", "sheetFormatPr", "cols",
            "sheetData", "sheetCalcPr", "autoFilter", "mergeCells", "conditionalFormatting", "printOptions",
            "pageMargins", "pageSetup", "headerFooter", "tableParts"};
        for (auto node : root.children())
            require(allowed.count(local(node.name())) != 0,
                "此工作表含批注、绘图、验证或其他未支持的定位对象，请保留原行列结构。");
    }
    std::string relations_path(const std::string& source)
    {
        const auto slash = source.rfind('/');
        return source.substr(0, slash + 1) + "_rels/" + source.substr(slash + 1) + ".rels";
    }
}
