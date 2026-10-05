#include "spreadsheet_bridge.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

void qml_register_types_Mirrorfly_Native();

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    bool wait_for(mirrorfly::SpreadsheetBridge& bridge)
    {
        QElapsedTimer timer;
        timer.start();
        while (bridge.busy() && timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        QCoreApplication::processEvents();
        return check(!bridge.busy(), "spreadsheet operation completes");
    }

    bool call_bool(QObject* object, const char* method)
    {
        QVariant result;
        return QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result)) && result.toBool();
    }

    bool run_cases(QQmlEngine& engine, const QVariantMap& theme, const QDir& source)
    {
        {
            mirrorfly::SpreadsheetBridge rotated;
            rotated.requestOpen(
                QUrl::fromLocalFile(source.filePath("tests/fixtures/spreadsheet-rotation.xlsx")));
            if (!wait_for(rotated))
                return false;
            const auto read_angle = [&](const QString& address)
            {
                return rotated.readContent({{"view", "format"}, {"id", address}})
                    .value("format")
                    .toMap()
                    .value("textRotation")
                    .toString();
            };
            if (!check(read_angle("A2") == "45" && read_angle("A6") == "255" && read_angle("A8") == "150",
                    "independent rotated, stacked and merged formatting reaches public reads"))
                return false;
            rotated.selectAddress("A1");
            if (!check(rotated.formatSelection({{"textRotation", 30}}) && read_angle("A1") == "30",
                    "AI and GUI use the same rotation patch"))
                return false;
            rotated.undo();
            if (!check(read_angle("A1") == "0", "undo restores original direction"))
                return false;
            rotated.redo();
            if (!check(read_angle("A1") == "30", "redo restores rotation"))
                return false;
            const auto invalid = rotated.readContent({{"view", "text"}, {"id", "A1:A5"}});
            if (!check(invalid.value("error") == "invalid_cell_address" &&
                        invalid.value("hint").toString().contains("view=content"),
                    "range misuse receives actionable bounded-read guidance"))
                return false;
            const auto focused = rotated.readContent({{"view", "content"}, {"id", "A8"}});
            const auto items = focused.value("items").toList();
            if (!check(items.size() == 1 && items.first().toMap().value("id") == "A8" &&
                        focused.value("total").toInt() == 1,
                    "content ID narrows the read instead of silently returning unrelated cells"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge fitted;
            fitted.requestOpen(
                QUrl::fromLocalFile(source.filePath("tests/fixtures/spreadsheet-shrink.xlsx")));
            if (!wait_for(fitted))
                return false;
            const auto format = fitted.readContent({{"view", "format"}, {"id", "A1"}});
            if (!check(format.value("format").toMap().value("shrinkToFit") == "1" &&
                        format.value("format").toMap().value("size") == "20" &&
                        fitted.model()->data(fitted.model()->index(0, 0), Qt::UserRole + 3).toMap() ==
                            format.value("display").toMap(),
                    "imported shrink format reaches GUI and AI without changing stored size"))
                return false;
            fitted.selectAddress("A2");
            if (!check(fitted.formatSelection({{"shrinkToFit", true}}) &&
                        fitted.readContent({{"view", "format"}, {"id", "A2"}})
                                .value("format")
                                .toMap()
                                .value("shrinkToFit") == "1",
                    "public format operation enables shrink on the selected cell"))
                return false;
            fitted.undo();
            if (!check(fitted.readContent({{"view", "format"}, {"id", "A2"}})
                            .value("format")
                            .toMap()
                            .value("shrinkToFit") == "0",
                    "undo restores the original shrink flag"))
                return false;
            fitted.redo();
            if (!check(fitted.readContent({{"view", "format"}, {"id", "A2"}})
                            .value("format")
                            .toMap()
                            .value("shrinkToFit") == "1",
                    "redo restores shrink through the shared transaction"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge colors;
            colors.requestOpen(
                QUrl::fromLocalFile(source.filePath("tests/fixtures/spreadsheet-theme-colors.xlsx")));
            if (!wait_for(colors))
                return false;
            const auto revision = colors.revision();
            const auto format = colors.readContent({{"view", "format"}, {"id", "A1"}});
            const auto condition = colors.readContent({{"view", "format"}, {"id", "B2"}});
            const auto table = colors.readContent({{"view", "format"}, {"id", "D2"}});
            const auto base = format.value("format").toMap();
            if (!check(format.value("ok").toBool() && base.value("text") == "#95B3D7" &&
                        base.value("fill") == "#DCE6F2" && base.value("borderBottomColor") == "#FF0000" &&
                        condition.value("display").toMap().value("fill") == "#95B3D7" &&
                        table.value("display").toMap().value("fill") == "#95B3D7" &&
                        colors.revision() == revision &&
                        colors.model()->data(colors.model()->index(0, 0), Qt::UserRole + 3).toMap() ==
                            format.value("display").toMap(),
                    "AI targeted color reads match GUI model theme, indexed, conditional and table styles"))
                return false;
            if (!check(colors.readContent({{"view", "format"}, {"id", "invalid"}}).value("error") ==
                        "invalid_cell_address",
                    "format inspection rejects invented addresses"))
                return false;
        }

        {
            mirrorfly::SpreadsheetBridge cutting;
            cutting.requestNew();
            cutting.selectAddress("A1:B1");
            cutting.pasteText("7\t=A1+1");
            cutting.addSheet(QStringLiteral("引用"));
            cutting.setCellValue(0, 0, QStringLiteral("='工作表1'!A1"), "auto");
            cutting.selectSheet(0);
            cutting.selectAddress("A1:B1");
            if (!check(cutting.startTool("cut") &&
                        cutting.model()->data(cutting.model()->index(0, 0)).toString() == "7" &&
                        cutting.layoutInfo().contains("pendingCut"),
                    "cut keeps source intact until paste commits"))
                return false;
            cutting.selectSheet(1);
            cutting.selectAddress("A3");
            if (!check(cutting.pasteCell() &&
                        cutting.model()->data(cutting.model()->index(2, 1)).toString() == "8" &&
                        !cutting.layoutInfo().contains("pendingCut"),
                    "cross-sheet paste commits deferred cut"))
                return false;
            cutting.selectAddress("A1");
            if (!check(cutting.cellInfo().value("inputText") == "='引用'!A3",
                    "cut redirects formulas outside the moved region"))
                return false;
            cutting.selectSheet(0);
            if (!check(cutting.model()->data(cutting.model()->index(0, 0)).toString().isEmpty(),
                    "successful cut clears source"))
                return false;
            cutting.undo();
            cutting.selectAddress("A1");
            if (!check(cutting.cellInfo().value("inputText") == "='工作表1'!A1",
                    "one cut undo restores external formula"))
                return false;
            cutting.selectSheet(0);
            if (!check(cutting.model()->data(cutting.model()->index(0, 1)).toString() == "8",
                    "cut undo restores source formula"))
                return false;
            cutting.redo();
            cutting.selectAddress("A3");
            if (!check(cutting.startTool("cut") && cutting.setCellValue(0, 3, "2", "number"),
                    "stale cut fixture"))
                return false;
            cutting.selectAddress("A4");
            if (!check(!cutting.pasteCell() &&
                        cutting.model()->data(cutting.model()->index(2, 0)).toString() == "7" &&
                        cutting.model()->data(cutting.model()->index(3, 0)).toString().isEmpty(),
                    "stale cut cannot clear or overwrite cells"))
                return false;
            mirrorfly::SpreadsheetBridge receiving;
            receiving.requestNew();
            if (!check(receiving.pasteCell() &&
                        receiving.model()->data(receiving.model()->index(0, 0)).toString() == "7" &&
                        cutting.model()->data(cutting.model()->index(2, 0)).toString() == "7",
                    "clipboard cut from another document is a non-destructive copy"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge sheets;
            sheets.requestNew();
            sheets.setCellValue(0, 0, "7", "number");
            if (!check(sheets.startTool("copySheet", {{"name", "副本"}}) && sheets.currentSheet() == 1 &&
                        sheets.model()->data(sheets.model()->index(0, 0)).toString() == "7",
                    "copy worksheet selects independent copy") ||
                !check(sheets.startTool("moveSheet", {{"index", 0}}) && sheets.currentSheet() == 0 &&
                        sheets.sheetNames().front() == "副本",
                    "move worksheet exposes new tab order") ||
                !check(sheets.startTool("hideSheet") && sheets.currentSheet() == 1 &&
                        sheets.layoutInfo().value("hiddenSheetIndices").toList() == QVariantList{0},
                    "hide switches to visible worksheet and exposes visibility"))
                return false;
            sheets.selectSheet(0);
            if (!check(sheets.currentSheet() == 1 && !sheets.startTool("hideSheet"),
                    "hidden sheet cannot be selected and last visible sheet is retained"))
                return false;
            sheets.undo();
            if (!check(sheets.currentSheet() == 0 &&
                        sheets.layoutInfo().value("hiddenSheets").toList().isEmpty(),
                    "undo restores worksheet visibility and selection"))
                return false;
            sheets.redo();
            if (!check(sheets.startTool("showSheet", {{"index", 0}}) && sheets.currentSheet() == 0 &&
                        sheets.startTool("deleteSheet") && sheets.sheetNames().size() == 1,
                    "show and delete worksheet through public commands"))
                return false;
            sheets.undo();
            if (!check(sheets.sheetNames().size() == 2 && sheets.sheetNames().front() == "副本" &&
                        sheets.model()->data(sheets.model()->index(0, 0)).toString() == "7",
                    "delete undo restores data and worksheet order"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge structure;
            structure.requestNew();
            if (!check(structure.setCellValue(1, 0, "12", "number") &&
                        structure.addSheet(QStringLiteral("引用")) &&
                        structure.setCellValue(0, 0, QStringLiteral("='工作表1'!$A$2"), "auto"),
                    "axis undo fixture"))
                return false;
            structure.selectSheet(0);
            structure.selectAddress("A2");
            if (!check(structure.startTool("insertRows", {{"count", 2}}) &&
                        structure.model()->data(structure.model()->index(3, 0)).toString() == "12",
                    "insert menu shifts entire rows"))
                return false;
            structure.selectSheet(1);
            if (!check(structure.cellInfo().value("inputText").toString() == "='工作表1'!$A$4",
                    "axis command adjusts references on another sheet"))
                return false;
            structure.undo();
            structure.selectSheet(1);
            if (!check(structure.cellInfo().value("inputText").toString() == "='工作表1'!$A$2",
                    "one undo restores references across the workbook"))
                return false;
            structure.redo();
            structure.selectAddress("A4");
            if (!check(structure.startTool("deleteRows"), "delete row menu commits"))
                return false;
            structure.selectSheet(1);
            if (!check(structure.model()->data(structure.model()->index(0, 0)).toString() == "#REF!",
                    "deleted cell reference reports REF"))
                return false;
            structure.undo();
            structure.undo();
            structure.undo();
            if (!check(structure.model()->data(structure.model()->index(0, 0)).toString().isEmpty(),
                    "cell undo remains valid after document snapshots") ||
                !check(!structure.startTool("insertRows", {{"count", 1.5}}) &&
                        !structure.startTool("insertRows", {{"count", true}}) &&
                        !structure.startTool("moveSheet", {{"index", "0"}}) &&
                        !structure.startTool("insertColumns", {{"after", "yes"}}),
                    "structural parameters reject nonintegral counts and nonboolean placement"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge tables;
            tables.requestNew();
            tables.selectAddress("A1:B3");
            const QVariantMap palette{{"header", "#345B89"}, {"headerText", "#FFFFFF"}, {"body", "#FFFFFF"},
                {"bodyText", "#123456"}, {"alternate", "#EDF2F9"}};
            if (!check(tables.pasteText("城市\t数量\n苏州\t5\n杭州\t10") &&
                        tables.startTool("tableStyle", {{"name", "Sales"}, {"palette", palette}}),
                    "table created through public menu command") ||
                !check(tables.layoutInfo().value("tables").toList().size() == 1,
                    "table metadata exposed through snapshot"))
                return false;
            tables.undo();
            if (!check(tables.layoutInfo().value("tables").toList().isEmpty(), "table creation is one undo"))
                return false;
            tables.redo();
            tables.selectAddress("B2");
            if (!check(tables.formatInfo().value("fill") == "#EDF2F9" &&
                        tables.formatSelection({{"fill", "#112233"}}),
                    "table band and direct format"))
                return false;
            if (!check(tables.startTool("tableStyle", {{"rowStripes", false}, {"lastColumn", true}}) &&
                        tables.formatInfo().value("fill") == "#112233" &&
                        tables.formatInfo().value("bold") == "1",
                    "table restyle retains manual fill"))
                return false;
            tables.selectAddress("A1:B4");
            if (!check(tables.startTool("tableRange", {{"name", "Sales"}}) &&
                        tables.layoutInfo().value("tables").toList().front().toMap().value("range") ==
                            "A1:B4",
                    "resize table range through public command"))
                return false;
            if (!check(tables.startTool("removeTable", {{"name", "Sales"}}), "remove table"))
                return false;
            tables.undo();
            if (!check(tables.layoutInfo().value("tables").toList().size() == 1,
                    "table removal undo restores metadata"))
                return false;
            tables.selectAddress("D2:E3");
            const QVariantMap cell_palette{{"fill", "#FFFFFF"}, {"text", "#123456"}, {"line", "#334455"}};
            for (const auto& name :
                QStringList{"good", "bad", "neutral", "input", "output", "title", "total"})
                if (!check(tables.styleSelection(name, cell_palette), "named cell preset applies"))
                    return false;
            if (!check(tables.formatInfo().value("borderTop") == "double" &&
                        tables.formatInfo().value("bold") == "1",
                    "total preset uses semantic borders"))
                return false;
            tables.undo();
            if (!check(tables.formatInfo().value("size") == "20", "cell preset undo restores title"))
                return false;
            if (!check(tables.styleSelection("normal", {}) && tables.formatInfo().value("bold") == "0",
                    "normal preset clears prior preset formatting"))
                return false;
            mirrorfly::SpreadsheetBridge sorted_table;
            sorted_table.requestNew();
            sorted_table.selectAddress("A1:B3");
            if (!check(sorted_table.pasteText("Key\tValue\n2\tb\n1\ta") &&
                        sorted_table.startTool("tableStyle", {{"palette", palette}}) &&
                        sorted_table.sortSelection(false, true),
                    "table rows sort"))
                return false;
            sorted_table.selectAddress("A2");
            if (!check(sorted_table.cellInfo().value("inputText") == "1" &&
                        sorted_table.formatInfo().value("fill") == "#EDF2F9",
                    "first sorted row keeps dynamic table stripe"))
                return false;
            sorted_table.selectAddress("A3");
            if (!check(sorted_table.formatInfo().value("fill") == "#FFFFFF",
                    "source stripe is not transferred as direct formatting"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge tabs;
            tabs.requestNew();
            if (!check(tabs.addSheet(QStringLiteral("资料")) && tabs.currentSheet() == 1,
                    "new worksheet selects its tab") ||
                !check(tabs.renameSheet(QStringLiteral("统计")), "rename worksheet through public API") ||
                !check(tabs.setCellValue(0, 0, QStringLiteral("内容"), "text"), "edit new worksheet"))
                return false;
            tabs.undo();
            tabs.undo();
            tabs.undo();
            if (!check(tabs.sheetNames().size() == 1 && tabs.currentSheet() == 0,
                    "undo across edits and worksheet structure restores a valid active tab"))
                return false;
            tabs.redo();
            tabs.redo();
            tabs.redo();
            if (!check(tabs.sheetNames().size() == 2 && tabs.sheetNames()[1] == QStringLiteral("统计"),
                    "redo restores worksheet name and edits"))
                return false;
            tabs.selectBand(1, 3, true);
            if (!check(tabs.rangeInfo().value("address").toString() == "A2:Z4",
                    "row header drag selects complete visible rows"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge tools;
            tools.requestNew();
            tools.selectAddress("A1:A3");
            if (!check(tools.pasteText("1\n2\n3") && tools.startTool("aggregate", {{"kind", "sum"}}),
                    "numeric aggregate commits"))
                return false;
            if (!check(tools.model()->data(tools.model()->index(3, 0)).toString() == "6",
                    "aggregate writes next empty row"))
                return false;
            tools.undo();
            if (!check(
                    tools.model()->data(tools.model()->index(3, 0)).toString().isEmpty(), "aggregate undo"))
                return false;
            tools.selectAddress("B1:D1");
            if (!check(tools.startTool("merge") && tools.layoutInfo().value("merges").toList().size() == 1,
                    "merge public state"))
                return false;
            if (!check(!tools.setCellValue(0, 2, "bad", "text"), "merged covered cell rejected"))
                return false;
            tools.undo();
            if (!check(tools.layoutInfo().value("merges").toList().isEmpty(), "merge undo"))
                return false;
            tools.redo();
            if (!check(tools.startTool("freeze", {{"rows", 1}, {"columns", 0}}), "freeze commits"))
                return false;
            tools.undo();
            if (!check(tools.layoutInfo().value("frozenRows").toInt() == 0, "freeze undo"))
                return false;
            tools.selectAddress("A1:A3");
            if (!check(tools.startTool(
                           "condition", {{"operator", "greaterThan"}, {"value", 1}, {"fill", "#ABCDEF"}}) &&
                        tools.model()
                                ->data(tools.model()->index(1, 0), Qt::UserRole + 3)
                                .toMap()
                                .value("fill") == "#ABCDEF",
                    "condition changes model style"))
                return false;
            if (!check(tools.startTool("filter", {{"value", "2"}}) && tools.rowHeight(2) == 0 &&
                        tools.rowHeight(1) > 0,
                    "filter hides mismatches with stable indices"))
                return false;
            tools.undo();
            if (!check(tools.rowHeight(2) > 0, "filter undo"))
                return false;
            tools.setCellValue(1, 1, "yes", "text");
            tools.setCellValue(2, 1, "no", "text");
            tools.selectAddress("A1:B3");
            if (!check(tools.startTool("filter", {{"column", 0}, {"values", QStringList{"2", "3"}}}) &&
                        tools.startTool("filter", {{"column", 1}, {"value", "yes"}}) &&
                        tools.layoutInfo().value("filters").toList().size() == 2 && tools.rowHeight(2) == 0 &&
                        tools.rowHeight(1) > 0,
                    "filters combine columns without replacing previous conditions"))
                return false;
            if (!check(tools.startTool("clearFilterColumn", {{"column", 1}}) && tools.rowHeight(2) > 0,
                    "clear one filter preserves the others"))
                return false;
            tools.undo();
            if (!check(tools.rowHeight(2) == 0 && tools.layoutInfo().value("filters").toList().size() == 2,
                    "undo restores column filter"))
                return false;
            tools.startTool("clearFilter");
            tools.selectAddress("B1:D1");
            tools.startTool("unmerge");
            tools.selectAddress("A1:C3");
            if (!check(
                    tools.startTool("border", {{"kind", "outer"}, {"style", "double"}, {"color", "#123456"}}),
                    "outer border commits atomically"))
                return false;
            tools.selectAddress("A1");
            if (!check(tools.formatInfo().value("borderLeft") == "double" &&
                        tools.formatInfo().value("borderRight") == "none",
                    "outer border omits internal edges"))
                return false;
            tools.selectAddress("B2");
            if (!check(tools.formatInfo().value("border") == "0", "center cell stays borderless"))
                return false;
            tools.selectAddress("A1:C3");
            tools.startTool("border", {{"kind", "inner"}, {"style", "dotted"}, {"color", "#445566"}});
            tools.selectAddress("A1");
            if (!check(tools.formatInfo().value("borderRight") == "dotted" &&
                        tools.formatInfo().value("borderLeft") == "double",
                    "inner borders preserve outer edges"))
                return false;
            tools.undo();
            tools.selectAddress("A1");
            if (!check(tools.formatInfo().value("borderRight") == "none", "border undo restores exact edges"))
                return false;
            tools.selectAddress("A1:C3");
            tools.startTool("border", {{"kind", "none"}});
            tools.selectAddress("A1:A3");
            tools.startTool("sequence");
            tools.selectAddress("A1");
            tools.formatSelection({{"strike", "1"}});
            tools.startTool("copyFormat");
            tools.selectAddress("A2");
            if (!check(tools.startTool("pasteFormat") && tools.formatInfo().value("strike") == "1",
                    "format brush"))
                return false;
            tools.undo();
            if (!check(tools.formatInfo().value("strike") == "0", "format brush undo"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge formulas;
            formulas.requestNew();
            formulas.setCellValue(99, 0, "7", "number");
            formulas.selectAddress("A100");
            if (!check(formulas.startTool("aggregate", {{"kind", "sum"}}) &&
                        formulas.model()->rowCount() == 101 &&
                        formulas.model()->data(formulas.model()->index(100, 0)).toString() == "7",
                    "aggregate at last visible row extends model"))
                return false;
            formulas.setCellValue(99, 0, "11", "number");
            if (!check(formulas.model()->data(formulas.model()->index(100, 0)).toString() == "11",
                    "aggregate recalculates after source edit"))
                return false;
            formulas.setCellValue(100, 0, "literal", "text");
            formulas.undo();
            formulas.selectAddress("A101");
            if (!check(formulas.cellInfo().value("inputText") == "=SUM(A100:A100)" &&
                        formulas.model()->data(formulas.model()->index(100, 0)).toString() == "11",
                    "undo restores formula source instead of cached result"))
                return false;
            formulas.setCellValue(0, 0, "2", "number");
            formulas.setCellValue(1, 0, "4", "number");
            formulas.setCellValue(0, 1, "=A1+$A$1", "auto");
            formulas.selectAddress("B1:B2");
            if (!check(formulas.startTool("fillDown") &&
                        formulas.model()->data(formulas.model()->index(1, 1)).toString() == "6",
                    "fill relocates relative references and preserves absolute references"))
                return false;
            formulas.selectAddress("B2");
            if (!check(formulas.cellInfo().value("inputText") == "=A2+$A$1", "filled formula is editable"))
                return false;
            formulas.undo();
            if (!check(
                    formulas.model()->data(formulas.model()->index(1, 1)).toString().isEmpty(), "fill undo"))
                return false;
            formulas.redo();
            if (!check(formulas.model()->data(formulas.model()->index(1, 1)).toString() == "6", "fill redo"))
                return false;
            formulas.selectAddress("B1");
            formulas.formatSelection({{"bold", "1"}, {"fill", "#ABCDEF"}});
            formulas.copyCell();
            formulas.selectAddress("C2");
            if (!check(formulas.pasteCell() && formulas.cellInfo().value("inputText") == "=B2+$A$1" &&
                        formulas.formatInfo().value("bold") == "1",
                    "copy paste retains style and translates formula"))
                return false;
            formulas.undo();
            if (!check(formulas.cellInfo().value("inputText").toString().isEmpty() &&
                        formulas.formatInfo().value("bold") == "0",
                    "clipboard transaction undo restores value and style"))
                return false;
            if (!check(formulas.startTool("pasteValues") && formulas.cellInfo().value("inputText") == "4" &&
                        !formulas.cellInfo().value("isFormula").toBool() &&
                        formulas.formatInfo().value("bold") == "0",
                    "paste values uses calculation and keeps target formatting"))
                return false;
            if (!check(formulas.startTool("pasteCellFormat") &&
                        formulas.cellInfo().value("inputText") == "4" &&
                        formulas.formatInfo().value("bold") == "1",
                    "paste formats leaves target value intact"))
                return false;
            if (!check(formulas.startTool("clearFormat") && formulas.cellInfo().value("inputText") == "4" &&
                        formulas.formatInfo().value("bold") == "0",
                    "clear formats restores normal style"))
                return false;
            formulas.selectAddress("A1:B2");
            if (!check(formulas.sortSelection(true, false) &&
                        formulas.model()->data(formulas.model()->index(0, 0)).toString() == "4" &&
                        formulas.model()->data(formulas.model()->index(0, 1)).toString() == "8" &&
                        formulas.model()
                                ->data(formulas.model()->index(1, 1), Qt::UserRole + 3)
                                .toMap()
                                .value("bold") == "1",
                    "sort moves row formats and translates formula references"))
                return false;
            formulas.undo();
            if (!check(formulas.model()->data(formulas.model()->index(0, 0)).toString() == "2" &&
                        formulas.model()
                                ->data(formulas.model()->index(0, 1), Qt::UserRole + 3)
                                .toMap()
                                .value("bold") == "1",
                    "sort undo restores values formulas and styles"))
                return false;
        }
        {
            mirrorfly::SpreadsheetBridge large;
            large.requestNew();
            const QString value(3000, 'x');
            for (int row = 0; row < 88; ++row)
                for (int column = 0; column < 4; ++column)
                    if (!large.setCellValue(row, column, value, "text"))
                        return false;
            large.selectAddress("A1:D88");
            if (!check(!large.startTool("cut") &&
                        large.model()->data(large.model()->index(0, 0)).toString().size() == 256,
                    "oversized clipboard cut preserves cell content"))
                return false;
        }
        mirrorfly::SpreadsheetBridge bridge;
        bridge.requestNew();
        auto* model = bridge.model();
        bool passed = check(
            bridge.active() && !bridge.modified() && model->rowCount() >= 100 && model->columnCount() >= 26,
            "new workbook exposes a sparse grid through the model");
        passed = check(model->headerData(25, Qt::Horizontal).toString() == "Z" &&
                         model->headerData(9, Qt::Vertical).toString() == "10",
                     "row and column labels use model coordinates") &&
            passed;

        passed = check(!bridge.setCellValue(
                           model->rowCount(), 0, QStringLiteral("outside"), QStringLiteral("text")) &&
                         !bridge.setCellValue(
                             0, model->columnCount(), QStringLiteral("outside"), QStringLiteral("text")) &&
                         !bridge.modified(),
                     "public edit requests cannot create invisible out-of-grid edits") &&
            passed;

        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import "../ui"
SpreadsheetPage
{
    required property var backend
    tableModel: backend.model
    sheetNames: backend.sheetNames
    currentSheet: backend.currentSheet
    cellInfo: backend.cellInfo
    rangeInfo: backend.rangeInfo
    layoutInfo: backend.layoutInfo
    layoutRevision: backend.layoutRevision
    function columnWidthFor(column) { return backend.columnWidth(column); }
    function rowHeightFor(row) { return backend.rowHeight(row); }
    busy: backend.locked
    modified: backend.modified
    canUndo: backend.canUndo
    canRedo: backend.canRedo
    error: backend.error
    compatibilitySummary: backend.compatibilitySummary
    onHomeRequested: backend.requestHome()
    onDismissError: backend.clearError()
    onCellRequested: function(row, column, extend) { backend.selectCell(row, column, extend); }
    onSheetRequested: function(index) { backend.selectSheet(index); }
    onCellEditRequested: function(row, column, value, kind)
    {
        acceptCellEdit(backend.setCellValue(row, column, value, kind));
    }
}
)qml",
            QUrl::fromLocalFile(source.filePath("tests/spreadsheet-contract.qml")));
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({{"backend", QVariant::fromValue(&bridge)},
                {"theme", theme}, {"width", 1040}, {"height", 720}, {"chromeInset", 44}}));
        auto* page = qobject_cast<QQuickItem*>(object.get());
        if (!check(page != nullptr, "spreadsheet page constructs without a window"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        for (int pass = 0; pass < 4; ++pass)
        {
            QCoreApplication::processEvents();
            page->ensurePolished();
        }
        auto* grid = page->findChild<QQuickItem*>(QStringLiteral("spreadsheetGrid"));
        passed = check(grid && grid->width() > 800 && grid->height() > 300,
                     "collapsed tools reserve the majority of the minimum window for cells") &&
            passed;
        passed = check(call_bool(page, "startEditing"), "ordinary cell enters draft editing") && passed;
        page->setProperty("draft", QStringLiteral("001234"));
        passed = check(!bridge.modified() && call_bool(page, "finishCellEditing") && bridge.modified() &&
                         bridge.cellInfo().value("kind") == "text" &&
                         model->data(model->index(0, 0)).toString() == "001234",
                     "draft commits once and automatic type keeps leading-zero identifiers") &&
            passed;
        bridge.undo();
        passed = check(!bridge.modified() && model->data(model->index(0, 0)).toString().isEmpty(),
                     "undo restores the clean baseline") &&
            passed;
        bridge.redo();
        passed = check(bridge.modified() && model->data(model->index(0, 0)).toString() == "001234",
                     "redo restores a committed delta") &&
            passed;

        call_bool(page, "startEditing");
        page->setProperty("draftKind", QStringLiteral("auto"));
        page->setProperty("draft", QStringLiteral("=SUMIFS(A2:A3)"));
        passed = check(!call_bool(page, "finishCellEditing") && page->property("editing").toBool() &&
                         page->property("draft").toString() == "=SUMIFS(A2:A3)" &&
                         bridge.cellInfo().value("inputText") == "001234",
                     "unsupported formula draft remains available after rejection") &&
            passed;
        QVariant selected;
        QMetaObject::invokeMethod(page, "chooseCell", Q_RETURN_ARG(QVariant, selected), Q_ARG(QVariant, 1),
            Q_ARG(QVariant, 1), Q_ARG(QVariant, false));
        passed = check(!selected.toBool() && bridge.cellInfo().value("address") == "A1",
                     "invalid draft prevents moving to another cell") &&
            passed;
        QMetaObject::invokeMethod(page, "cancelCellEditing");
        passed = check(!page->property("editing").toBool() && page->property("draft") == "001234",
                     "escape restores the committed value") &&
            passed;
        bridge.selectCell(0, 1);
        passed =
            check(bridge.setCellValue(0, 1, QStringLiteral("12345678901234567890"), QStringLiteral("auto")) &&
                    bridge.cellInfo().value("kind") == "text",
                "automatic type preserves long numeric identifiers") &&
            passed;

        QTemporaryDir directory;
        const auto path = directory.filePath("roundtrip.xlsx");
        bridge.saveAs();
        bridge.selectSaveFile(QUrl::fromLocalFile(path));
        passed = wait_for(bridge) && passed;
        const auto loaded = mirrorfly::load_spreadsheet_file(path.toUtf8().toStdString());
        passed = check(!bridge.modified() && loaded.error == mirrorfly::SpreadsheetError::None &&
                         mirrorfly::spreadsheet_cell(loaded.document, 0, {0, 0}).value.text == "001234" &&
                         mirrorfly::spreadsheet_cell(loaded.document, 0, {0, 1}).value.text ==
                             "12345678901234567890",
                     "real XLSX save and reopen retain exact values") &&
            passed;
        bridge.undo();
        passed = check(bridge.modified(), "undo across a save is dirty") && passed;
        bridge.redo();
        passed = check(!bridge.modified(), "redo to the saved generation is clean") && passed;
        bridge.undo();
        bridge.setCellValue(0, 1, QStringLiteral("new branch"), QStringLiteral("text"));
        passed = check(bridge.modified() && !bridge.canRedo(),
                     "branch after undo cannot reuse a saved identity") &&
            passed;

        QFile disk(path);
        if (!disk.open(QIODevice::Append))
        {
            return false;
        }
        disk.write("external change");
        disk.close();
        bridge.save();
        passed = wait_for(bridge) && passed;
        passed = check(bridge.modified() && !bridge.error().isEmpty() &&
                         bridge.cellInfo().value("inputText") == "new branch",
                     "external save conflict retains the edit") &&
            passed;
#ifdef Q_OS_WIN
        QTemporaryDir alias_directory;
        const auto alias = alias_directory.filePath("workbook-alias");
        QProcess junction;
        junction.setProgram(QStringLiteral("powershell.exe"));
        auto quoted = [](QString value)
        {
            return "'" + value.replace("'", "''") + "'";
        };
        junction.setArguments({"-NoProfile", "-NonInteractive", "-Command",
            "New-Item -ItemType Junction -Path " + quoted(alias) + " -Target " + quoted(directory.path()) +
                " | Out-Null"});
        junction.start();
        const bool alias_created = junction.waitForFinished(10000) && junction.exitCode() == 0;
        passed =
            check(alias_created, "isolated directory alias is available for conflict regression") && passed;
        if (alias_created)
        {
            bridge.saveAs();
            bridge.selectSaveFile(QUrl::fromLocalFile(QDir(alias).filePath("roundtrip.xlsx")));
            passed = wait_for(bridge) && passed;
            passed = check(bridge.modified() && !bridge.error().isEmpty(),
                         "directory alias save cannot bypass current-file revision validation") &&
                passed;
            QDir().rmdir(alias);
        }
#endif
        bridge.requestOpen(QUrl::fromLocalFile(directory.filePath("missing.xlsx")));
        bridge.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for(bridge) && passed;
        passed = check(bridge.active() && bridge.modified() &&
                         bridge.cellInfo().value("inputText") == "new branch",
                     "failed replacement load retains the complete previous workbook") &&
            passed;
        bridge.requestHome();
        bridge.resolveUnsaved(QStringLiteral("save"));
        passed = wait_for(bridge) && passed;
        passed = check(bridge.active() && bridge.modified() && !bridge.locked(),
                     "failed save cancels pending home navigation") &&
            passed;

        auto package = mirrorfly::serialize_spreadsheet(mirrorfly::make_spreadsheet());
        for (auto& part : package.parts)
        {
            if (part.path == "xl/worksheets/sheet1.xml")
            {
                part.bytes =
                    "<worksheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'>"
                    "<sheetData><row r='1'><c r='A1'><f>SUBTOTAL(9,B1)</f><v>5</v></c><c r='B1'><v>4</v></c>"
                    "<c r='C1' t='inlineStr'><is><t>=literal</t></is></c><c r='D1' t='inlineStr'><is><t>" +
                    std::string(40000, 'x') + "</t></is></c></row></sheetData></worksheet>";
            }
            if (part.path == "xl/workbook.xml")
            {
                part.bytes.insert(
                    part.bytes.find("</sheets>"), "<sheet name='Second' sheetId='2' r:id='second'/>");
            }
            if (part.path == "xl/_rels/workbook.xml.rels")
            {
                part.bytes.insert(part.bytes.find("</Relationships>"),
                    "<Relationship Id='second' "
                    "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet' "
                    "Target='worksheets/sheet2.xml'/>");
            }
            if (part.path == "[Content_Types].xml")
            {
                part.bytes.insert(part.bytes.find("</Types>"),
                    "<Override PartName='/xl/worksheets/sheet2.xml' "
                    "ContentType='application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml'/"
                    ">");
            }
        }
        package.parts.push_back({"xl/worksheets/sheet2.xml",
            "<worksheet "
            "xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'><sheetData/></worksheet>"});
        const auto formula_path = directory.filePath("formula.xlsx");
        const auto saved =
            mirrorfly::save_spreadsheet_file(formula_path.toUtf8().toStdString(), package.parts, {});
        passed = check(saved.error == mirrorfly::SpreadsheetError::None, "formula fixture saves") && passed;
        bridge.requestOpen(QUrl::fromLocalFile(formula_path));
        bridge.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for(bridge) && passed;
        passed = check(!bridge.cellInfo().value("canEdit").toBool() &&
                         bridge.cellInfo().value("isFormula").toBool() && !call_bool(page, "startEditing") &&
                         model->data(model->index(0, 0)).toString() == "5",
                     "formula cells expose a cached value while remaining read-only") &&
            passed;
        bridge.setCellValue(0, 1, QStringLiteral("10"), QStringLiteral("number"));
        passed = check(model->data(model->index(0, 0)).toString() == QStringLiteral("待重算"),
                     "editing marks formula caches stale rather than presenting an incorrect result") &&
            passed;
        bridge.selectCell(0, 2);
        bridge.setCellValue(0, 2, QStringLiteral("changed literal"), QStringLiteral("text"));
        bridge.undo();
        passed = check(bridge.cellInfo().value("inputText") == "=literal",
                     "undo restores imported equals-prefixed literal text") &&
            passed;
        bridge.redo();
        bridge.undo();
        bridge.selectCell(0, 3);
        passed = check(!bridge.cellInfo().value("canEdit").toBool() &&
                         bridge.cellInfo().value("inputText").toString().size() <= 257 &&
                         model->data(model->index(0, 3)).toString().size() <= 256,
                     "large imported values remain read-only and never enter full UI text layout") &&
            passed;
        bridge.selectCell(0, 1);
        call_bool(page, "startEditing");
        page->setProperty("draft", QStringLiteral("20"));
        page->setProperty("activeGroup", QStringLiteral("sheet"));
        page->setProperty("activeSection", QStringLiteral("switch"));
        auto* chooser = page->findChild<QObject*>(QStringLiteral("spreadsheetSheetChooser"));
        if (!check(chooser != nullptr, "workbook sheet chooser exists"))
        {
            return false;
        }
        chooser->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(chooser, "activated", Q_ARG(int, 1));
        passed = check(bridge.currentSheet() == 1 && !page->property("editing").toBool(),
                     "sheet chooser retains requested target across synchronous draft commit") &&
            passed;
        bridge.save();
        passed = wait_for(bridge) && passed;
        const auto literal_saved = mirrorfly::load_spreadsheet_file(formula_path.toUtf8().toStdString());
        passed =
            check(literal_saved.error == mirrorfly::SpreadsheetError::None &&
                    literal_saved.document.sheets.size() == 2 &&
                    mirrorfly::spreadsheet_cell(literal_saved.document, 0, {0, 2}).value.text == "=literal" &&
                    !mirrorfly::spreadsheet_cell(literal_saved.document, 0, {0, 2}).formula_cell &&
                    mirrorfly::spreadsheet_cell(literal_saved.document, 0, {0, 3}).value.text.size() ==
                        40000 &&
                    mirrorfly::spreadsheet_cell(literal_saved.document, 0, {0, 1}).value.text == "20",
                "saved workbook preserves literal text, untouched large cells and committed sheet draft") &&
            passed;

        bridge.selectSheet(0);
        bridge.selectCell(0, 1);
        call_bool(page, "startEditing");
        passed = check(page->property("draftKind") == "auto",
                     "stored numeric type does not lock subsequent typing") &&
            passed;
        page->setProperty("draft", QStringLiteral("普通文字"));
        QMetaObject::invokeMethod(page, "navigate", Q_ARG(QVariant, "home"));
        passed = check(!page->property("editing").toBool() && bridge.locked() &&
                         bridge.cellInfo().value("inputText") == QStringLiteral("普通文字"),
                     "numeric cell changed to Chinese text reaches the normal unsaved-home prompt") &&
            passed;
        bridge.resolveUnsaved(QStringLiteral("cancel"));
        passed = check(bridge.active() && !bridge.locked(), "cancelling home keeps the committed workbook") &&
            passed;
        call_bool(page, "startEditing");
        page->setProperty("draftKind", QStringLiteral("number"));
        page->setProperty("draft", QStringLiteral("invalid number"));
        QMetaObject::invokeMethod(page, "navigate", Q_ARG(QVariant, "home"));
        passed = check(page->property("pendingNavigation") == "home" && page->property("editing").toBool() &&
                         !bridge.locked(),
                     "invalid draft offers an explicit navigation decision") &&
            passed;
        QMetaObject::invokeMethod(page, "resolveDraftNavigation", Q_ARG(QVariant, false));
        passed = check(page->property("pendingNavigation").toString().isEmpty() &&
                         page->property("draft") == "invalid number",
                     "keep-editing retains invalid draft") &&
            passed;
        QMetaObject::invokeMethod(page, "navigate", Q_ARG(QVariant, "home"));
        QMetaObject::invokeMethod(page, "resolveDraftNavigation", Q_ARG(QVariant, true));
        passed = check(!page->property("editing").toBool() && bridge.locked() &&
                         bridge.cellInfo().value("inputText") == QStringLiteral("普通文字"),
                     "discarding only the draft still protects previously committed edits") &&
            passed;
        bridge.resolveUnsaved(QStringLiteral("cancel"));
        bridge.selectCell(0, 1);
        passed =
            check(bridge.findCell(QStringLiteral("literal")) && bridge.cellInfo().value("address") == "C1" &&
                    bridge.findCell(QStringLiteral("literal"), true) &&
                    bridge.cellInfo().value("address") == "C1",
                "search wraps in both directions over real cell contents") &&
            passed;
        passed =
            check(bridge.findCell(QStringLiteral("SUBTOTAL")) && bridge.cellInfo().value("address") == "A1",
                "search includes imported formula source without evaluating it") &&
            passed;
        passed = check(!bridge.findCell(QStringLiteral("not present anywhere")) &&
                         bridge.cellInfo().value("address") == "A1",
                     "failed search retains selection") &&
            passed;
        passed = check(bridge.selectAddress(QStringLiteral(" c20 ")) &&
                         bridge.cellInfo().value("address") == "C20" &&
                         !bridge.selectAddress(QStringLiteral("XFD1048577")) &&
                         bridge.cellInfo().value("address") == "C20",
                     "address navigation is validated and does not dirty the workbook") &&
            passed;
        bridge.selectCell(1, 0);
        passed = check(bridge.setCellValue(1, 0, QStringLiteral("FALSE"), QStringLiteral("auto")) &&
                         bridge.cellInfo().value("kind") == "boolean",
                     "automatic input recognizes boolean words") &&
            passed;
        call_bool(page, "startEditing");
        passed = check(call_bool(page, "finishCellEditing") && bridge.cellInfo().value("kind") == "boolean",
                     "entering and leaving an unchanged boolean does not convert it to a number") &&
            passed;
        auto* value_input = page->findChild<QObject*>(QStringLiteral("spreadsheetValueEditor"));
        value_input->setProperty("text", QStringLiteral("首字输入"));
        QMetaObject::invokeMethod(value_input, "textEdited");
        passed = check(page->property("draft") == QStringLiteral("首字输入") &&
                         call_bool(page, "finishCellEditing"),
                     "first input-bar edit preserves the text before synchronizing the draft") &&
            passed;
        QMetaObject::invokeMethod(page, "navigate", Q_ARG(QVariant, "home"));
        bridge.resolveUnsaved(QStringLiteral("discard"));
        passed = check(!bridge.active() && !page->property("editing").toBool(),
                     "home completes after explicit discard") &&
            passed;
        bridge.requestNew();
        passed =
            check(bridge.pasteText(QStringLiteral("项目\t进度\t编号\nA\t12.5\t0012\nB\t7.5\t0013")) &&
                    bridge.rangeInfo().value("count").toInt() == 9 && bridge.rangeInfo().value("sum") == "20",
                "rectangular paste detects types and selection statistics") &&
            passed;
        passed = check(bridge.selectionText().contains(QStringLiteral("0012")),
                     "region copy preserves leading zeros") &&
            passed;
        bridge.undo();
        passed = check(!bridge.modified() && model->data(model->index(1, 1)).toString().isEmpty(),
                     "one undo reverses every pasted cell") &&
            passed;
        bridge.redo();
        passed = check(model->data(model->index(2, 1)).toString() == "7.5", "one redo restores the batch") &&
            passed;
        bridge.selectAddress(QStringLiteral("B2:B3"));
        passed = check(bridge.pasteText(QStringLiteral("5")) && bridge.rangeInfo().value("sum") == "10",
                     "one copied value fills the selected rectangle") &&
            passed;
        bridge.undo();
        passed = check(model->data(model->index(1, 1)).toString() == "12.5",
                     "fill undo retains original values") &&
            passed;
        bridge.selectAddress(QStringLiteral("A1:C3"));
        passed = check(bridge.clearSelection() && bridge.rangeInfo().value("nonempty").toInt() == 0,
                     "clear selection is a batch operation") &&
            passed;
        bridge.undo();
        const auto before_invalid = bridge.selectionText();
        passed = check(!bridge.pasteText(QStringLiteral("ok\t=SUMIFS(A1)")) &&
                         bridge.selectionText() == before_invalid,
                     "invalid formula rejects the entire paste") &&
            passed;
        passed =
            check(!bridge.pasteText(QStringLiteral("a\tb\nc")) && bridge.selectionText() == before_invalid,
                "ragged clipboard rows reject atomically") &&
            passed;
        bridge.selectAddress(QStringLiteral("E1"));
        passed = check(bridge.pasteText(QStringLiteral("\"first\nsecond\"\t\"a\"\"b\"")) &&
                         model->data(model->index(0, 4)).toString() == "first\nsecond" &&
                         bridge.selectionText() == "\"first\nsecond\"\t\"a\"\"b\"\n",
                     "quoted multiline cells and doubled quotes roundtrip") &&
            passed;
        bridge.selectAddress(QStringLiteral("A1:A4"));
        const auto with_empty_last_row = bridge.selectionText();
        bridge.selectCell(10, 0);
        passed =
            check(bridge.pasteText(with_empty_last_row) && bridge.rangeInfo().value("count").toInt() == 4 &&
                    model->data(model->index(13, 0)).toString().isEmpty(),
                "copy and paste preserve a trailing empty row") &&
            passed;
        bridge.selectCell(0, 0);
        QMetaObject::invokeMethod(page, "revealCell");
        call_bool(page, "startEditing");
        page->setProperty("draft", QStringLiteral("42"));
        for (int pass = 0; pass < 5; ++pass)
        {
            QCoreApplication::processEvents();
            grid->ensurePolished();
        }
        QQuickItem* inline_input = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QList<QQuickItem*> pending_items{grid};
        while (!pending_items.empty())
        {
            auto* candidate = pending_items.takeLast();
            if (candidate->objectName() == QStringLiteral("spreadsheetInlineEditor") &&
                candidate->isVisible() && candidate->parentItem() &&
                candidate->parentItem()->property("row").toInt() == bridge.cellInfo().value("row").toInt() &&
                candidate->parentItem()->property("column").toInt() ==
                    bridge.cellInfo().value("column").toInt())
            {
                inline_input = candidate;
            }
            pending_items.append(candidate->childItems());
        }
        passed =
            check(inline_input != nullptr, "inline editor is available for keyboard regression") && passed;
        if (inline_input)
        {
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(inline_input, &enter);
            passed = check(!page->property("editing").toBool() && bridge.cellInfo().value("row").toInt() == 1,
                         "Enter commits once and does not bubble into a new edit session") &&
                passed;
        }

        for (const auto* group : {"cell", "sheet", "file"})
        {
            page->setProperty("activeGroup", group);
            for (int pass = 0; pass < 3; ++pass)
            {
                QCoreApplication::processEvents();
                page->ensurePolished();
            }
            passed = check(page->property("activeSection").toString().isEmpty() && grid->height() > 200,
                         "changing the parent category keeps child parameters collapsed") &&
                passed;
        }
        bridge.selectCell(0, 0);
        passed = check(bridge.startTool("freeze", {{"rows", 1}, {"columns", 1}}),
                     "frozen panes public operation") &&
            passed;
        for (int pass = 0; pass < 5; ++pass)
        {
            QCoreApplication::processEvents();
            page->ensurePolished();
        }
        auto* frozen = page->findChild<QQuickItem*>("frozenCorner");
        passed = check(frozen && frozen->width() == bridge.columnWidth(0) &&
                         frozen->height() == bridge.rowHeight(0),
                     "frozen corner has real cell geometry without opening a window") &&
            passed;
        bridge.startTool("unfreeze");
        return passed;
    }
}

int run_spreadsheet_ui_tests(int argc, char* argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
    QGuiApplication application(argc, argv);
    qml_register_types_Mirrorfly_Native();
    QStandardPaths::setTestModeEnabled(true);
    QQmlEngine engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& errors)
    {
        for (const auto& error : errors)
        {
            warnings.append(error.toString());
        }
    });
    const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
    QFile file(source.filePath("config/theme.json"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return 1;
    }
    const auto theme = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    const bool passed = run_cases(engine, theme, source);
    for (const auto& warning : warnings)
    {
        std::cerr << warning.toStdString() << '\n';
    }
    return passed && warnings.empty() ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_spreadsheet_ui_tests(argc, argv);
}
