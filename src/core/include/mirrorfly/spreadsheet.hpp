#pragma once

#include <mirrorfly/office_package.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mirrorfly
{
    constexpr std::size_t maximum_spreadsheet_archive_bytes = 64 * 1024 * 1024;
    constexpr std::size_t maximum_spreadsheet_expanded_bytes = 128 * 1024 * 1024;
    constexpr std::size_t maximum_spreadsheet_part_bytes = 24 * 1024 * 1024;
    constexpr std::size_t maximum_spreadsheet_xml_bytes = 8 * 1024 * 1024;
    constexpr std::size_t maximum_spreadsheet_parts = 2048;
    constexpr std::size_t maximum_spreadsheet_sheets = 64;
    constexpr std::size_t maximum_spreadsheet_batch_cells = 4096;
    constexpr std::size_t maximum_spreadsheet_cells = 100000;
    constexpr std::size_t maximum_spreadsheet_text_bytes = 16 * 1024 * 1024;
    constexpr std::size_t maximum_spreadsheet_xml_nodes = 500000;
    constexpr std::size_t maximum_spreadsheet_xml_depth = 64;
    constexpr std::size_t maximum_spreadsheet_path_bytes = 1024;
    constexpr std::uint32_t maximum_spreadsheet_rows = 1048576;
    constexpr std::uint32_t maximum_spreadsheet_columns = 16384;

    enum class SpreadsheetError
    {
        None,
        UnsupportedType,
        ReadFailed,
        TooLarge,
        InvalidArchive,
        EncryptedArchive,
        InvalidPackage,
        InvalidXml,
        MissingPart,
        WriteFailed,
        ChangedOnDisk,
        ReadOnly,
        InvalidValue
    };

    enum class SpreadsheetValueKind
    {
        Empty,
        Text,
        Number,
        Boolean,
        Error,
        // Editing/source value only; spreadsheet_cell returns the calculated value.
        Formula
    };

    struct SpreadsheetAddress
    {
        std::uint32_t row = 0;
        std::uint32_t column = 0;

        bool operator<(const SpreadsheetAddress& other) const
        {
            return row < other.row || (row == other.row && column < other.column);
        }
    };

    struct SpreadsheetValue
    {
        SpreadsheetValueKind kind = SpreadsheetValueKind::Empty;
        std::string text;
    };

    struct SpreadsheetCell
    {
        SpreadsheetValue value;
        std::string formula;
        std::uint32_t style_index = 0;
        bool formula_cell = false;
        bool formula_supported = false;
        bool editable = true;
        std::string read_only_reason;
        bool merged_covered = false;
    };

    struct SpreadsheetRange
    {
        SpreadsheetAddress first;
        SpreadsheetAddress last;
    };

    struct SpreadsheetCondition
    {
        SpreadsheetRange range;
        std::string comparison = "greaterThan";
        double value = 0;
        std::string fill;
        bool stop_if_true = true;
    };

    struct SpreadsheetFilter
    {
        std::uint32_t column = 0;
        std::string comparison = "equal";
        // Equal matches any listed value; columns are combined with AND.
        std::vector<std::string> values;
    };

    // Supported direct format fields; an empty map restores the imported style.
    using SpreadsheetFormat = std::map<std::string, std::string>;

    struct SpreadsheetTable
    {
        SpreadsheetRange range;
        std::string name;
        // Empty for new tables; imported identity is immutable.
        std::string path;
        std::uint32_t id = 0;
        std::map<std::string, SpreadsheetFormat> style;
        bool row_stripes = true;
        bool column_stripes = false;
        bool first_column = false;
        bool last_column = false;
        bool supported = true;
    };

    struct SpreadsheetFeatures
    {
        std::vector<SpreadsheetRange> merges;
        std::vector<SpreadsheetCondition> conditions;
        std::optional<SpreadsheetRange> filter;
        std::vector<SpreadsheetFilter> filters;
        std::vector<SpreadsheetTable> tables;
        std::uint32_t frozen_rows = 0;
        std::uint32_t frozen_columns = 0;
        std::set<std::uint32_t> hidden_rows;
        std::set<std::uint32_t> hidden_columns;
        // Imported unsupported structures remain preserved and cannot be replaced accidentally.
        bool conditions_supported = true;
        bool filter_supported = true;
        bool panes_supported = true;
        bool tables_supported = true;
    };

    struct SpreadsheetDimensionCommand
    {
        std::size_t sheet_index = 0;
        bool column = true;
        std::uint32_t index = 0;
        // Excel character units for columns, points for rows; zero restores the source.
        double size = 0;
    };

    enum class SpreadsheetReferenceAction
    {
        Insert,
        Erase,
        RenameSheet,
        DeleteSheet,
        Move
    };

    struct SpreadsheetReferenceChange
    {
        SpreadsheetReferenceAction action = SpreadsheetReferenceAction::Insert;
        std::string sheet;
        std::string target_sheet;
        bool column = false;
        std::uint32_t index = 0;
        std::uint32_t count = 1;
        SpreadsheetRange range;
        SpreadsheetAddress destination;
    };

    struct SpreadsheetAxisCommand
    {
        std::size_t sheet_index = 0;
        bool column = false;
        bool insert = true;
        std::uint32_t index = 0;
        std::uint32_t count = 1;
    };

    struct SpreadsheetMoveCommand
    {
        std::size_t sheet_index = 0;
        SpreadsheetRange range;
        std::size_t target_sheet = 0;
        SpreadsheetAddress destination;
    };

    struct SpreadsheetFormatCommand
    {
        std::size_t sheet_index = 0;
        SpreadsheetAddress address;
        SpreadsheetFormat format;
    };

    struct SpreadsheetSheet
    {
        std::string name;
        std::string path;
        std::map<SpreadsheetAddress, SpreadsheetCell> cells;
        std::vector<SpreadsheetRange> protected_ranges;
        std::map<std::uint32_t, double> column_widths;
        std::map<std::uint32_t, double> row_heights;
        SpreadsheetFeatures features;
        std::uint32_t rows = 100;
        std::uint32_t columns = 26;
        bool editable = true;
        bool hidden = false;
        std::string read_only_reason;
    };

    struct SpreadsheetDocument
    {
        std::vector<SpreadsheetSheet> sheets;
        std::shared_ptr<const std::vector<OfficePart>> original_parts;
        std::map<std::size_t, std::map<SpreadsheetAddress, SpreadsheetValue>> edits;
        std::vector<SpreadsheetFormat> styles;
        // Properties explicitly supplied by each imported cell style.
        std::vector<std::set<std::string>> style_properties;
        std::string styles_path;
        std::string workbook_path;
        std::map<std::size_t, std::map<SpreadsheetAddress, SpreadsheetFormat>> format_edits;
        std::map<std::size_t, std::map<std::uint32_t, double>> column_edits;
        std::map<std::size_t, std::map<std::uint32_t, double>> row_edits;
        std::map<std::size_t, SpreadsheetFeatures> feature_edits;
        bool read_only = false;
        std::string read_only_reason;
        bool caches_stale = false;
        bool date_1904 = false;
    };

    struct SpreadsheetEditCommand
    {
        std::size_t sheet_index = 0;
        SpreadsheetAddress address;
        SpreadsheetValue value;
    };

    struct SpreadsheetEditResult
    {
        SpreadsheetError error = SpreadsheetError::None;
        std::string message;
        bool changed = false;
    };

    enum class SpreadsheetSheetAction
    {
        Add,
        Rename,
        RemoveAdded,
        Delete,
        Copy,
        Move,
        Hide,
        Show
    };

    struct SpreadsheetSheetCommand
    {
        SpreadsheetSheetAction action = SpreadsheetSheetAction::Add;
        std::size_t index = 0;
        std::string name;
        std::size_t destination = 0;
    };

    struct SpreadsheetResult
    {
        SpreadsheetError error = SpreadsheetError::None;
        std::string message;
        SpreadsheetDocument document;
        std::string path;
        std::string revision;
    };

    struct SpreadsheetPackageResult
    {
        SpreadsheetError error = SpreadsheetError::None;
        std::string message;
        std::vector<OfficePart> parts;
    };

    struct SpreadsheetCalculation
    {
        // A snapshot for one document version; regenerate after edits.
        std::map<std::size_t, std::map<SpreadsheetAddress, SpreadsheetValue>> values;
        std::size_t work = 0;
        bool complete = true;
    };

    bool is_spreadsheet_path(const std::string& path);
    const SpreadsheetFeatures& spreadsheet_features(const SpreadsheetDocument& document, std::size_t sheet);
    SpreadsheetEditResult apply_spreadsheet_features(
        SpreadsheetDocument& document, std::size_t sheet, const SpreadsheetFeatures& features);
    bool spreadsheet_row_visible(const SpreadsheetDocument& document, std::size_t sheet, std::uint32_t row,
        const SpreadsheetCalculation* calculation = nullptr);
    std::string spreadsheet_conditional_fill(const SpreadsheetDocument& document, std::size_t sheet,
        SpreadsheetAddress address, const SpreadsheetCalculation* calculation = nullptr);
    std::optional<SpreadsheetAddress> parse_spreadsheet_address(const std::string& text);
    std::string spreadsheet_address(SpreadsheetAddress address);
    SpreadsheetDocument make_spreadsheet();
    SpreadsheetEditResult apply_spreadsheet_sheet_command(
        SpreadsheetDocument& document, const SpreadsheetSheetCommand& command);
    SpreadsheetEditResult apply_spreadsheet_axis_command(
        SpreadsheetDocument& document, const SpreadsheetAxisCommand& command);
    SpreadsheetEditResult apply_spreadsheet_move(
        SpreadsheetDocument& document, const SpreadsheetMoveCommand& command);
    SpreadsheetResult parse_spreadsheet(std::vector<OfficePart> parts);
    SpreadsheetCell spreadsheet_cell(const SpreadsheetDocument& document, std::size_t sheet_index,
        SpreadsheetAddress address, const SpreadsheetCalculation* calculation = nullptr);
    // Metadata without evaluation; formula values may be old imported caches.
    SpreadsheetCell spreadsheet_cell_properties(
        const SpreadsheetDocument& document, std::size_t sheet_index, SpreadsheetAddress address);
    SpreadsheetValue spreadsheet_source_value(
        const SpreadsheetDocument& document, std::size_t sheet_index, SpreadsheetAddress address);
    bool spreadsheet_formula_supported(const std::string& formula);
    SpreadsheetValue spreadsheet_calculate(
        const SpreadsheetDocument& document, std::size_t sheet_index, SpreadsheetAddress address);
    SpreadsheetCalculation spreadsheet_calculate_all(const SpreadsheetDocument& document);
    std::optional<std::string> spreadsheet_translate_formula(
        const std::string& formula, int row_offset, int column_offset);
    std::optional<std::string> spreadsheet_rewrite_formula(const std::string& formula,
        const std::string& source_sheet, const std::string& result_sheet,
        const SpreadsheetReferenceChange& change);
    SpreadsheetEditResult apply_spreadsheet_edit(
        SpreadsheetDocument& document, const SpreadsheetEditCommand& command);
    SpreadsheetEditResult apply_spreadsheet_edits(
        SpreadsheetDocument& document, const std::vector<SpreadsheetEditCommand>& commands);
    SpreadsheetPackageResult serialize_spreadsheet(const SpreadsheetDocument& document);
    SpreadsheetFormat spreadsheet_cell_format(
        const SpreadsheetDocument& document, std::size_t sheet, SpreadsheetAddress address);
    // A transferable cell format; table style layers are excluded and the target base is reset.
    SpreadsheetFormat spreadsheet_cell_direct_format(
        const SpreadsheetDocument& document, std::size_t sheet, SpreadsheetAddress address);
    SpreadsheetEditResult apply_spreadsheet_formats(
        SpreadsheetDocument& document, const std::vector<SpreadsheetFormatCommand>& commands);
    SpreadsheetEditResult apply_spreadsheet_dimensions(
        SpreadsheetDocument& document, const std::vector<SpreadsheetDimensionCommand>& commands);
    double spreadsheet_dimension(
        const SpreadsheetDocument& document, std::size_t sheet, bool column, std::uint32_t index);
    SpreadsheetEditResult apply_spreadsheet_transaction(SpreadsheetDocument& document,
        const std::vector<SpreadsheetEditCommand>& cells,
        const std::vector<SpreadsheetFormatCommand>& formats,
        const std::vector<SpreadsheetDimensionCommand>& dimensions);
}
