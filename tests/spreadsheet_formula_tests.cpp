#include <mirrorfly/spreadsheet.hpp>

#include <iostream>

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
}

int run_spreadsheet_formula_tests()
{
    using namespace mirrorfly;
    {
        SpreadsheetReferenceChange change;
        change.sheet = "Source";
        change.index = 1;
        change.count = 2;
        expect(spreadsheet_rewrite_formula("A1+$A$2+SUM(A1:A5)", "Source", "Source", change) ==
                "A1+$A$4+SUM(A1:A7)",
            "insertion shifts absolute references and expands ranges");
        change.action = SpreadsheetReferenceAction::Erase;
        expect(spreadsheet_rewrite_formula("A1+$A$2+SUM(A1:A5)+SUM(A2:A3)", "Source", "Source", change) ==
                "A1+#REF!+SUM(A1:A3)+SUM(#REF!)",
            "deletion shrinks ranges and invalidates removed references");
        expect(spreadsheet_rewrite_formula("SUM(A5:A1)", "Source", "Source", change) == "SUM(A3:A1)",
            "reversed range direction is preserved");
        change.action = SpreadsheetReferenceAction::RenameSheet;
        change.target_sheet = "O'Brien 中文";
        expect(spreadsheet_rewrite_formula("Source!$A1+A2", "Elsewhere", "Elsewhere", change) ==
                "'O''Brien 中文'!$A1+A2",
            "rename escapes quoted sheet names");
        expect(spreadsheet_rewrite_formula("A1+Source!B2", "Source", "O'Brien 中文", change) ==
                "A1+'O''Brien 中文'!B2",
            "renaming host keeps unqualified references local");
        change.action = SpreadsheetReferenceAction::DeleteSheet;
        expect(spreadsheet_rewrite_formula("SUM(Source!A1:B5)+A1", "Elsewhere", "Elsewhere", change) ==
                "SUM(#REF!)+A1",
            "removed sheet references become errors");
        change.action = SpreadsheetReferenceAction::Move;
        change.target_sheet = "Target";
        change.range = {{0, 0}, {2, 1}};
        change.destination = {4, 3};
        expect(spreadsheet_rewrite_formula("SUM(A1:B3)+$A$2+C4+SUM(A1:C3)", "Source", "Target", change) ==
                "SUM(D5:E7)+$D$6+'Source'!C4+SUM('Source'!A1:C3)",
            "move follows enclosed references and retains targets outside the cut range");
        expect(spreadsheet_rewrite_formula("SUM(Source!A1:B3)", "Elsewhere", "Elsewhere", change) ==
                "SUM('Target'!D5:E7)",
            "other worksheets follow moved cells");
        expect(!spreadsheet_rewrite_formula("UNSUPPORTED(A1)", "Source", "Source", change),
            "unknown formulas cannot be rewritten silently");
    }
    auto book = make_spreadsheet();
    const auto set = [&](SpreadsheetAddress address, SpreadsheetValueKind kind, const char* text)
    {
        const auto result = apply_spreadsheet_edit(book, {0, address, {kind, text}});
        expect(result.error == SpreadsheetError::None, result.message.c_str());
    };
    const auto result = [&](const char* formula, const char* expected)
    {
        set({0, 2}, SpreadsheetValueKind::Formula, formula);
        expect(spreadsheet_cell(book, 0, {0, 2}).value.text == expected, formula);
    };
    set({0, 0}, SpreadsheetValueKind::Number, "1");
    set({1, 0}, SpreadsheetValueKind::Number, "2");
    set({2, 0}, SpreadsheetValueKind::Number, "3");
    set({3, 0}, SpreadsheetValueKind::Text, "ignored");
    result("SUM(A1:A4)", "6");
    result("AVERAGE(A1:A4)", "2");
    result("COUNT(A1:A4)", "3");
    result("MIN(A1:A4)", "1");
    result("MAX(A1:A4)", "3");
    result("SUM(A3:A1,4)+2*3", "16");
    result("SUM($A$1:$A$3)/COUNT(A1:A4)", "2");
    result("(2+3)^2+50%", "25.5");
    result("1/0", "#DIV/0!");
    result("AVERAGE(B9:B10)", "#DIV/0!");
    result("A4+1", "#VALUE!");
    result("A4", "ignored");
    expect(parse_spreadsheet(serialize_spreadsheet(book).parts).error == SpreadsheetError::None,
        "formula returning text writes string cache type");
    result("B9", "0");
    result("'missing'!A1", "#REF!");
    result("C1", "#REF!");
    result("SUM(A1:A3)", "6");
    set({1, 0}, SpreadsheetValueKind::Number, "5");
    expect(spreadsheet_cell(book, 0, {0, 2}).value.text == "9", "source edit recalculates dependent formula");
    set({0, 1}, SpreadsheetValueKind::Formula, "C1*2");
    expect(spreadsheet_cell(book, 0, {0, 1}).value.text == "18", "formula chain calculates");
    expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Add, 1, "统计"}).changed,
        "append sheet");
    expect(apply_spreadsheet_edit(book, {1, {0, 0}, {SpreadsheetValueKind::Formula, "'Sheet1'!B1"}}).changed,
        "cross sheet formula");
    const auto name = book.sheets[0].name;
    expect(apply_spreadsheet_edit(book, {1, {0, 0}, {SpreadsheetValueKind::Formula, "'" + name + "'!B1"}})
                .error == SpreadsheetError::None,
        "cross sheet reference accepts local name");
    expect(spreadsheet_cell(book, 1, {0, 0}).value.text == "18", "cross sheet calculation");
    expect(apply_spreadsheet_sheet_command(book, {SpreadsheetSheetAction::Rename, 0, "renamed"}).changed &&
            spreadsheet_cell(book, 1, {0, 0}).value.text == "18" &&
            spreadsheet_source_value(book, 1, {0, 0}).text == "'renamed'!B1",
        "rename updates formula references and preserves calculation");
    expect(spreadsheet_translate_formula("SUM(A1:$B$2)+C$4+$D5+'统计'!E6", 2, 1) ==
            "SUM(B3:$B$2)+D$4+$D7+'统计'!F8",
        "fill moves only relative coordinates");
    expect(
        spreadsheet_translate_formula("A1", -1, 0) == "#REF!", "out of bounds copy yields reference error");
    for (const auto* text : {"SUM(", "SUM(A1,,2)", "A0", "SUMIFS(A1)", "[Book1]Sheet1!A1", "1.2.3", "1e999"})
        expect(!spreadsheet_formula_supported(text), text);
    expect(!spreadsheet_formula_supported(std::string(100, '(') + "1" + std::string(100, ')')),
        "formula nesting budget");
    const auto before = spreadsheet_source_value(book, 0, {0, 2});
    expect(apply_spreadsheet_edits(book,
               {{0, {0, 2}, {SpreadsheetValueKind::Number, "12"}},
                   {0, {0, 3}, {SpreadsheetValueKind::Formula, "SUM("}}})
                    .error == SpreadsheetError::InvalidValue &&
            spreadsheet_source_value(book, 0, {0, 2}).text == before.text,
        "invalid formula batch is atomic");
    expect(apply_spreadsheet_transaction(book, {{0, {100, 0}, {SpreadsheetValueKind::Number, "7"}}},
               {{0, {100, 0}, {{"size", "999"}}}}, {})
                    .error != SpreadsheetError::None &&
            book.sheets[0].rows == 100,
        "failed value and style transaction restores visible extent");
    auto package = serialize_spreadsheet(book);
    expect(package.error == SpreadsheetError::None, package.message.c_str());
    auto reopened = parse_spreadsheet(package.parts);
    expect(reopened.error == SpreadsheetError::None, reopened.message.c_str());
    expect(spreadsheet_cell(reopened.document, 0, {0, 2}).value.text == "9" &&
            spreadsheet_source_value(reopened.document, 0, {0, 2}).kind == SpreadsheetValueKind::Formula,
        "formula and calculation survive save reopen");
    bool cache_written = false;
    for (const auto& part : package.parts)
        if (part.path == book.sheets[0].path)
            cache_written = part.bytes.find("<f>SUM(A1:A3)</f><v>9</v>") != std::string::npos;
    expect(cache_written, "interoperable calculation cache is written beside formula");
    apply_spreadsheet_edit(reopened.document, {0, {0, 2}, {SpreadsheetValueKind::Text, "literal"}});
    package = serialize_spreadsheet(reopened.document);
    reopened = parse_spreadsheet(package.parts);
    expect(!spreadsheet_cell(reopened.document, 0, {0, 2}).formula_cell &&
            spreadsheet_cell(reopened.document, 0, {0, 2}).value.text == "literal",
        "replacing formula removes XML formula node");
    set({0, 3}, SpreadsheetValueKind::Formula, "SUM(A1:XFD1048576)");
    expect(spreadsheet_cell(book, 0, {0, 3}).value.text == "#NUM!", "oversized range is bounded");
    {
        auto shared = make_spreadsheet();
        std::vector<SpreadsheetEditCommand> commands{
            {0, {0, 0}, {SpreadsheetValueKind::Formula, "SUM(Z1:Z10000)"}}};
        for (std::uint32_t row = 0; row < 100; ++row)
            commands.push_back({0, {row, 1}, {SpreadsheetValueKind::Formula, "$A$1+1"}});
        expect(apply_spreadsheet_edits(shared, commands).changed, "shared formula dependency fixture");
        auto calculation = spreadsheet_calculate_all(shared);
        expect(calculation.complete && calculation.work < 20000 && calculation.values.at(0).size() == 101 &&
                spreadsheet_cell(shared, 0, {99, 1}, &calculation).value.text == "1",
            "whole-workbook evaluation reuses shared dependencies within one work budget");
        apply_spreadsheet_edit(shared, {0, {0, 25}, {SpreadsheetValueKind::Number, "3"}});
        calculation = spreadsheet_calculate_all(shared);
        expect(calculation.complete && calculation.values.at(0).at({99, 1}).text == "4",
            "new calculation snapshot sees source edits without reusing stale state");
        auto expensive = make_spreadsheet();
        commands.clear();
        for (std::uint32_t row = 0; row < 22; ++row)
            commands.push_back({0, {row, 0}, {SpreadsheetValueKind::Formula, "SUM(Z1:Z100000)"}});
        apply_spreadsheet_edits(expensive, commands);
        calculation = spreadsheet_calculate_all(expensive);
        expect(!calculation.complete && calculation.work <= 2000000,
            "independent expensive formulas share an overall evaluation limit");
        expect(serialize_spreadsheet(expensive).error == SpreadsheetError::TooLarge,
            "save reports budget exhaustion instead of writing incomplete formula caches");
        auto recovery = make_spreadsheet();
        apply_spreadsheet_edits(recovery,
            {{0, {0, 0}, {SpreadsheetValueKind::Formula, "SUM(Z1:Z100000)+SUM(Z1:Z100000)"}},
                {0, {0, 1}, {SpreadsheetValueKind::Formula, "2+3"}}});
        calculation = spreadsheet_calculate_all(recovery);
        expect(calculation.complete && calculation.values.at(0).at({0, 0}).text == "#NUM!" &&
                calculation.values.at(0).at({0, 1}).text == "5",
            "per-formula failure leaves the batch evaluator usable for later cells");
    }
    return failures ? 1 : 0;
}

int main()
{
    return run_spreadsheet_formula_tests();
}
