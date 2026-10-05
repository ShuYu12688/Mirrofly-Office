#pragma once

#include <mirrorfly/spreadsheet.hpp>
#include <pugixml.hpp>

namespace mirrorfly
{
    bool spreadsheet_filter_matches(const SpreadsheetCell& cell, const SpreadsheetFilter& filter);
    bool valid_spreadsheet_filters(const SpreadsheetFeatures& features);
    void read_spreadsheet_filters(pugi::xml_node node, SpreadsheetFeatures& features);
    void write_spreadsheet_filters(pugi::xml_node node, const SpreadsheetFeatures& features);
}
