#include <algorithm>
#include <cmath>
#include <iostream>
#include <mirrorfly/spreadsheet.hpp>

namespace
{
    int failures = 0;
    void expect(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
    void theme_colors()
    {
        using namespace mirrorfly;
        auto parts = serialize_spreadsheet(make_spreadsheet()).parts;
        const auto part = [&](const std::string& path) -> OfficePart&
        {
            return *std::find_if(parts.begin(), parts.end(), [&](const auto& value)
            {
                return value.path == path;
            });
        };
        auto& rels = part("xl/_rels/workbook.xml.rels").bytes;
        rels.insert(rels.find("</Relationships>"),
            "<Relationship Id='theme' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme' "
            "Target='custom/palette.xml'/>");
        parts.push_back({"xl/custom/palette.xml",
            "<a:theme "
            "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'><a:themeElements><a:clrScheme>"
            "<a:dk1><a:sysClr val='windowText' lastClr='102030'/></a:dk1><a:lt1><a:srgbClr "
            "val='FEFDFC'/></a:lt1>"
            "<a:dk2><a:srgbClr val='203040'/></a:dk2><a:lt2><a:srgbClr val='EFEDEC'/></a:lt2>"
            "<a:accent1><a:srgbClr val='4F81BD'/></a:accent1></a:clrScheme></a:themeElements></a:theme>"});
        const std::pair<const char*, const char*> cases[]{{"theme='0'", "#FEFDFC"}, {"theme='1'", "#102030"},
            {"theme='2'", "#EFEDEC"}, {"theme='3'", "#203040"}, {"theme='4' tint='0.4'", "#95B3D7"},
            {"theme='4' tint='1'", "#FFFFFF"}, {"theme='4' tint='-1'", "#000000"},
            {"rgb='FF808080' tint='-0.5'", "#404040"}, {"indexed='10'", "#FF0000"},
            {"indexed='64'", "#000000"}, {"indexed='65'", "#FFFFFF"}, {"auto='1'", "#000000"},
            {"theme='-1'", ""}, {"theme='4x'", ""}, {"theme='12'", ""}, {"indexed='99'", ""},
            {"theme='4' tint='nan'", ""}, {"theme='4' tint='1.5'", ""}, {"rgb='GG123456'", ""}};
        std::string fonts, fills, borders, xfs;
        for (std::size_t i = 0; i < std::size(cases); ++i)
        {
            const std::string color = cases[i].first;
            fonts += "<font><color " + color + "/></font>";
            fills += "<fill><patternFill patternType='solid'><fgColor " + color + "/></patternFill></fill>";
            borders += "<border><left style='thin'><color " + color + "/></left></border>";
            const auto id = std::to_string(i);
            xfs += "<xf fontId='" + id + "' fillId='" + id + "' borderId='" + id + "' numFmtId='0'/>";
        }
        part("xl/styles.xml").bytes =
            "<styleSheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'>"
            "<fonts>" +
            fonts + "</fonts><fills>" + fills + "</fills><borders>" + borders + "</borders><cellXfs>" + xfs +
            "</cellXfs></styleSheet>";
        const auto parsed = parse_spreadsheet(parts);
        expect(parsed.error == SpreadsheetError::None && parsed.document.styles.size() == std::size(cases),
            "relationship-based theme and all color variants parse");
        if (parsed.error != SpreadsheetError::None || parsed.document.styles.size() != std::size(cases))
            return;
        for (std::size_t i = 0; i < std::size(cases); ++i)
        {
            const auto& style = parsed.document.styles[i];
            const std::string expected = cases[i].second;
            expect(expected.empty() ? !style.count("text") && !style.count("fill")
                                    : style.at("text") == expected && style.at("fill") == expected &&
                        style.at("borderLeftColor") == expected,
                "font, fill and border resolve theme/indexed/tint colors without guessing malformed values");
        }
        auto edited = parsed.document;
        expect(apply_spreadsheet_edit(edited, {0, {0, 0}, {SpreadsheetValueKind::Text, "Theme preserved"}})
                   .changed,
            "theme workbook accepts an independent content edit");
        const auto saved = serialize_spreadsheet(edited);
        const auto reopened = parse_spreadsheet(saved.parts);
        expect(saved.error == SpreadsheetError::None && reopened.error == SpreadsheetError::None &&
                reopened.document.styles == parsed.document.styles,
            "theme appearance survives editing and serialization");
        for (const auto& original : parts)
            if (original.path == "xl/styles.xml" || original.path == "xl/custom/palette.xml")
                for (const auto& output : saved.parts)
                    if (original.path == output.path)
                        expect(
                            original.bytes == output.bytes, "theme and original style XML are not flattened");
        auto& styles = part("xl/styles.xml").bytes;
        styles.insert(styles.find("</styleSheet>"),
            "<colors><indexedColors><rgbColor rgb='FF123456'/></indexedColors></colors>");
        styles.replace(styles.find("indexed='10'"), 12, "indexed='0'");
        const auto custom = parse_spreadsheet(parts);
        expect(custom.error == SpreadsheetError::None && custom.document.styles[8].at("text") == "#123456",
            "custom indexed palettes override the standard palette");
    }

}

int run_spreadsheet_format_tests()
{
    using namespace mirrorfly;
    theme_colors();
    auto book = make_spreadsheet();
    expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Add, 1, "统计"}).changed,
        "new worksheet is appended");
    expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Rename, 0, "原始数据"}).changed,
        "sheet without references can be renamed");
    expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Add, 2, "统计"}).error !=
            SpreadsheetError::None,
        "duplicate sheet name is rejected");
    expect(apply_spreadsheet_edit(book, {1, {2, 3}, {SpreadsheetValueKind::Text, "新表内容"}}).changed,
        "new sheet accepts values");
    const auto saved_book = serialize_spreadsheet(book);
    expect(saved_book.error == SpreadsheetError::None, saved_book.message.c_str());
    const auto read_book = parse_spreadsheet(saved_book.parts);
    expect(read_book.error == SpreadsheetError::None && read_book.document.sheets.size() == 2 &&
            read_book.document.sheets[0].name == "原始数据" && read_book.document.sheets[1].name == "统计" &&
            spreadsheet_cell(read_book.document, 1, {2, 3}).value.text == "新表内容",
        "sheet names, relationships and new sheet values survive serialization");
    expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::RemoveAdded, 1, {}}).error !=
            SpreadsheetError::None,
        "nonempty sheet cannot be removed by undo helper");
    apply_spreadsheet_edit(book, {1, {2, 3}, {}});
    expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::RemoveAdded, 1, {}}).error !=
            SpreadsheetError::None,
        "materialized sheet cannot use the unsaved-sheet undo helper");
    auto added = make_spreadsheet();
    apply_spreadsheet_sheet_command(added, {SpreadsheetSheetAction::Add, 1, "临时"});
    expect(apply_spreadsheet_sheet_command(added, {SpreadsheetSheetAction::RemoveAdded, 1, {}}).changed &&
            added.sheets.size() == 1,
        "empty added sheet can be removed for undo");
    auto document = make_spreadsheet();
    const SpreadsheetFormat patch{{"font", "Arial"}, {"size", "18"}, {"bold", "1"}, {"fill", "#EAF2EC"},
        {"text", "#123456"}, {"border", "1"}, {"align", "center"}, {"number", "10"}};
    expect(apply_spreadsheet_formats(document, {{0, {1, 1}, patch}}).changed, "style applies");
    expect(!document.caches_stale, "style only leaves formula caches intact");
    expect(apply_spreadsheet_dimensions(document, {{0, true, 1, 24}, {0, false, 1, 48}}).changed,
        "dimensions apply");
    expect(apply_spreadsheet_formats(document,
               {{0, {2, 2},
                   {{"underline", "1"}, {"strike", "1"}, {"valign", "top"}, {"indent", "2"},
                       {"number", "currency"}, {"decimals", "3"}}}})
               .changed,
        "extended cell format applies");
    apply_spreadsheet_edits(document,
        {{0, {1, 0}, {SpreadsheetValueKind::Number, "5"}}, {0, {2, 0}, {SpreadsheetValueKind::Number, "9"}}});
    auto features = spreadsheet_features(document, 0);
    features.merges.push_back({{4, 0}, {4, 2}});
    features.frozen_rows = 1;
    features.frozen_columns = 0;
    features.filter = SpreadsheetRange{{0, 0}, {2, 2}};
    features.filters.push_back({0, "equal", {"5"}});
    features.conditions.push_back({{{1, 0}, {2, 0}}, "greaterThan", 6, "#AABBCC"});
    features.hidden_columns.insert(4);
    expect(apply_spreadsheet_features(document, 0, features).changed, "worksheet features apply atomically");
    expect(spreadsheet_row_visible(document, 0, 1) && !spreadsheet_row_visible(document, 0, 2),
        "filter evaluates current values");
    expect(
        spreadsheet_conditional_fill(document, 0, {2, 0}) == "#AABBCC", "condition evaluates numeric values");
    expect(apply_spreadsheet_edit(document, {0, {4, 1}, {SpreadsheetValueKind::Text, "lost"}}).error ==
            SpreadsheetError::ReadOnly,
        "covered cell write rejected");
    auto invalid = features;
    invalid.merges.push_back({{1, 0}, {2, 0}});
    expect(apply_spreadsheet_features(document, 0, invalid).error != SpreadsheetError::None &&
            spreadsheet_features(document, 0).merges.size() == 1,
        "destructive merge fails without changes");
    invalid = features;
    invalid.frozen_rows = 5;
    invalid.merges[0] = {{4, 0}, {5, 2}};
    expect(apply_spreadsheet_features(document, 0, invalid).error != SpreadsheetError::None,
        "freeze cannot bisect merge");
    const auto saved = serialize_spreadsheet(document);
    expect(saved.error == SpreadsheetError::None, saved.message.c_str());
    auto parsed = parse_spreadsheet(saved.parts);
    expect(parsed.error == SpreadsheetError::None, parsed.message.c_str());
    const auto& restored_features = spreadsheet_features(parsed.document, 0);
    expect(restored_features.merges.size() == 1 && restored_features.frozen_rows == 1 &&
            restored_features.filter && restored_features.conditions.size() == 1 &&
            restored_features.hidden_columns.count(4),
        "worksheet settings survive round trip");
    const auto extra = spreadsheet_cell_format(parsed.document, 0, {2, 2});
    expect(extra.at("underline") == "1" && extra.at("strike") == "1" && extra.at("valign") == "top" &&
            extra.at("indent") == "2" && extra.at("number") == "currency" && extra.at("decimals") == "3",
        "extended style round trip");
    expect(apply_spreadsheet_formats(parsed.document, {{0, {2, 2}, {{"number", "9"}}}}).changed &&
            !spreadsheet_cell_format(parsed.document, 0, {2, 2}).count("decimals"),
        "switching imported currency to percent clears inherited decimals");
    auto percent = parse_spreadsheet(serialize_spreadsheet(parsed.document).parts);
    expect(spreadsheet_cell_format(parsed.document, 0, {2, 2}) ==
            spreadsheet_cell_format(percent.document, 0, {2, 2}),
        "imported numeric category has identical format before and after saving");
    const auto format = spreadsheet_cell_format(parsed.document, 0, {1, 1});
    for (const auto& [key, value] : patch)
        expect(format.at(key) == value, key.c_str());
    expect(spreadsheet_dimension(parsed.document, 0, true, 1) == 24, "column survives round trip");
    expect(spreadsheet_dimension(parsed.document, 0, false, 1) == 48, "row survives round trip");
    document = make_spreadsheet();
    apply_spreadsheet_formats(document, {{0, {1, 1}, patch}});
    apply_spreadsheet_dimensions(document, {{0, false, 1, 48}});
    const auto before = document.format_edits.at(0).at({1, 1});
    expect(
        apply_spreadsheet_formats(document, {{0, {2, 2}, {{"size", "nan"}}}}).error != SpreadsheetError::None,
        "NaN rejected");
    expect(apply_spreadsheet_formats(document, {{0, {2, 2}, patch}, {0, {2, 2}, patch}}).error !=
            SpreadsheetError::None,
        "duplicates rejected");
    expect(document.format_edits.size() == 1 && document.format_edits.at(0).size() == 1 &&
            before == document.format_edits.at(0).at({1, 1}),
        "failed styles are atomic");
    const auto transaction =
        apply_spreadsheet_transaction(document, {{0, {2, 2}, {SpreadsheetValueKind::Text, "must rollback"}}},
            {{0, {2, 2}, {{"fill", "invalid"}}}}, {});
    expect(transaction.error != SpreadsheetError::None, "combined transaction fails");
    expect(spreadsheet_cell(document, 0, {2, 2}).value.text.empty() && !document.caches_stale,
        "value and stale flag rolled back");
    expect(apply_spreadsheet_formats(document, {{0, {1, 1}, {}}}).changed, "restore original style");
    expect(spreadsheet_cell_format(document, 0, {1, 1}).at("bold") == "0", "restored style effective");
    expect(apply_spreadsheet_dimensions(document, {{0, false, 1, 0}}).changed, "restore original dimension");
    document.format_edits[0][{1, 1}] = {{"font", std::string("bad\0font", 8)}};
    expect(serialize_spreadsheet(document).error != SpreadsheetError::None,
        "serializer rejects invalid public patch");
    document.format_edits.clear();
    {
        auto borders = make_spreadsheet();
        const SpreadsheetFormat edges{{"borderLeft", "none"}, {"borderRight", "double"},
            {"borderRightColor", "#123456"}, {"borderTop", "medium"}, {"borderTopColor", "#654321"},
            {"borderBottom", "dashed"}, {"borderBottomColor", "#CC2211"}};
        expect(apply_spreadsheet_formats(borders, {{0, {0, 0}, edges}}).changed,
            "independent border edges commit");
        const auto roundtrip = parse_spreadsheet(serialize_spreadsheet(borders).parts);
        const auto restored = spreadsheet_cell_format(roundtrip.document, 0, {0, 0});
        for (const auto& [key, value] : edges)
            expect(restored.at(key) == value, "edge style and color survive serialization");
        auto changed = roundtrip.document;
        expect(apply_spreadsheet_formats(
                   changed, {{0, {0, 0}, {{"borderLeft", "thick"}, {"borderLeftColor", "#0055AA"}}}})
                   .changed,
            "one imported edge can be changed");
        const auto next = parse_spreadsheet(serialize_spreadsheet(changed).parts);
        const auto amended = spreadsheet_cell_format(next.document, 0, {0, 0});
        expect(amended.at("borderLeft") == "thick" && amended.at("borderRight") == "double" &&
                amended.at("borderBottomColor") == "#CC2211",
            "unmodified imported edges survive editing another edge");
        expect(apply_spreadsheet_formats(changed, {{0, {0, 0}, {{"borderTop", "invalid"}}}}).error ==
                SpreadsheetError::InvalidValue,
            "invalid border style is rejected atomically");
    }
    {
        auto source = serialize_spreadsheet(make_spreadsheet());
        for (auto& part : source.parts)
            if (part.path == "xl/worksheets/sheet1.xml")
                part.bytes = "<worksheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'>"
                             "<sheetData><row r='1'><c r='A1'><v>10</v></c><c "
                             "r='B1'><f>SUBTOTAL(109,A1:A2)</f><v>30</v></c></row>"
                             "<row r='2'><c r='A2'><v>20</v></c></row></sheetData><mergeCells "
                             "count='1'><mergeCell ref='D1:D5000'/></mergeCells></worksheet>";
        auto imported = parse_spreadsheet(source.parts);
        expect(imported.error == SpreadsheetError::None, "large imported merge fixture");
        const auto baseline = spreadsheet_features(imported.document, 0);
        auto changed = baseline;
        changed.hidden_rows.insert(1);
        expect(apply_spreadsheet_features(imported.document, 0, changed).changed &&
                imported.document.caches_stale,
            "visibility changes invalidate unsupported formulas despite untouched large merge");
        apply_spreadsheet_edit(imported.document, {0, {0, 0}, {SpreadsheetValueKind::Number, "11"}});
        apply_spreadsheet_edit(imported.document, {0, {0, 0}, {SpreadsheetValueKind::Number, "10"}});
        expect(imported.document.edits.empty() && imported.document.caches_stale,
            "value undo does not clear earlier visibility invalidation");
        const auto persisted = serialize_spreadsheet(imported.document);
        bool invalidated = false;
        for (const auto& part : persisted.parts)
            if (part.path == "xl/worksheets/sheet1.xml")
                invalidated = part.bytes.find("<v>30</v>") == std::string::npos;
        expect(persisted.error == SpreadsheetError::None && invalidated,
            "hidden rows remove unsupported formula cache on save");
        changed.merges.clear();
        expect(apply_spreadsheet_features(imported.document, 0, changed).changed &&
                apply_spreadsheet_features(imported.document, 0, baseline).changed &&
                spreadsheet_features(imported.document, 0).merges.size() == 1,
            "large imported merge can be removed and restored atomically");
    }
    {
        auto source = serialize_spreadsheet(make_spreadsheet());
        const auto style_path = make_spreadsheet().styles_path;
        for (auto& part : source.parts)
        {
            if (part.path == "xl/worksheets/sheet1.xml")
                part.bytes = "<worksheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'>"
                             "<sheetData><row r='1'><c "
                             "r='A1'><v>10</v></c></row></sheetData><conditionalFormatting sqref='A1'>"
                             "<cfRule type='cellIs' operator='greaterThan' dxfId='0' "
                             "priority='2'><formula>0</formula></cfRule>"
                             "<cfRule type='cellIs' operator='greaterThan' dxfId='1' "
                             "priority='1'><formula>5</formula></cfRule>"
                             "</conditionalFormatting></worksheet>";
            if (part.path == style_path)
                part.bytes.insert(part.bytes.find("</styleSheet>"),
                    "<dxfs count='2'>"
                    "<dxf><fill><patternFill patternType='solid'><fgColor "
                    "rgb='FFFF0000'/></patternFill></fill></dxf>"
                    "<dxf><fill><patternFill patternType='solid'><fgColor "
                    "rgb='FF00FF00'/></patternFill></fill></dxf></dxfs>");
        }
        auto imported = parse_spreadsheet(source.parts);
        expect(imported.error == SpreadsheetError::None &&
                spreadsheet_conditional_fill(imported.document, 0, {0, 0}) == "#00FF00",
            "conditional rules use priority rather than XML order");
        auto changed = spreadsheet_features(imported.document, 0);
        changed.conditions.push_back({{{1, 0}, {1, 0}}, "equal", 1, "#0000FF"});
        expect(apply_spreadsheet_features(imported.document, 0, changed).changed, "add unrelated rule");
        auto restored = parse_spreadsheet(serialize_spreadsheet(imported.document).parts);
        expect(restored.error == SpreadsheetError::None &&
                spreadsheet_conditional_fill(restored.document, 0, {0, 0}) == "#00FF00",
            "adding a rule retains existing precedence after save");
    }
    {
        auto fitted = make_spreadsheet();
        expect(apply_spreadsheet_formats(
                   fitted, {{0, {0, 0}, {{"size", "24"}, {"shrinkToFit", "1"}, {"wrap", "0"}}}})
                   .changed,
            "shrink-to-fit is an independent format property");
        auto reopened = parse_spreadsheet(serialize_spreadsheet(fitted).parts);
        auto value = spreadsheet_cell_format(reopened.document, 0, {0, 0});
        expect(reopened.error == SpreadsheetError::None && value.at("shrinkToFit") == "1" &&
                value.at("size") == "24",
            "shrink display does not rewrite stored font size");
        expect(apply_spreadsheet_formats(reopened.document, {{0, {0, 0}, {{"shrinkToFit", "2"}}}}).error ==
                SpreadsheetError::InvalidValue,
            "invalid shrink boolean is rejected");
        expect(apply_spreadsheet_formats(reopened.document, {{0, {0, 0}, {{"shrinkToFit", "0"}}}}).changed,
            "imported shrink flag can be disabled");
        const auto cleared = parse_spreadsheet(serialize_spreadsheet(reopened.document).parts);
        expect(spreadsheet_cell_format(cleared.document, 0, {0, 0}).at("shrinkToFit") == "0",
            "disabled shrink persists across another save");
    }
    for (int angle = 0; angle <= 181; ++angle)
    {
        const std::string value = std::to_string(angle == 181 ? 255 : angle);
        auto rotated = make_spreadsheet();
        apply_spreadsheet_edit(rotated, {0, {0, 0}, {SpreadsheetValueKind::Text, "Mixed 文字"}});
        expect(apply_spreadsheet_formats(
                   rotated, {{0, {0, 0}, {{"textRotation", value}, {"size", "20"}, {"fill", "#123456"}}}})
                    .error == SpreadsheetError::None,
            "every standard rotation code is accepted");
        const auto rotated_parsed = parse_spreadsheet(serialize_spreadsheet(rotated).parts);
        const auto rotated_format = spreadsheet_cell_format(rotated_parsed.document, 0, {0, 0});
        expect(rotated_parsed.error == SpreadsheetError::None && rotated_format.at("textRotation") == value &&
                rotated_format.at("size") == "20" && rotated_format.at("fill") == "#123456" &&
                spreadsheet_cell(rotated_parsed.document, 0, {0, 0}).value.text == "Mixed 文字",
            "rotation round-trip preserves text and unrelated style");
    }
    for (const auto* invalid_angle : {"-1", "181", "254", "256", "45.5", "nan", "inf", "45x"})
        expect(apply_spreadsheet_formats(document, {{0, {0, 0}, {{"textRotation", invalid_angle}}}}).error ==
                SpreadsheetError::InvalidValue,
            "invalid rotation cannot become a silent partial edit");
    document.read_only = true;
    expect(apply_spreadsheet_formats(document, {{0, {0, 0}, patch}}).error == SpreadsheetError::ReadOnly,
        "readonly format blocked");
    expect(apply_spreadsheet_dimensions(document, {{0, true, 0, 20}}).error == SpreadsheetError::ReadOnly,
        "readonly dimensions blocked");
    return failures == 0 ? 0 : 1;
}

int main()
{
    return run_spreadsheet_format_tests();
}
