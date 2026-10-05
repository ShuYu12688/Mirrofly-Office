#pragma once
#include <mirrorfly/spreadsheet.hpp>
#include <pugixml.hpp>

namespace mirrorfly
{
    class SpreadsheetColors;
    void read_spreadsheet_features(SpreadsheetSheet& sheet, pugi::xml_node root,
        const std::vector<OfficePart>& parts, const std::string& styles_path,
        const SpreadsheetColors& colors);
    SpreadsheetPackageResult write_spreadsheet_features(const SpreadsheetDocument& document,
        std::vector<OfficePart> parts, const SpreadsheetCalculation* calculation);
}
