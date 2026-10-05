#pragma once

#include <mirrorfly/spreadsheet.hpp>

namespace mirrorfly
{
    struct SpreadsheetSaveResult
    {
        SpreadsheetError error = SpreadsheetError::None;
        std::string message;
        std::string path;
        std::string revision;
    };

    SpreadsheetResult load_spreadsheet_file(const std::string& path);
    // expected_revision="missing" requires a new destination.
    SpreadsheetSaveResult save_spreadsheet_file(
        const std::string& path, const std::vector<OfficePart>& parts, const std::string& expected_revision);
}
