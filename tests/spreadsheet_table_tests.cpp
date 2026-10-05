#include <mirrorfly/spreadsheet.hpp>

#include <algorithm>
#include <iostream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;
    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
    SpreadsheetTable make_table()
    {
        SpreadsheetTable table;
        table.name = "Sales";
        table.range = {{0, 0}, {3, 2}};
        table.first_column = true;
        table.style = {{"wholeTable", {{"fill", "#FFFFFF"}, {"text", "#123456"}}},
            {"headerRow", {{"fill", "#234567"}, {"text", "#FFFFFF"}, {"bold", "1"}}},
            {"firstRowStripe", {{"fill", "#ABCDEF"}}}, {"firstColumnStripe", {{"fill", "#FEDCBA"}}},
            {"firstColumn", {{"bold", "1"}}}, {"lastColumn", {{"bold", "1"}}}};
        return table;
    }
    SpreadsheetDocument make_book()
    {
        auto document = make_spreadsheet();
        expect(apply_spreadsheet_edits(document,
                   {{0, {0, 0}, {SpreadsheetValueKind::Text, "城市"}},
                       {0, {0, 1}, {SpreadsheetValueKind::Text, "数量"}},
                       {0, {0, 2}, {SpreadsheetValueKind::Text, "状态"}},
                       {0, {1, 0}, {SpreadsheetValueKind::Text, "苏州"}},
                       {0, {1, 1}, {SpreadsheetValueKind::Number, "5"}},
                       {0, {2, 0}, {SpreadsheetValueKind::Text, "杭州"}},
                       {0, {2, 1}, {SpreadsheetValueKind::Number, "10"}}})
                   .changed,
            "table source cells commit");
        return document;
    }
    SpreadsheetResult round_trip(const SpreadsheetDocument& document)
    {
        auto output = serialize_spreadsheet(document);
        expect(output.error == SpreadsheetError::None, output.message.c_str());
        auto result = parse_spreadsheet(output.parts);
        expect(result.error == SpreadsheetError::None, result.message.c_str());
        return result;
    }
}

