#pragma once

#include <mirrorfly/spreadsheet.hpp>
#include <pugixml.hpp>

namespace mirrorfly
{
    class SpreadsheetColors;
    void read_spreadsheet_styles(
        SpreadsheetDocument& document, pugi::xml_node styles, const SpreadsheetColors& colors);
    void read_spreadsheet_dimensions(SpreadsheetSheet& sheet, pugi::xml_node worksheet);
    SpreadsheetPackageResult write_spreadsheet_format_parts(
        const SpreadsheetDocument& document, std::vector<OfficePart> parts);
}
