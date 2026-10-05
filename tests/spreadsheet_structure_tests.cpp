#include <mirrorfly/spreadsheet.hpp>

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
    bool axis(SpreadsheetDocument& document, SpreadsheetAxisCommand command)
    {
        const auto result = apply_spreadsheet_axis_command(document, command);
        expect(result.changed && result.error == SpreadsheetError::None, result.message.c_str());
        return result.changed;
    }
}

int run_spreadsheet_structure_tests()
{
    {
        auto book = make_spreadsheet();
        apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Add, 1, "目标"});
        apply_spreadsheet_edits(book,
            {{0, {0, 0}, {SpreadsheetValueKind::Number, "10"}},
                {0, {0, 1}, {SpreadsheetValueKind::Number, "20"}},
                {0, {0, 2}, {SpreadsheetValueKind::Formula, "A1+$B$1+F2"}},
                {0, {1, 5}, {SpreadsheetValueKind::Number, "2"}},
                {1, {0, 0}, {SpreadsheetValueKind::Formula, "'工作表1'!A1"}}});
        apply_spreadsheet_formats(book, {{0, {0, 0}, {{"fill", "#123456"}}}});
        auto moved = apply_spreadsheet_move(book, {0, {{0, 0}, {0, 2}}, 0, {1, 1}});
        expect(moved.changed && spreadsheet_source_value(book, 0, {1, 3}).text == "B2+$C$2+F2" &&
                spreadsheet_source_value(book, 0, {0, 0}).kind == SpreadsheetValueKind::Empty &&
                spreadsheet_cell(book, 1, {0, 0}).value.text == "10",
            "move follows absolute, relative and external references");
        moved = apply_spreadsheet_move(book, {0, {{1, 1}, {1, 3}}, 0, {1, 2}});
        expect(moved.changed && spreadsheet_source_value(book, 0, {1, 4}).text == "C2+$D$2+F2" &&
                spreadsheet_source_value(book, 0, {1, 1}).kind == SpreadsheetValueKind::Empty,
            "overlapping move buffers source cells before replacing destination");
        moved = apply_spreadsheet_move(book, {0, {{1, 2}, {1, 4}}, 1, {3, 0}});
        expect(moved.changed && spreadsheet_source_value(book, 1, {3, 2}).text == "A4+$B$4+'工作表1'!F2" &&
                spreadsheet_cell(book, 1, {3, 2}).value.text == "32" &&
                spreadsheet_cell(book, 1, {0, 0}).value.text == "10" &&
                spreadsheet_cell_format(book, 1, {3, 0}).at("fill") == "#123456",
            "cross-sheet move preserves external source targets and direct formatting");
        const auto before = book.original_parts;
        expect(apply_spreadsheet_move(book, {1, {{3, 0}, {3, 2}}, 0, {1048575, 16383}}).error !=
                    SpreadsheetError::None &&
                book.original_parts == before,
            "invalid destination leaves source intact");
        const auto empty_tail = apply_spreadsheet_move(book, {0, {{50, 0}, {51, 0}}, 1, {99, 25}});
        expect(empty_tail.changed && book.sheets[1].rows >= 101,
            "moving blank tail cells extends the target grid");
    }
    {
        auto book = make_spreadsheet();
        apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Add, 1, "目标"});
        apply_spreadsheet_edits(book,
            {{0, {0, 0}, {SpreadsheetValueKind::Text, "名称"}},
                {0, {0, 1}, {SpreadsheetValueKind::Text, "数量"}},
                {0, {1, 0}, {SpreadsheetValueKind::Text, "甲"}},
                {0, {1, 1}, {SpreadsheetValueKind::Number, "5"}},
                {0, {2, 0}, {SpreadsheetValueKind::Text, "乙"}},
                {0, {2, 1}, {SpreadsheetValueKind::Number, "10"}}});
        auto features = spreadsheet_features(book, 0);
        SpreadsheetTable table;
        table.name = "Moving";
        table.range = {{0, 0}, {2, 1}};
        table.style = {{"headerRow", {{"fill", "#123456"}}}};
        features.tables.push_back(table);
        features.merges.push_back({{0, 3}, {0, 4}});
        features.filter = table.range;
        features.filters.push_back({1, "greaterThan", {"6"}});
        features.conditions.push_back({{{1, 1}, {2, 1}}, "greaterThan", 6, "#ABCDEF"});
        expect(apply_spreadsheet_features(book, 0, features).changed, "move region fixture");
        const auto before = serialize_spreadsheet(book);
        expect(apply_spreadsheet_move(book, {0, {{0, 0}, {0, 1}}, 1, {0, 0}}).error ==
                SpreadsheetError::ReadOnly,
            "partial table header cut is rejected");
        expect(serialize_spreadsheet(book).parts.size() == before.parts.size(),
            "failed move leaves package structure intact");
        const auto moved = apply_spreadsheet_move(book, {0, {{0, 0}, {2, 4}}, 1, {3, 6}});
        expect(moved.changed, moved.message.c_str());
        if (!moved.changed)
            return 1;
        const auto& destination = spreadsheet_features(book, 1);
        expect(spreadsheet_features(book, 0).tables.empty() && spreadsheet_features(book, 0).merges.empty() &&
                spreadsheet_features(book, 0).conditions.empty() && !spreadsheet_features(book, 0).filter &&
                spreadsheet_row_visible(book, 0, 1),
            "moving full regions removes only their original metadata");
        expect(destination.tables.size() == 1 && destination.tables[0].range.first.row == 3 &&
                destination.tables[0].range.first.column == 6 && destination.filter &&
                destination.filter->first.row == 3 && destination.merges[0].first.column == 9 &&
                destination.conditions[0].range.first.column == 7 && !spreadsheet_row_visible(book, 1, 4) &&
                spreadsheet_row_visible(book, 1, 5),
            "full-table cut moves relationships, filter, condition and merge together");
        const auto saved = serialize_spreadsheet(book);
        const auto reopened = parse_spreadsheet(saved.parts);
        expect(reopened.error == SpreadsheetError::None &&
                spreadsheet_features(reopened.document, 1).tables.size() == 1 &&
                spreadsheet_cell(reopened.document, 1, {5, 7}).value.text == "10",
            "moved table saves and reopens");
    }
    {
        auto book = make_spreadsheet();
        const auto original_name = book.sheets[0].name;
        apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Add, 1, "引用"});
        apply_spreadsheet_edits(book,
            {{0, {0, 0}, {SpreadsheetValueKind::Text, "数值"}},
                {0, {1, 0}, {SpreadsheetValueKind::Number, "8"}},
                {0, {1, 2}, {SpreadsheetValueKind::Formula, "'" + original_name + "'!A2+A2"}},
                {1, {0, 0}, {SpreadsheetValueKind::Formula, "'" + original_name + "'!A2"}}});
        auto settings = spreadsheet_features(book, 0);
        SpreadsheetTable table;
        table.name = "Original";
        table.range = {{0, 0}, {3, 0}};
        table.style = {{"headerRow", {{"fill", "#123456"}}}};
        settings.tables.push_back(table);
        const auto created = apply_spreadsheet_features(book, 0, settings);
        expect(created.changed, created.message.c_str());
        if (!created.changed)
            return 1;
        const auto copied = apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Copy, 0, "副本"});
        expect(copied.changed && book.sheets.size() == 3, copied.message.c_str());
        if (copied.error != SpreadsheetError::None)
            return 1;
        expect(spreadsheet_features(book, 0).tables.size() == 1 &&
                spreadsheet_features(book, 1).tables.size() == 1 &&
                spreadsheet_features(book, 0).tables[0].path !=
                    spreadsheet_features(book, 1).tables[0].path &&
                spreadsheet_features(book, 0).tables[0].name != spreadsheet_features(book, 1).tables[0].name,
            "copied worksheet has independent table identity and part");
        expect(spreadsheet_source_value(book, 1, {1, 2}).text == "'副本'!A2+A2" &&
                spreadsheet_cell(book, 2, {0, 0}).value.text == "8",
            "copy follows self references only on copied sheet");
        apply_spreadsheet_edit(book, {1, {1, 0}, {SpreadsheetValueKind::Number, "10"}});
        expect(spreadsheet_cell(book, 1, {1, 2}).value.text == "20" &&
                spreadsheet_cell(book, 0, {1, 2}).value.text == "16",
            "copy data is independent");
        expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Move, 1, {}, 2}).changed &&
                book.sheets[2].name == "副本" && spreadsheet_cell(book, 1, {0, 0}).value.text == "8",
            "move reorders sheets and keeps references valid");
        expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Hide, 0}).changed &&
                book.sheets[0].hidden,
            "hide preserves worksheet data");
        expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Hide, 1}).changed &&
                apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Hide, 2}).error ==
                    SpreadsheetError::ReadOnly,
            "cannot hide final visible worksheet");
        expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Show, 1}).changed &&
                !book.sheets[1].hidden,
            "show restores hidden worksheet");
        expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Delete, 0}).changed &&
                book.sheets.size() == 2 && spreadsheet_cell(book, 0, {0, 0}).value.text == "#REF!" &&
                spreadsheet_cell(book, 1, {1, 2}).value.text == "20",
            "delete removes owned objects and adjusts only affected references");
        const auto saved = serialize_spreadsheet(book);
        const auto reopened = parse_spreadsheet(saved.parts);
        expect(reopened.error == SpreadsheetError::None && reopened.document.sheets.size() == 2 &&
                spreadsheet_features(reopened.document, 1).tables.size() == 1,
            "worksheet structure roundtrip retains copy tables");
    }
    auto document = make_spreadsheet();
    const auto name = document.sheets[0].name;
    apply_spreadsheet_sheet_command(document, {SpreadsheetSheetAction::Add, 1, "引用"});
    apply_spreadsheet_edits(document,
        {{0, {0, 0}, {SpreadsheetValueKind::Text, "值"}}, {0, {0, 1}, {SpreadsheetValueKind::Text, "状态"}},
            {0, {1, 0}, {SpreadsheetValueKind::Number, "10"}},
            {0, {2, 0}, {SpreadsheetValueKind::Number, "20"}},
            {0, {3, 0}, {SpreadsheetValueKind::Number, "30"}},
            {0, {4, 0}, {SpreadsheetValueKind::Number, "40"}},
            {0, {2, 2}, {SpreadsheetValueKind::Formula, "A2+$A$3"}},
            {1, {0, 0}, {SpreadsheetValueKind::Formula, "SUM('" + name + "'!A2:A5)"}}});
    apply_spreadsheet_formats(document, {{0, {2, 0}, {{"fill", "#FF1234"}}}});
    apply_spreadsheet_dimensions(document, {{0, false, 2, 45}, {0, true, 1, 30}});
    auto features = spreadsheet_features(document, 0);
    features.merges.push_back({{1, 3}, {2, 4}});
    features.filter = SpreadsheetRange{{0, 0}, {4, 1}};
    features.filters = {{0, "greaterThan", {"15"}}};
    features.conditions.push_back({{{1, 0}, {4, 0}}, "greaterThan", 25, "#ABCDEF"});
    features.frozen_rows = 1;
    features.hidden_rows.insert(4);
    expect(apply_spreadsheet_features(document, 0, features).changed, "structure settings fixture");
    if (!axis(document, {0, false, true, 2, 2}))
        return 1;
    expect(spreadsheet_source_value(document, 0, {4, 0}).text == "20" &&
            spreadsheet_source_value(document, 0, {2, 0}).kind == SpreadsheetValueKind::Empty &&
            spreadsheet_cell_format(document, 0, {4, 0}).at("fill") == "#FF1234" &&
            spreadsheet_dimension(document, 0, false, 4) == 45,
        "inserted rows shift contents, direct styles and row heights together");
    expect(spreadsheet_source_value(document, 0, {4, 2}).text == "A2+$A$5" &&
            spreadsheet_cell(document, 0, {4, 2}).value.text == "30" &&
            spreadsheet_cell(document, 1, {0, 0}).value.text == "100",
        "all worksheets recompute with adjusted absolute and relative references");
    features = spreadsheet_features(document, 0);
    expect(features.filter->last.row == 6 && features.conditions.front().range.last.row == 6 &&
            features.merges.front().last.row == 4 && features.hidden_rows.count(6) &&
            !spreadsheet_row_visible(document, 0, 1) && spreadsheet_row_visible(document, 0, 4),
        "row insertion adjusts filter, condition, merge and manual hidden range");
    auto saved = serialize_spreadsheet(document);
    expect(saved.error == SpreadsheetError::None, "shifted workbook saves");
    auto restored = parse_spreadsheet(saved.parts).document;
    expect(!spreadsheet_row_visible(restored, 0, 1) && spreadsheet_row_visible(restored, 0, 4),
        "filter visibility is persisted after structural recalculation");
    if (!axis(document, {0, true, false, 0, 1}))
        return 1;
    expect(spreadsheet_cell(document, 0, {4, 1}).value.text == "#REF!" &&
            spreadsheet_cell(document, 1, {0, 0}).value.text == "#REF!",
        "deleting referenced column invalidates removed references");
    expect(spreadsheet_dimension(document, 0, true, 0) == 30 && spreadsheet_row_visible(document, 0, 1) &&
            !spreadsheet_row_visible(document, 0, 6),
        "deleted filter predicate does not leave phantom hidden rows");
    auto table_book = make_spreadsheet();
    apply_spreadsheet_edits(table_book,
        {{0, {0, 0}, {SpreadsheetValueKind::Text, "名称"}}, {0, {0, 1}, {SpreadsheetValueKind::Text, "金额"}},
            {0, {1, 0}, {SpreadsheetValueKind::Text, "甲"}},
            {0, {1, 1}, {SpreadsheetValueKind::Number, "5"}}});
    SpreadsheetTable table;
    table.name = "Items";
    table.range = {{0, 0}, {3, 1}};
    table.style = {
        {"headerRow", {{"fill", "#123456"}, {"bold", "1"}}}, {"firstRowStripe", {{"fill", "#ABCDEF"}}}};
    features = spreadsheet_features(table_book, 0);
    features.tables.push_back(table);
    expect(apply_spreadsheet_features(table_book, 0, features).changed, "table structure fixture");
    if (!axis(table_book, {0, true, true, 1, 2}))
        return 1;
    expect(spreadsheet_features(table_book, 0).tables_supported &&
            spreadsheet_features(table_book, 0).tables.front().range.last.column == 3 &&
            spreadsheet_source_value(table_book, 0, {0, 1}).text == "列1" &&
            spreadsheet_source_value(table_book, 0, {0, 2}).text == "列2" &&
            spreadsheet_source_value(table_book, 0, {0, 3}).text == "金额" &&
            spreadsheet_source_value(table_book, 0, {1, 3}).text == "5",
        "inserted table columns get unique headers and preserve existing columns");
    if (!axis(table_book, {0, false, true, 2, 2}) || !axis(table_book, {0, true, false, 1, 1}))
        return 1;
    const auto before = table_book.original_parts;
    expect(
        apply_spreadsheet_axis_command(table_book, {0, false, false, 0, 1}).error != SpreadsheetError::None &&
            table_book.original_parts == before,
        "invalid table header deletion is atomic");
    if (!axis(table_book, {0, false, false, 0, 6}))
        return 1;
    expect(spreadsheet_features(table_book, 0).tables.empty(), "deleting entire table removes its metadata");
    auto boundary = make_spreadsheet();
    apply_spreadsheet_edit(boundary, {0, {1048575, 0}, {SpreadsheetValueKind::Text, "keep"}});
    expect(apply_spreadsheet_axis_command(boundary, {0, false, true, 0, 1}).error != SpreadsheetError::None &&
            spreadsheet_source_value(boundary, 0, {1048575, 0}).text == "keep",
        "overflow keeps last-row data");
    auto unknown = serialize_spreadsheet(make_spreadsheet()).parts;
    for (auto& item : unknown)
        if (item.path == "xl/worksheets/sheet1.xml")
            item.bytes = "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
                         "<sheetData><row r=\"1\"><c "
                         "r=\"A1\"><f>UNSUPPORTED(A2)</f><v>9</v></c></row></sheetData></worksheet>";
    auto protected_book = parse_spreadsheet(unknown).document;
    const auto protected_source = protected_book.original_parts;
    expect(apply_spreadsheet_axis_command(protected_book, {0, false, true, 0, 1}).error ==
                SpreadsheetError::ReadOnly &&
            protected_book.original_parts == protected_source,
        "unknown formula prevents partial structural edits");
    return failures ? 1 : 0;
}

int main()
{
    return run_spreadsheet_structure_tests();
}
