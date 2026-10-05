#include "spreadsheet_bridge.hpp"
#include "spreadsheet_input.hpp"

#include <QFontDatabase>
#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    PdfExportSource SpreadsheetBridge::pdfSource() const
    {
        PdfExportSource result;
        if (!active_ || locked())
            result.error = "工作簿尚未准备好导出。";
        else
            result.content = document_;
        result.title = documentName().toStdString();
        result.source_path = path_.toStdString();
        result.current = static_cast<std::size_t>(sheet_);
        result.selection = selectedRange();
        return result;
    }

    void SpreadsheetBridge::selectBand(int first, int last, bool rows)
    {
        if (!active_ || locked() || first < 0 || last < 0)
            return;
        const int limit = rows ? model_.rowCount() : model_.columnCount();
        if (first >= limit || last >= limit)
            return;
        selectCell(rows ? first : 0, rows ? 0 : first, false);
        selectCell(rows ? last : model_.rowCount() - 1, rows ? model_.columnCount() - 1 : last, true);
    }

    bool SpreadsheetBridge::addSheet(const QString& requested)
    {
        QString name = requested.trimmed();
        if (name.isEmpty())
        {
            int number = 1;
            do
            {
                name = QStringLiteral("工作表%1").arg(number++);
            } while (sheetNames().contains(name, Qt::CaseInsensitive));
        }
        if (!name.isValidUtf16() || sheetNames().contains(name, Qt::CaseInsensitive))
            return false;
        const auto index = document_.sheets.size();
        return commitStructure({SpreadsheetSheetAction::Add, index, name.toStdString()},
            {SpreadsheetSheetAction::RemoveAdded, index, {}});
    }

    bool SpreadsheetBridge::renameSheet(const QString& requested)
    {
        if (!active_ || locked() || !requested.isValidUtf16())
            return false;
        const auto name = requested.trimmed();
        const auto names = sheetNames();
        for (int index = 0; index < names.size(); ++index)
            if (index != sheet_ && names[index].compare(name, Qt::CaseInsensitive) == 0)
                return false;
        return applySheetTool("renameSheet", {{"name", name}});
    }

    bool SpreadsheetBridge::commitStructure(
        const SpreadsheetSheetCommand& command, const SpreadsheetSheetCommand& inverse)
    {
        if (!active_ || locked())
            return false;
        HistoryEntry entry;
        entry.sheet = command.index;
        entry.before_generation = generation_;
        entry.after_generation = next_generation_;
        entry.structure_before = inverse;
        entry.structure_after = command;
        try
        {
            undo_.push_back(entry);
        }
        catch (...)
        {
            setError(QStringLiteral("无法分配撤销记录，工作簿未修改。"));
            return false;
        }
        const auto result = apply_spreadsheet_sheet_command(document_, command);
        if (result.error != SpreadsheetError::None || !result.changed)
        {
            undo_.pop_back();
            setError(QString::fromStdString(result.message));
            return result.error == SpreadsheetError::None;
        }
        generation_ = next_generation_++;
        redo_.clear();
        trimHistory();
        sheet_ = static_cast<int>(command.index);
        cell_ = {};
        anchor_ = {};
        model_.setDocument(&document_, command.index);
        ++layout_revision_;
        clearError();
        emit documentChanged();
        emit cellChanged();
        emit stateChanged();
        return true;
    }

    QVariantMap SpreadsheetBridge::formatInfo() const
    {
        QVariantMap result;
        for (const auto& [key, value] :
            spreadsheet_cell_format(document_, static_cast<std::size_t>(sheet_), cell_))
            result.insert(QString::fromStdString(key), QString::fromStdString(value));
        result.insert("columnWidth", spreadsheet_dimension(document_, sheet_, true, cell_.column));
        result.insert("rowHeight", spreadsheet_dimension(document_, sheet_, false, cell_.row));
        return result;
    }

    qulonglong SpreadsheetBridge::revision() const
    {
        return revision_;
    }
    int SpreadsheetBridge::layoutRevision() const
    {
        return layout_revision_;
    }

    double SpreadsheetBridge::columnWidth(int column) const
    {
        if (column >= 0 && spreadsheet_features(document_, sheet_).hidden_columns.count(column))
            return 0;
        return column < 0
            ? 132
            : std::clamp(spreadsheet_dimension(document_, sheet_, true, column) * 7 + 6, 28.0, 566.0);
    }

    double SpreadsheetBridge::rowHeight(int row) const
    {
        if (row >= 0 && !model_.rowVisible(row))
            return 0;
        return row < 0
            ? 34
            : std::clamp(spreadsheet_dimension(document_, sheet_, false, row) * 4 / 3, 16.0, 400.0);
    }

    bool SpreadsheetBridge::formatSelection(const QVariantMap& patch)
    {
        if (!active_ || locked() || patch.size() > 32)
            return false;
        if (patch.contains("font") && !QFontDatabase::families().contains(patch.value("font").toString()))
        {
            setError(QStringLiteral("未找到该系统字体。"));
            return false;
        }
        const auto range = selectedRange();
        std::vector<SpreadsheetFormatCommand> commands;
        for (auto row = range.first.row; row <= range.last.row; ++row)
            for (auto column = range.first.column; column <= range.last.column; ++column)
            {
                SpreadsheetFormat format;
                const auto sheet = document_.format_edits.find(sheet_);
                if (sheet != document_.format_edits.end())
                {
                    const auto found = sheet->second.find({row, column});
                    if (found != sheet->second.end())
                        format = found->second;
                }
                if (patch.contains("number") && !patch.contains("decimals"))
                    format.erase("decimals");
                if (patch.contains("border"))
                    for (auto item = format.begin(); item != format.end();)
                    {
                        if (item->first.rfind("border", 0) == 0)
                            item = format.erase(item);
                        else
                            ++item;
                    }
                for (auto item = patch.begin(); item != patch.end(); ++item)
                    format[item.key().toStdString()] = item.value().metaType().id() == QMetaType::Bool
                        ? (item.value().toBool() ? "1" : "0")
                        : item.value().toString().toStdString();
                commands.push_back({static_cast<std::size_t>(sheet_), {row, column}, std::move(format)});
            }
        return commitChanges({}, commands, {});
    }

    bool SpreadsheetBridge::styleSelection(const QString& preset, const QVariantMap& palette)
    {
        const QStringList presets{"header", "banded", "plain", "normal", "good", "bad", "neutral", "input",
            "output", "title", "total"};
        if (!active_ || locked() || !presets.contains(preset))
            return false;
        if (preset != "header" && preset != "banded" && preset != "plain")
        {
            SpreadsheetFormat format{{"reset", "1"}};
            if (preset != "normal")
            {
                if (palette.size() != 3 || !palette.contains("fill") || !palette.contains("text") ||
                    !palette.contains("line"))
                    return false;
                format["fill"] = palette.value("fill").toString().toStdString();
                format["text"] = palette.value("text").toString().toStdString();
                format["bold"] = preset == "output" || preset == "title" || preset == "total" ? "1" : "0";
                if (preset == "title")
                    format["size"] = "20";
                if (preset == "input")
                    format["italic"] = "1";
                if (preset == "output" || preset == "total")
                {
                    format["borderBottom"] = "thin";
                    format["borderBottomColor"] = palette.value("line").toString().toStdString();
                }
                if (preset == "total")
                {
                    format["borderTop"] = "double";
                    format["borderTopColor"] = palette.value("line").toString().toStdString();
                }
            }
            const auto range = selectedRange();
            std::vector<SpreadsheetFormatCommand> commands;
            for (auto row = range.first.row; row <= range.last.row; ++row)
                for (auto column = range.first.column; column <= range.last.column; ++column)
                    commands.push_back({static_cast<std::size_t>(sheet_), {row, column}, format});
            return commitChanges({}, commands, {});
        }
        const auto range = selectedRange();
        std::vector<SpreadsheetFormatCommand> commands;
        for (auto row = range.first.row; row <= range.last.row; ++row)
            for (auto column = range.first.column; column <= range.last.column; ++column)
            {
                SpreadsheetFormat format;
                if (preset != "plain")
                {
                    const bool heading = preset == "header" || row == range.first.row;
                    format["fill"] =
                        palette
                            .value(
                                heading ? "header" : (row % 2 == range.first.row % 2 ? "body" : "alternate"))
                            .toString()
                            .toStdString();
                    format["text"] =
                        palette.value(heading ? "headerText" : "bodyText").toString().toStdString();
                    format["bold"] = heading ? "1" : "0";
                    format["border"] = "1";
                    format["wrap"] = "1";
                }
                commands.push_back({static_cast<std::size_t>(sheet_), {row, column}, std::move(format)});
            }
        return commitChanges({}, commands, {});
    }

    bool SpreadsheetBridge::resizeSelection(bool columns, double size)
    {
        if (!active_ || locked())
            return false;
        const auto range = selectedRange();
        std::vector<SpreadsheetDimensionCommand> commands;
        const auto last = columns ? range.last.column : range.last.row;
        for (auto index = columns ? range.first.column : range.first.row; index <= last; ++index)
            commands.push_back({static_cast<std::size_t>(sheet_), columns, index, size});
        return commitChanges({}, {}, commands);
    }

    bool SpreadsheetBridge::sortSelection(bool descending, bool header)
    {
        if (!active_ || locked())
            return false;
        const auto range = selectedRange();
        for (const auto& table : spreadsheet_features(document_, sheet_).tables)
            if (range.first.row <= table.range.last.row && table.range.first.row <= range.last.row &&
                range.first.column <= table.range.last.column &&
                table.range.first.column <= range.last.column &&
                (!table.supported || range.first.column != table.range.first.column ||
                    range.last.column != table.range.last.column || range.first.row < table.range.first.row ||
                    range.last.row > table.range.last.row ||
                    (range.first.row == table.range.first.row && !header)))
            {
                setError(
                    QStringLiteral("表格排序需包含全部列，且不能混入表外区域；包含表头时请勾选首行为表头。"));
                return false;
            }
        const auto first = range.first.row + (header ? 1U : 0U);
        if (first >= range.last.row)
        {
            setError(QStringLiteral("请选择至少两行数据；含表头时请再多选一行。"));
            return false;
        }
        struct Row
        {
            std::uint32_t source = 0;
            SpreadsheetValue key;
            std::vector<SpreadsheetValue> values;
            std::vector<SpreadsheetFormat> formats;
        };
        std::vector<Row> rows;
        for (auto row = first; row <= range.last.row; ++row)
        {
            Row values;
            values.source = row;
            for (auto column = range.first.column; column <= range.last.column; ++column)
            {
                const auto cell = spreadsheet_cell(document_, sheet_, {row, column});
                if (!cell.editable || (cell.formula_cell && !cell.formula_supported))
                {
                    setError(QStringLiteral("排序区域包含不支持的公式、合并或只读内容，未进行修改。"));
                    return false;
                }
                values.values.push_back(spreadsheet_source_value(document_, sheet_, {row, column}));
                values.formats.push_back(spreadsheet_cell_direct_format(document_, sheet_, {row, column}));
                if (column == range.first.column)
                    values.key = cell.value;
            }
            rows.push_back(std::move(values));
        }
        std::stable_sort(rows.begin(), rows.end(), [descending](const auto& left, const auto& right)
        {
            const auto& a = left.key;
            const auto& b = right.key;
            if (a.kind == SpreadsheetValueKind::Empty || b.kind == SpreadsheetValueKind::Empty)
                return a.kind != SpreadsheetValueKind::Empty && b.kind == SpreadsheetValueKind::Empty;
            int order = 0;
            if (a.kind != b.kind)
                order = a.kind < b.kind ? -1 : 1;
            else if (a.kind == SpreadsheetValueKind::Number)
            {
                const double x = QString::fromStdString(a.text).toDouble();
                const double y = QString::fromStdString(b.text).toDouble();
                order = x < y ? -1 : x > y ? 1 : 0;
            }
            else
                order = QString::localeAwareCompare(
                    QString::fromStdString(a.text), QString::fromStdString(b.text));
            return descending ? order > 0 : order < 0;
        });
        std::vector<SpreadsheetEditCommand> commands;
        std::vector<SpreadsheetFormatCommand> formats;
        for (std::size_t row = 0; row < rows.size(); ++row)
            for (std::size_t column = 0; column < rows[row].values.size(); ++column)
            {
                const SpreadsheetAddress target{
                    first + static_cast<unsigned>(row), range.first.column + static_cast<unsigned>(column)};
                auto value = rows[row].values[column];
                if (value.kind == SpreadsheetValueKind::Formula)
                {
                    const auto translated =
                        spreadsheet_translate_formula(value.text, int(target.row) - int(rows[row].source), 0);
                    if (!translated)
                        return false;
                    value.text = *translated;
                }
                commands.push_back({static_cast<std::size_t>(sheet_), target, std::move(value)});
                auto format = rows[row].formats[column];
                format["reset"] = "1";
                formats.push_back({static_cast<std::size_t>(sheet_), target, std::move(format)});
            }
        return commitChanges(commands, formats, {});
    }

    bool SpreadsheetBridge::insertTemplate(const QString& name)
    {
        if (!active_ || locked())
            return false;
        QString text;
        if (name == "references")
        {
            text = QStringLiteral("题名\t作者\t来源 / 期刊\t年份\tDOI / 链接\t备注");
            text += QStringLiteral("\n待填写\t待填写\t待填写\t待填写\t待填写\t阅读结论");
        }
        else if (name == "tasks")
            text = QStringLiteral(
                "事项\t负责人\t截止日期\t状态\t下一步\n待办事项\t待指定\t待指定\t未开始\t待填写");
        else if (name == "budget")
            text = QStringLiteral("项目\t预算金额\t实际金额\t说明\n待填写\t0\t0\t待填写");
        else
        {
            setError(QStringLiteral("未知的表格模板。"));
            return false;
        }
        QVector<QStringList> rows;
        QString error;
        if (!spreadsheet_paste_rows(text, rows, error))
            return false;
        const auto range = selectedRange();
        for (qsizetype row = 0; row < rows.size(); ++row)
            for (qsizetype column = 0; column < rows[row].size(); ++column)
            {
                const auto cell = spreadsheet_cell(document_, sheet_,
                    {range.first.row + static_cast<unsigned>(row),
                        range.first.column + static_cast<unsigned>(column)});
                if (!cell.editable || cell.formula_cell || cell.value.kind != SpreadsheetValueKind::Empty)
                {
                    setError(QStringLiteral("模板需要一块空白区域，请先选择空白单元格。"));
                    return false;
                }
            }
        return pasteText(text);
    }

    QVariantMap SpreadsheetBridge::snapshot() const
    {
        QVariantList cells;
        std::size_t bytes = 0;
        bool truncated = false;
        if (active_)
        {
            const auto range = selectedRange();
            for (auto row = range.first.row; row <= range.last.row; ++row)
                for (auto column = range.first.column; column <= range.last.column; ++column)
                {
                    const auto cell = spreadsheet_cell(document_, sheet_, {row, column});
                    if (bytes + cell.value.text.size() + cell.formula.size() > 1024 * 1024)
                    {
                        truncated = true;
                        continue;
                    }
                    bytes += cell.value.text.size() + cell.formula.size();
                    cells.push_back(
                        QVariantMap{{"address", QString::fromStdString(spreadsheet_address({row, column}))},
                            {"text", QString::fromStdString(cell.value.text)},
                            {"formula", QString::fromStdString(cell.formula)}, {"editable", cell.editable}});
                }
        }
        return {{"active", active_}, {"locked", locked()}, {"modified", modified()}, {"name", documentName()},
            {"sheet", sheet_}, {"range", rangeInfo()}, {"format", formatInfo()}, {"layout", layoutInfo()},
            {"cells", cells}, {"truncated", truncated}, {"revision", QVariant::fromValue(revision_)}};
    }
}
