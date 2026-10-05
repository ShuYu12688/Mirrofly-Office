#pragma once

#include <mirrorfly/spreadsheet.hpp>

namespace mirrorfly
{
    class SpreadsheetColors;
    void read_spreadsheet_tables(SpreadsheetSheet& sheet, const std::vector<std::string>& paths,
        const std::vector<OfficePart>& parts, const std::string& styles_path, std::size_t& xml_nodes,
        const SpreadsheetColors& colors);
    SpreadsheetPackageResult write_spreadsheet_tables(
        const SpreadsheetDocument& document, std::vector<OfficePart> parts);
    std::string spreadsheet_tables_key(const std::vector<SpreadsheetTable>& tables);
    SpreadsheetEditResult validate_spreadsheet_tables(
        const SpreadsheetDocument& document, std::size_t sheet, const SpreadsheetFeatures& features);
    bool valid_spreadsheet_table_headers(const SpreadsheetDocument& document, std::size_t sheet,
        const SpreadsheetTable& table,
        const std::map<std::size_t, std::map<SpreadsheetAddress, SpreadsheetValue>>* edits = nullptr);
    SpreadsheetFormat spreadsheet_table_format(
        const SpreadsheetFeatures& features, SpreadsheetAddress address);
    bool spreadsheet_filter_in_table(const SpreadsheetFeatures& features);
}
