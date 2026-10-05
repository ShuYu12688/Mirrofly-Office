#include <mirrorfly/spreadsheet_storage.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>

namespace
{
    QJsonObject format_json(const mirrorfly::SpreadsheetFormat& format)
    {
        QJsonObject result;
        for (const auto& [key, value] : format)
            result.insert(QString::fromStdString(key), QString::fromStdString(value));
        return result;
    }
}

int run_spreadsheet_corpus_probe(int argc, char* argv[])
{
    using namespace mirrorfly;
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() != 3)
        return 2;
    const QDir output(args[2]);
    if (!QDir().mkpath(output.absolutePath()) || !output.entryList(QDir::Files).isEmpty())
        return 2;
    QElapsedTimer timer;
    timer.start();
    const auto loaded = load_spreadsheet_file(args[1].toStdString());
    QJsonObject report{{"loaded", loaded.error == SpreadsheetError::None},
        {"error", QString::fromStdString(loaded.message)}, {"loadMs", timer.elapsed()},
        {"scope", "Public core model and untouched package roundtrip; not a visual fidelity result."}};
    bool success = loaded.error == SpreadsheetError::None;
    if (success)
    {
        QJsonArray sheets;
        for (std::size_t i = 0; i < loaded.document.sheets.size(); ++i)
        {
            const auto& sheet = loaded.document.sheets[i];
            QJsonArray cells;
            int supported_formulas = 0;
            int unsupported_formulas = 0;
            for (const auto& [address, original] : sheet.cells)
            {
                const auto cell = spreadsheet_cell_properties(loaded.document, i, address);
                if (cell.formula_cell)
                    cell.formula_supported ? ++supported_formulas : ++unsupported_formulas;
                if (cells.size() >= 4096)
                    continue;
                cells.append(QJsonObject{{"address", QString::fromStdString(spreadsheet_address(address))},
                    {"text", QString::fromStdString(cell.value.text)},
                    {"formula", QString::fromStdString(cell.formula)},
                    {"formulaSupported", cell.formula_supported}, {"editable", cell.editable},
                    {"directFormat",
                        format_json(spreadsheet_cell_direct_format(loaded.document, i, address))},
                    {"format", format_json(spreadsheet_cell_format(loaded.document, i, address))}});
            }
            const auto& features = spreadsheet_features(loaded.document, i);
            sheets.append(QJsonObject{{"name", QString::fromStdString(sheet.name)}, {"hidden", sheet.hidden},
                {"storedCells", static_cast<int>(sheet.cells.size())}, {"cells", cells},
                {"truncated", sheet.cells.size() > 4096}, {"supportedFormulas", supported_formulas},
                {"unsupportedFormulas", unsupported_formulas},
                {"merges", static_cast<int>(features.merges.size())},
                {"conditions", static_cast<int>(features.conditions.size())},
                {"conditionsSupported", features.conditions_supported},
                {"tablesSupported", features.tables_supported},
                {"tables", static_cast<int>(features.tables.size())}});
        }
        report.insert("sheets", sheets);
        const auto serialized = serialize_spreadsheet(loaded.document);
        report.insert("serialized", serialized.error == SpreadsheetError::None);
        const auto saved = serialized.error == SpreadsheetError::None
            ? save_spreadsheet_file(
                  output.filePath("roundtrip.xlsx").toStdString(), serialized.parts, "missing")
            : SpreadsheetSaveResult{serialized.error, serialized.message, {}, {}};
        report.insert("saved", saved.error == SpreadsheetError::None);
        report.insert("saveError", QString::fromStdString(saved.message));
        const auto reopened = saved.error == SpreadsheetError::None
            ? load_spreadsheet_file(output.filePath("roundtrip.xlsx").toStdString())
            : SpreadsheetResult{};
        const bool reopened_ok = saved.error == SpreadsheetError::None &&
            reopened.error == SpreadsheetError::None &&
            reopened.document.sheets.size() == loaded.document.sheets.size();
        report.insert("reopened", reopened_ok);
        success = reopened_ok;
    }
    report.insert("totalMs", timer.elapsed());
    QFile file(output.filePath("report.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0)
        return 2;
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
    return success ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_spreadsheet_corpus_probe(argc, argv);
}
