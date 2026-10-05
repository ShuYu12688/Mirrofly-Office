#include <iostream>
#include <mirrorfly/spreadsheet.hpp>

namespace
{
    using namespace mirrorfly;
    int failures = 0;
    void expect(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
    void visibility(const SpreadsheetDocument& document, const std::vector<bool>& expected)
    {
        for (std::size_t row = 0; row < expected.size(); ++row)
            expect(spreadsheet_row_visible(document, 0, static_cast<std::uint32_t>(row)) == expected[row],
                "row visibility agrees with all column conditions");
    }
}

int run_spreadsheet_filter_tests()
{
    auto document = make_spreadsheet();
    apply_spreadsheet_edits(document,
        {{0, {0, 0}, {SpreadsheetValueKind::Text, "城市"}}, {0, {0, 1}, {SpreadsheetValueKind::Text, "数量"}},
            {0, {1, 0}, {SpreadsheetValueKind::Text, "苏州"}},
            {0, {1, 1}, {SpreadsheetValueKind::Number, "5"}},
            {0, {2, 0}, {SpreadsheetValueKind::Text, "苏州"}},
            {0, {2, 1}, {SpreadsheetValueKind::Number, "15"}},
            {0, {3, 0}, {SpreadsheetValueKind::Text, "杭州"}},
            {0, {3, 1}, {SpreadsheetValueKind::Formula, "B2+2"}},
            {0, {4, 1}, {SpreadsheetValueKind::Number, "6"}},
            {0, {5, 0}, {SpreadsheetValueKind::Text, "A*?~b"}},
            {0, {5, 1}, {SpreadsheetValueKind::Number, "8"}}});
    auto features = spreadsheet_features(document, 0);
    features.filter = SpreadsheetRange{{0, 0}, {5, 1}};
    features.filters = {{0, "equal", {"苏州", "杭州", ""}}, {1, "between", {"5", "10"}}};
    features.conditions.push_back({{{1, 1}, {5, 1}}, "greaterThan", 6, "#123456"});
    expect(apply_spreadsheet_features(document, 0, features).changed, "multiple filters commit");
    visibility(document, {true, true, false, true, true, false});
    expect(spreadsheet_conditional_fill(document, 0, {3, 1}) == "#123456",
        "conditional formatting uses calculated values of supported formulas");
    const auto saved = serialize_spreadsheet(document);
    auto reopened = parse_spreadsheet(saved.parts);
    expect(saved.error == SpreadsheetError::None && reopened.error == SpreadsheetError::None &&
            spreadsheet_features(reopened.document, 0).filters.size() == 2,
        "multiple filters including blank values round trip");
    visibility(reopened.document, {true, true, false, true, true, false});
    auto invalid = features;
    invalid.filters.push_back(features.filters.front());
    expect(apply_spreadsheet_features(document, 0, invalid).error == SpreadsheetError::InvalidValue,
        "duplicate column cannot change existing filter");
    invalid = features;
    invalid.filters[1].values = {"10", "5"};
    expect(apply_spreadsheet_features(document, 0, invalid).error == SpreadsheetError::InvalidValue,
        "reversed numeric interval is rejected");
    invalid.filters[1].values = {"nan", "10"};
    expect(apply_spreadsheet_features(document, 0, invalid).error == SpreadsheetError::InvalidValue,
        "nonfinite numeric threshold is rejected");
    features.filters = {{0, "contains", {"*?~"}}};
    expect(apply_spreadsheet_features(document, 0, features).changed, "literal wildcard filter commits");
    reopened = parse_spreadsheet(serialize_spreadsheet(document).parts);
    expect(spreadsheet_features(reopened.document, 0).filter_supported &&
            spreadsheet_features(reopened.document, 0).filters.front().values.front() == "*?~",
        "Excel wildcard escaping restores literal text");
    visibility(reopened.document, {true, false, false, false, false, true});
    for (const auto& op : std::vector<std::string>{"beginsWith", "endsWith", "notEqual", "greaterThan",
             "greaterThanOrEqual", "lessThan", "lessThanOrEqual"})
    {
        const bool text = op == "beginsWith" || op == "endsWith" || op == "notEqual";
        features.filters = {{text ? 0u : 1u, op, {text ? "A*?~b" : "7"}}};
        expect(apply_spreadsheet_features(document, 0, features).changed, "comparison commits");
        reopened = parse_spreadsheet(serialize_spreadsheet(document).parts);
        expect(spreadsheet_features(reopened.document, 0).filter_supported &&
                spreadsheet_features(reopened.document, 0).filters.front().comparison == op,
            "comparison operator survives save");
        for (std::uint32_t row = 0; row < 6; ++row)
            expect(spreadsheet_row_visible(document, 0, row) ==
                    spreadsheet_row_visible(reopened.document, 0, row),
                "visibility is unchanged by save");
    }
    features.filters.clear();
    features.hidden_rows.insert(4);
    expect(
        apply_spreadsheet_features(document, 0, features).changed, "clear conditions preserves filter range");
    reopened = parse_spreadsheet(serialize_spreadsheet(document).parts);
    visibility(reopened.document, {true, true, true, true, false, true});
    for (const auto& unsupported :
        std::vector<std::string>{"<filterColumn colId='0'><colorFilter dxfId='0'/></filterColumn>",
            "<filterColumn colId='0'><customFilters><customFilter val='A?B'/></customFilters></filterColumn>",
            "<filterColumn colId='0'><filters><dateGroupItem year='2026' "
            "dateTimeGrouping='year'/></filters></filterColumn>",
            "<filterColumn colId='0x'><filters><filter val='A'/></filters></filterColumn>"})
    {
        auto parts = serialize_spreadsheet(make_spreadsheet()).parts;
        for (auto& part : parts)
            if (part.path == "xl/worksheets/sheet1.xml")
                part.bytes = "<worksheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'>"
                             "<sheetData/><autoFilter ref='A1:B6'>" +
                    unsupported + "</autoFilter></worksheet>";
        auto imported = parse_spreadsheet(parts);
        expect(imported.error == SpreadsheetError::None &&
                !spreadsheet_features(imported.document, 0).filter_supported,
            "unsupported filter is protected");
        apply_spreadsheet_edit(imported.document, {0, {8, 0}, {SpreadsheetValueKind::Text, "unrelated"}});
        auto restored = serialize_spreadsheet(imported.document);
        bool preserved = false;
        for (const auto& part : restored.parts)
            if (part.path == "xl/worksheets/sheet1.xml")
                preserved = part.bytes.find("autoFilter") != std::string::npos;
        expect(restored.error == SpreadsheetError::None && preserved &&
                !spreadsheet_features(parse_spreadsheet(restored.parts).document, 0).filter_supported,
            "unrelated edit preserves unsupported filter");
    }
    return failures ? 1 : 0;
}

int main()
{
    return run_spreadsheet_filter_tests();
}
