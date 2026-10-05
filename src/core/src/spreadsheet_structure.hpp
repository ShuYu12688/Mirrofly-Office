#pragma once

#include <mirrorfly/spreadsheet.hpp>
#include <pugixml.hpp>

namespace mirrorfly::spreadsheet_structure
{
    using Node = pugi::xml_node;
    struct Failure
    {
        SpreadsheetError error;
        std::string message;
    };
    void require(bool condition, const char* message);
    std::string local(const char* name);
    Node child(Node parent, const char* name);
    std::string qualified(Node parent, const char* name);
    Node add(Node parent, const char* name);
    void set(Node node, const char* name, const std::string& text);
    OfficePart* part(std::vector<OfficePart>& parts, const std::string& path);
    pugi::xml_document load(std::vector<OfficePart>& parts, const std::string& path);
    void store(std::vector<OfficePart>& parts, const std::string& path, const pugi::xml_document& xml);
    std::string reference(SpreadsheetRange range);
    SpreadsheetRange parse_range(const std::string& text);
    void require_supported(const SpreadsheetDocument& document, const std::vector<OfficePart>& parts);
    void require_axis_sheet(Node root);
    std::string relations_path(const std::string& source);
}

namespace mirrorfly
{
    SpreadsheetEditResult edit_spreadsheet_workbook(
        SpreadsheetDocument& document, const SpreadsheetSheetCommand& command);
}
