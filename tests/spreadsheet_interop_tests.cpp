#include <mirrorfly/spreadsheet_storage.hpp>

#include <QCoreApplication>
#include <QDir>

#include <iostream>

int run_spreadsheet_interop_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const auto fixture = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY))
                             .filePath(QStringLiteral("tests/fixtures/spreadsheet-openpyxl.xlsx"));
    const auto output = QDir(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY))
                            .filePath(QStringLiteral("spreadsheet-interop-result.xlsx"));
    auto loaded = mirrorfly::load_spreadsheet_file(fixture.toUtf8().toStdString());
    if (loaded.error != mirrorfly::SpreadsheetError::None || loaded.document.sheets.size() != 2)
    {
        std::cerr << "Independent XLSX fixture did not load: " << loaded.message << '\n';
        return 1;
    }
    const auto text = mirrorfly::apply_spreadsheet_edit(
        loaded.document, {0, {1, 0}, {mirrorfly::SpreadsheetValueKind::Text, "Mirrorfly 编辑"}});
    const auto number = mirrorfly::apply_spreadsheet_edit(
        loaded.document, {0, {1, 1}, {mirrorfly::SpreadsheetValueKind::Number, "88.50"}});
    if (text.error != mirrorfly::SpreadsheetError::None || number.error != mirrorfly::SpreadsheetError::None)
    {
        std::cerr << "Independent workbook could not be edited: " << text.message << number.message << '\n';
        return 1;
    }
    const auto styled = mirrorfly::apply_spreadsheet_formats(loaded.document,
        {{0, {1, 0}, {{"bold", "1"}, {"fill", "#EAF2EC"}, {"align", "center"}}},
            {0, {1, 1}, {{"number", "2"}, {"border", "1"}}}});
    const auto sized =
        mirrorfly::apply_spreadsheet_dimensions(loaded.document, {{0, true, 0, 26}, {0, false, 1, 40}});
    if (styled.error != mirrorfly::SpreadsheetError::None || sized.error != mirrorfly::SpreadsheetError::None)
        return 1;
    const auto package = mirrorfly::serialize_spreadsheet(loaded.document);
    if (package.error != mirrorfly::SpreadsheetError::None)
    {
        std::cerr << package.message << '\n';
        return 1;
    }
    const auto saved = mirrorfly::save_spreadsheet_file(output.toUtf8().toStdString(), package.parts, {});
    if (saved.error != mirrorfly::SpreadsheetError::None)
    {
        std::cerr << saved.message << '\n';
        return 1;
    }
    const auto reopened = mirrorfly::load_spreadsheet_file(output.toUtf8().toStdString());
    if (reopened.error != mirrorfly::SpreadsheetError::None || reopened.document.sheets.size() != 2 ||
        mirrorfly::spreadsheet_cell(reopened.document, 0, {1, 0}).value.text != "Mirrorfly 编辑" ||
        mirrorfly::spreadsheet_cell(reopened.document, 0, {1, 1}).value.text != "88.50" ||
        mirrorfly::spreadsheet_cell(reopened.document, 0, {1, 2}).value.text != "001234" ||
        !mirrorfly::spreadsheet_cell(reopened.document, 0, {4, 3}).editable ||
        mirrorfly::spreadsheet_cell(reopened.document, 0, {4, 4}).editable)
    {
        std::cerr << "Independent workbook roundtrip failed\n";
        return 1;
    }
    auto extended = mirrorfly::make_spreadsheet();
    mirrorfly::apply_spreadsheet_edits(extended,
        {{0, {0, 0}, {mirrorfly::SpreadsheetValueKind::Text, "值"}},
            {0, {1, 0}, {mirrorfly::SpreadsheetValueKind::Number, "5"}},
            {0, {2, 0}, {mirrorfly::SpreadsheetValueKind::Number, "9"}},
            {0, {0, 1}, {mirrorfly::SpreadsheetValueKind::Text, "名称"}},
            {0, {1, 1}, {mirrorfly::SpreadsheetValueKind::Text, "Apple*?"}},
            {0, {2, 1}, {mirrorfly::SpreadsheetValueKind::Text, "Pear"}}});
    mirrorfly::apply_spreadsheet_formats(extended,
        {{0, {1, 0},
             {{"number", "currency"}, {"decimals", "3"}, {"underline", "1"}, {"strike", "1"},
                 {"valign", "top"}, {"align", "distributed"}, {"indent", "2"}, {"borderRight", "double"},
                 {"borderRightColor", "#123456"}, {"borderBottom", "dashed"},
                 {"borderBottomColor", "#CC2211"}}},
            {0, {2, 1}, {{"align", "justify"}, {"wrap", "1"}}}});
    auto features = mirrorfly::spreadsheet_features(extended, 0);
    features.merges.push_back({{4, 0}, {4, 2}});
    features.frozen_rows = 1;
    features.filter = mirrorfly::SpreadsheetRange{{0, 0}, {2, 1}};
    features.filters.push_back({0, "equal", {"5"}});
    features.filters.push_back({1, "contains", {"*?"}});
    features.conditions.push_back({{{1, 0}, {2, 0}}, "greaterThan", 6, "#AABBCC"});
    features.hidden_columns.insert(4);
    if (mirrorfly::apply_spreadsheet_features(extended, 0, features).error !=
        mirrorfly::SpreadsheetError::None)
        return 1;
    auto extended_package = mirrorfly::serialize_spreadsheet(extended);
    if (extended_package.error != mirrorfly::SpreadsheetError::None ||
        mirrorfly::save_spreadsheet_file(QDir(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY))
                                             .filePath("spreadsheet-start-tools.xlsx")
                                             .toStdString(),
            extended_package.parts, {})
                .error != mirrorfly::SpreadsheetError::None)
        return 1;
    mirrorfly::SpreadsheetTable table;
    table.name = "ProgressTable";
    table.range = {{0, 0}, {2, 1}};
    table.first_column = true;
    table.style = {{"wholeTable", {{"fill", "#FFFFFF"}, {"text", "#24352B"}}},
        {"headerRow", {{"bold", "1"}, {"fill", "#35644C"}, {"text", "#FFFFFF"}}},
        {"firstRowStripe", {{"fill", "#EDF4EF"}}}, {"firstColumn", {{"bold", "1"}}}};
    features.tables.push_back(table);
    if (mirrorfly::apply_spreadsheet_features(extended, 0, features).error !=
            mirrorfly::SpreadsheetError::None ||
        !mirrorfly::apply_spreadsheet_edit(
            extended, {0, {0, 0}, {mirrorfly::SpreadsheetValueKind::Text, "数量"}})
            .changed)
        return 1;
    const auto table_package = mirrorfly::serialize_spreadsheet(extended);
    if (table_package.error != mirrorfly::SpreadsheetError::None ||
        mirrorfly::save_spreadsheet_file(QDir(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY))
                                             .filePath("spreadsheet-tables.xlsx")
                                             .toStdString(),
            table_package.parts, {})
                .error != mirrorfly::SpreadsheetError::None)
        return 1;
    std::cout << "Independent workbook edited; output is available for an external reader.\n";
    using namespace mirrorfly;
    if (!apply_spreadsheet_axis_command(extended, {0, false, true, 2, 1}).changed ||
        !apply_spreadsheet_axis_command(extended, {0, true, true, 1, 1}).changed ||
        !apply_spreadsheet_sheet_command(extended, {SpreadsheetSheetAction::Add, 1, "汇总"}).changed ||
        !apply_spreadsheet_edit(
            extended, {1, {0, 0}, {SpreadsheetValueKind::Formula, "SUM('工作表1'!A2:A4)"}})
            .changed)
        return 1;
    auto named_parts = serialize_spreadsheet(extended).parts;
    for (auto& item : named_parts)
        if (item.path == extended.workbook_path)
        {
            const auto at = item.bytes.find("</sheets>");
            if (at == std::string::npos)
                return 1;
            item.bytes.insert(at + 9,
                "<definedNames><definedName name=\"Amount\">'工作表1'!$A$2:$A$4</definedName>"
                "<definedName name=\"LocalAmount\" "
                "localSheetId=\"0\">'工作表1'!$A$2</definedName></definedNames>");
        }
    auto named = parse_spreadsheet(std::move(named_parts));
    if (named.error != SpreadsheetError::None)
        return 1;
    extended = std::move(named.document);
    if (!apply_spreadsheet_sheet_command(extended, {SpreadsheetSheetAction::Rename, 0, "数据"}).changed ||
        !apply_spreadsheet_sheet_command(extended, {SpreadsheetSheetAction::Copy, 0, "副本"}).changed ||
        !apply_spreadsheet_sheet_command(extended, {SpreadsheetSheetAction::Move, 0, {}, 2}).changed ||
        !apply_spreadsheet_sheet_command(extended, {SpreadsheetSheetAction::Hide, 2}).changed)
        return 1;
    const auto structure_package = serialize_spreadsheet(extended);
    if (structure_package.error != SpreadsheetError::None ||
        save_spreadsheet_file(QDir(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY))
                                  .filePath("spreadsheet-structure.xlsx")
                                  .toStdString(),
            structure_package.parts, {})
                .error != SpreadsheetError::None)
        return 1;
    const auto moved = apply_spreadsheet_move(extended, {0, {{0, 0}, {5, 3}}, 1, {2, 4}});
    if (!moved.changed)
    {
        std::cerr << moved.message << '\n';
        return 1;
    }
    const auto moved_package = serialize_spreadsheet(extended);
    if (moved_package.error != SpreadsheetError::None ||
        save_spreadsheet_file(QDir(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY))
                                  .filePath("spreadsheet-moved.xlsx")
                                  .toStdString(),
            moved_package.parts, {})
                .error != SpreadsheetError::None)
        return 1;
    return 0;
}

int main(int argc, char* argv[])
{
    return run_spreadsheet_interop_tests(argc, argv);
}