int run_spreadsheet_table_tests()
{
    auto document = make_book();
    auto features = spreadsheet_features(document, 0);
    features.tables.push_back(make_table());
    expect(apply_spreadsheet_features(document, 0, features).changed, "create table");
    expect(spreadsheet_cell_format(document, 0, {0, 1}).at("bold") == "1" &&
            spreadsheet_cell_format(document, 0, {0, 1}).at("text") == "#FFFFFF" &&
            spreadsheet_cell_format(document, 0, {1, 1}).at("fill") == "#ABCDEF" &&
            spreadsheet_cell_format(document, 0, {2, 1}).at("fill") == "#FFFFFF" &&
            spreadsheet_cell_format(document, 0, {2, 0}).at("bold") == "1",
        "table header, bands and first column render dynamically");
    apply_spreadsheet_formats(document,
        {{0, {1, 1}, {{"fill", "#778899"}}}, {0, {0, 1}, {{"size", "17"}}},
            {0, {0, 2}, {{"bold", "0"}, {"text", "#CC1122"}}}});
    features.filter = features.tables.front().range;
    features.filters = {{1, "greaterThan", {"6"}}};
    expect(apply_spreadsheet_features(document, 0, features).changed, "filter a whole table");
    auto imported = round_trip(document);
    auto& restored = imported.document;
    const auto saved = spreadsheet_features(restored, 0);
    expect(saved.tables_supported && saved.tables.size() == 1 && saved.tables.front().name == "Sales" &&
            !saved.tables.front().path.empty() && saved.tables.front().id != 0,
        "table identity and supported custom style round trip");
    for (std::uint32_t row = 0; row < 4; ++row)
        for (std::uint32_t column = 0; column < 3; ++column)
            expect(spreadsheet_cell_format(document, 0, {row, column}) ==
                    spreadsheet_cell_format(restored, 0, {row, column}),
                "table formatting survives save exactly");
    expect(saved.filter_supported && saved.filters.size() == 1 && !spreadsheet_row_visible(restored, 0, 1) &&
            spreadsheet_row_visible(restored, 0, 2),
        "table filter is read and filtered hidden rows are not manual hiding");
    {
        auto unrelated = restored;
        expect(apply_spreadsheet_edit(unrelated, {0, {8, 0}, {SpreadsheetValueKind::Text, "reviewed"}})
                    .changed &&
                apply_spreadsheet_edit(unrelated, {0, {2, 1}, {SpreadsheetValueKind::Number, "12"}}).changed,
            "ordinary edits inside and outside table body commit");
        const auto kept = serialize_spreadsheet(unrelated);
        expect(kept.error == SpreadsheetError::None, "ordinary edits serialize");
        for (const auto& source : *restored.original_parts)
            if (source.path == saved.tables.front().path ||
                source.path == "xl/worksheets/_rels/sheet1.xml.rels")
                expect(std::any_of(kept.parts.begin(), kept.parts.end(),
                           [&](const auto& result)
                {
                    return source.path == result.path && source.bytes == result.bytes;
                }),
                    "ordinary cell edits preserve table identity, original styles and relationships");
    }
    expect(apply_spreadsheet_edit(restored, {0, {0, 1}, {SpreadsheetValueKind::Text, "金额"}}).changed,
        "supported header rename commits");
    expect(apply_spreadsheet_edits(restored,
               {{0, {0, 1}, {SpreadsheetValueKind::Text, "城市"}},
                   {0, {1, 1}, {SpreadsheetValueKind::Number, "99"}}})
                    .error == SpreadsheetError::InvalidValue &&
            spreadsheet_source_value(restored, 0, {1, 1}).text == "5",
        "duplicate header aborts entire batch");
    expect(apply_spreadsheet_edit(restored, {0, {0, 0}, {SpreadsheetValueKind::Empty, {}}}).error ==
            SpreadsheetError::InvalidValue,
        "cannot clear table header");
    expect(apply_spreadsheet_edits(restored,
               {{0, {0, 0}, {SpreadsheetValueKind::Text, "金额"}},
                   {0, {0, 1}, {SpreadsheetValueKind::Text, "城市"}}})
               .changed,
        "header swap validates final batch");
    restored = round_trip(restored).document;
    features = spreadsheet_features(restored, 0);
    features.filters.clear();
    features.tables.front().row_stripes = false;
    features.tables.front().column_stripes = true;
    features.tables.front().last_column = true;
    expect(apply_spreadsheet_features(restored, 0, features).changed, "table options update");
    expect(spreadsheet_cell_format(restored, 0, {2, 2}).at("fill") == "#FEDCBA" &&
            spreadsheet_cell_format(restored, 0, {2, 2}).at("bold") == "1" &&
            spreadsheet_cell_format(restored, 0, {1, 1}).at("fill") == "#778899",
        "column stripes preserve direct cell fill");
    const auto options = round_trip(restored).document;
    expect(spreadsheet_row_visible(options, 0, 1), "clear table filter reveals previously filtered rows");
    auto invalid = features;
    invalid.tables.push_back(make_table());
    expect(apply_spreadsheet_features(restored, 0, invalid).error == SpreadsheetError::InvalidValue,
        "overlapping tables rejected");
    invalid = features;
    invalid.tables.front().id += 1;
    expect(apply_spreadsheet_features(restored, 0, invalid).error == SpreadsheetError::InvalidValue,
        "import identity cannot be forged");
    invalid = features;
    invalid.merges.push_back({{2, 1}, {2, 2}});
    expect(apply_spreadsheet_features(restored, 0, invalid).error == SpreadsheetError::InvalidValue,
        "merge cannot cross a table");
    invalid = features;
    invalid.filter = SpreadsheetRange{{0, 0}, {2, 1}};
    expect(apply_spreadsheet_features(restored, 0, invalid).error == SpreadsheetError::InvalidValue,
        "partial table filter rejected");
    features.tables.clear();
    expect(apply_spreadsheet_features(restored, 0, features).changed, "remove supported table");
    const auto removed = serialize_spreadsheet(restored);
    expect(removed.error == SpreadsheetError::None &&
            std::none_of(removed.parts.begin(), removed.parts.end(),
                [](const auto& item)
    {
        return item.path.find("xl/tables/") == 0;
    }),
        "removal deletes table part and relationship");
    expect(
        spreadsheet_features(round_trip(restored).document, 0).tables.empty(), "removed table stays absent");
    expect(apply_spreadsheet_features(restored, 0, saved).error == SpreadsheetError::None,
        "restoring original table metadata remains possible");
    auto unsupported_parts = serialize_spreadsheet(document).parts;
    std::string unsupported_path, unsupported_bytes;
    for (auto& item : unsupported_parts)
        if (item.path.find("xl/tables/") == 0)
        {
            const auto at = item.bytes.find("<tableColumns");
            item.bytes.insert(at, "<sortState ref=\"A2:C4\"/>");
            unsupported_path = item.path;
            unsupported_bytes = item.bytes;
        }
    auto unsupported = parse_spreadsheet(unsupported_parts).document;
    expect(!spreadsheet_features(unsupported, 0).tables_supported &&
            !spreadsheet_cell_properties(unsupported, 0, {0, 0}).editable,
        "unknown table structure protects metadata and header");
    expect(
        apply_spreadsheet_edit(unsupported, {0, {8, 0}, {SpreadsheetValueKind::Text, "unrelated"}}).changed,
        "unrelated edits still work");
    const auto preserved = serialize_spreadsheet(unsupported);
    expect(preserved.error == SpreadsheetError::None &&
            std::any_of(preserved.parts.begin(), preserved.parts.end(),
                [&](const auto& item)
    {
        return item.path == unsupported_path && item.bytes == unsupported_bytes;
    }),
        "unsupported table bytes preserved exactly");
    auto bounded = make_book();
    features = spreadsheet_features(bounded, 0);
    features.tables.push_back(make_table());
    features.tables.front().range.last = {1048575, 16383};
    expect(apply_spreadsheet_features(bounded, 0, features).error == SpreadsheetError::InvalidValue,
        "oversized table is rejected before iterating cells");
    auto formula_parts = serialize_spreadsheet(document).parts;
    for (auto& item : formula_parts)
        if (item.path == "xl/worksheets/sheet1.xml")
            item.bytes.insert(item.bytes.find("</sheetData>"),
                "<row r=\"10\"><c r=\"A10\"><f>SUM(Sales[数量])</f><v>15</v></c></row>");
    auto formula_book = parse_spreadsheet(formula_parts).document;
    expect(apply_spreadsheet_edit(formula_book, {0, {0, 1}, {SpreadsheetValueKind::Text, "金额"}}).error ==
            SpreadsheetError::InvalidValue,
        "unknown structured references prevent header rename");
    features = spreadsheet_features(formula_book, 0);
    features.tables.front().row_stripes = false;
    expect(apply_spreadsheet_features(formula_book, 0, features).changed,
        "style changes do not alter unknown formula references");
    features.tables.clear();
    expect(apply_spreadsheet_features(formula_book, 0, features).error == SpreadsheetError::ReadOnly,
        "table removal protects unknown structured references");
    auto dtd_parts = serialize_spreadsheet(document).parts;
    for (auto& item : dtd_parts)
        if (item.path.find("xl/tables/") == 0)
            item.bytes.insert(item.bytes.find("<table "), "<!DOCTYPE table [<!ENTITY x 'value'>]>");
    expect(
        parse_spreadsheet(dtd_parts).error != SpreadsheetError::None, "table XML rejects DTD declarations");
    return failures ? 1 : 0;
}

int main()
{
    return run_spreadsheet_table_tests();
}
