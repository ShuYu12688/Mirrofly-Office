#include "spreadsheet_bridge.hpp"
#include "spreadsheet_input.hpp"
#include "spreadsheet_limits.hpp"
#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    bool SpreadsheetBridge::commitFeatures(const SpreadsheetFeatures& features)
    {
        if (!active_ || locked())
            return false;
        HistoryEntry entry;
        entry.sheet = sheet_;
        entry.address = anchor_;
        entry.last = cell_;
        entry.before_generation = generation_;
        entry.after_generation = next_generation_;
        try
        {
            entry.features_before = spreadsheet_features(document_, sheet_);
            entry.features_after = features;
            if (historyBytes(entry) > maximum_spreadsheet_history_bytes)
                return false;
            undo_.push_back(entry);
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("撤销记录超过内存预算。"));
            return false;
        }
        const auto result = apply_spreadsheet_features(document_, sheet_, features);
        if (result.error != SpreadsheetError::None || !result.changed)
        {
            undo_.pop_back();
            setError(QString::fromStdString(result.message));
            return result.error == SpreadsheetError::None;
        }
        generation_ = next_generation_++;
        redo_.clear();
        trimHistory();
        ++layout_revision_;
        model_.refresh();
        clearError();
        emit documentChanged();
        emit cellChanged();
        emit stateChanged();
        return true;
    }

    QVariantMap SpreadsheetBridge::layoutInfo() const
    {
        QVariantMap result;
        if (!active_ || sheet_ < 0 || static_cast<std::size_t>(sheet_) >= document_.sheets.size())
            return result;
        QVariantList sheets, hidden_indices, hidden_sheets;
        for (std::size_t index = 0; index < document_.sheets.size(); ++index)
        {
            const auto& sheet = document_.sheets[index];
            QVariantMap item{{"index", static_cast<int>(index)}, {"name", QString::fromStdString(sheet.name)},
                {"hidden", sheet.hidden}};
            sheets.push_back(item);
            if (sheet.hidden)
            {
                hidden_indices.push_back(static_cast<int>(index));
                hidden_sheets.push_back(item);
            }
        }
        result.insert("sheets", sheets);
        result.insert("hiddenSheetIndices", hidden_indices);
        result.insert("hiddenSheets", hidden_sheets);
        if (pending_cut_ && pending_cut_->generation == generation_)
            result.insert("pendingCut",
                QString::fromStdString(document_.sheets[pending_cut_->sheet].name + "!" +
                    spreadsheet_address(pending_cut_->range.first) + ":" +
                    spreadsheet_address(pending_cut_->range.last)));
        const auto& f = spreadsheet_features(document_, sheet_);
        result.insert("frozenRows", f.frozen_rows);
        result.insert("frozenColumns", f.frozen_columns);
        result.insert("filtered", f.filter.has_value());
        if (f.filter)
            result.insert("filterRange",
                QString::fromStdString(
                    spreadsheet_address(f.filter->first) + ":" + spreadsheet_address(f.filter->last)));
        QVariantList filters;
        for (const auto& filter : f.filters)
        {
            QStringList values;
            for (const auto& value : filter.values)
                values.push_back(QString::fromStdString(value));
            filters.push_back(QVariantMap{{"column", filter.column},
                {"operator", QString::fromStdString(filter.comparison)}, {"values", values}});
        }
        result.insert("filters", filters);
        QVariantList tables;
        QVariantMap current_table;
        for (const auto& table : f.tables)
        {
            QVariantMap info{{"name", QString::fromStdString(table.name)},
                {"range",
                    QString::fromStdString(spreadsheet_address(table.range.first) + ":" +
                        spreadsheet_address(table.range.last))},
                {"rowStripes", table.row_stripes}, {"columnStripes", table.column_stripes},
                {"firstColumn", table.first_column}, {"lastColumn", table.last_column},
                {"supported", table.supported}};
            tables.push_back(info);
            if (cell_.row >= table.range.first.row && cell_.row <= table.range.last.row &&
                cell_.column >= table.range.first.column && cell_.column <= table.range.last.column)
                current_table = info;
        }
        result.insert("tables", tables);
        result.insert("currentTable", current_table);
        result.insert("tablesSupported", f.tables_supported);
        result.insert("formatReady", !format_sample_.empty());
        const auto position = [&](bool column, std::uint32_t index)
        {
            double value = index * (column ? 132.0 : 34.0);
            std::set<std::uint32_t> different = column ? f.hidden_columns : f.hidden_rows;
            const auto& source =
                column ? document_.sheets[sheet_].column_widths : document_.sheets[sheet_].row_heights;
            for (const auto& item : source)
                different.insert(item.first);
            const auto& changes = column ? document_.column_edits : document_.row_edits;
            auto found = changes.find(sheet_);
            if (found != changes.end())
                for (const auto& item : found->second)
                    different.insert(item.first);
            if (!column && f.filter)
                for (auto y = f.filter->first.row + 1; y <= f.filter->last.row; ++y)
                    different.insert(y);
            for (auto i : different)
                if (i < index)
                    value += (column ? columnWidth(i) - 132.0 : rowHeight(i) - 34.0);
            return value;
        };
        result.insert("frozenWidth", position(true, f.frozen_columns));
        result.insert("frozenHeight", position(false, f.frozen_rows));
        QVariantList column_widths, row_heights;
        for (std::uint32_t i = 0; i < f.frozen_columns; ++i)
            column_widths.push_back(columnWidth(i));
        for (std::uint32_t i = 0; i < f.frozen_rows; ++i)
            row_heights.push_back(rowHeight(i));
        result.insert("frozenColumnWidths", column_widths);
        result.insert("frozenRowHeights", row_heights);
        QVariantList merges;
        for (auto r : f.merges)
        {
            const auto x = position(true, r.first.column);
            const auto y = position(false, r.first.row);
            const auto format =
                model_.data(model_.index(r.first.row, r.first.column), Qt::UserRole + 3).toMap();
            merges.push_back(QVariantMap{{"row", r.first.row}, {"column", r.first.column},
                {"lastRow", r.last.row}, {"lastColumn", r.last.column}, {"x", x}, {"y", y},
                {"width", position(true, r.last.column + 1) - x},
                {"height", position(false, r.last.row + 1) - y},
                {"text", model_.data(model_.index(r.first.row, r.first.column), Qt::DisplayRole)},
                {"format", format}});
        }
        result.insert("merges", merges);
        return result;
    }

    bool SpreadsheetBridge::startTool(const QString& action, const QVariantMap& args)
    {
        if (!active_ || locked())
            return false;
        static const QMap<QString, QStringList> parameters{{"copyFormat", {}}, {"cut", {}},
            {"pasteValues", {}}, {"pasteCellFormat", {}}, {"merge", {}}, {"unmerge", {}},
            {"freeze", {"rows", "columns"}}, {"unfreeze", {}},
            {"filter", {"column", "operator", "value", "value2", "values"}},
            {"clearFilterColumn", {"column"}}, {"clearFilter", {}},
            {"condition", {"operator", "value", "fill"}}, {"clearConditions", {}}, {"hideRows", {}},
            {"hideColumns", {}}, {"showAll", {}}, {"aggregate", {"kind"}}, {"fillDown", {}},
            {"fillRight", {}}, {"sequence", {}}, {"convert", {"kind"}}, {"replace", {"query", "replacement"}},
            {"pasteFormat", {}}, {"clearFormat", {}}, {"clearAll", {}}, {"grow", {}}, {"shrink", {}},
            {"autoWidth", {}}, {"border", {"kind", "style", "color"}},
            {"tableStyle", {"name", "palette", "rowStripes", "columnStripes", "firstColumn", "lastColumn"}},
            {"tableRange", {"name"}}, {"removeTable", {"name"}}, {"insertRows", {"count", "after"}},
            {"insertColumns", {"count", "after"}}, {"deleteRows", {"count"}}, {"deleteColumns", {"count"}},
            {"addSheet", {"name"}}, {"renameSheet", {"name"}}, {"copySheet", {"name"}}, {"deleteSheet", {}},
            {"moveSheet", {"index"}}, {"hideSheet", {}}, {"showSheet", {"index"}}};
        const auto allowed = parameters.constFind(action);
        if (allowed == parameters.cend())
            return false;
        for (auto item = args.cbegin(); item != args.cend(); ++item)
            if (!allowed.value().contains(item.key()))
                return false;
        const auto r = selectedRange();
        const auto sheet = static_cast<std::size_t>(sheet_);
        auto features = spreadsheet_features(document_, sheet);
        if (action.endsWith("Sheet"))
            return applySheetTool(action, args);
        if (action == "insertRows" || action == "insertColumns" || action == "deleteRows" ||
            action == "deleteColumns")
            return applyAxisTool(action, args);
        if (action == "tableStyle" || action == "tableRange" || action == "removeTable")
            return applyTableTool(action, args);
        if (((action == "filter" || action == "clearFilter" || action == "clearFilterColumn") &&
                !features.filter_supported) ||
            ((action == "condition" || action == "clearConditions") && !features.conditions_supported) ||
            ((action == "freeze" || action == "unfreeze") && !features.panes_supported))
        {
            setError(QStringLiteral("此导入文件的相应设置较复杂，当前保留原设置，不能在这里覆盖。"));
            return false;
        }
        if (action == "copyFormat")
        {
            format_sample_ = spreadsheet_cell_format(document_, sheet, cell_);
            emit documentChanged();
            return true;
        }
        if (action == "cut")
            return copyCells(true);
        if (action == "pasteValues" || action == "pasteCellFormat")
            return pasteCells(action == "pasteValues" ? "values" : "formats");
        if (action == "merge")
        {
            if (r.first.row == r.last.row && r.first.column == r.last.column)
                return false;
            features.merges.push_back(r);
            return commitFeatures(features);
        }
        if (action == "unmerge")
        {
            const auto removed = std::remove_if(features.merges.begin(), features.merges.end(), [&](auto m)
            {
                return r.first.row <= m.last.row && m.first.row <= r.last.row &&
                    r.first.column <= m.last.column && m.first.column <= r.last.column;
            });
            features.merges.erase(removed, features.merges.end());
            return commitFeatures(features);
        }
        if (action == "freeze" || action == "unfreeze")
        {
            bool rows_ok = false, columns_ok = false;
            const auto rows = args.value("rows", 0).toInt(&rows_ok);
            const auto columns = args.value("columns", 0).toInt(&columns_ok);
            if (!rows_ok || !columns_ok || rows < 0 || rows > 16 || columns < 0 || columns > 8)
                return false;
            features.frozen_rows = action == "freeze" ? rows : 0;
            features.frozen_columns = action == "freeze" ? columns : 0;
            for (auto m : features.merges)
                if ((m.first.row < features.frozen_rows && m.last.row >= features.frozen_rows) ||
                    (m.first.column < features.frozen_columns && m.last.column >= features.frozen_columns))
                {
                    setError(QStringLiteral("冻结线不能穿过合并单元格，请先取消合并。"));
                    return false;
                }
            return commitFeatures(features);
        }
        if (action == "filter" || action == "clearFilter" || action == "clearFilterColumn")
        {
            if (action != "clearFilter")
            {
                bool ok = false;
                auto column = args.value("column", static_cast<int>(r.first.column)).toInt(&ok);
                if (!ok || column < 0)
                    return false;
                if (action == "filter")
                {
                    if (!features.filter || features.filter->first.row != r.first.row ||
                        features.filter->first.column != r.first.column ||
                        features.filter->last.row != r.last.row ||
                        features.filter->last.column != r.last.column)
                        features.filters.clear();
                    features.filter = r;
                }
                const auto removed =
                    std::remove_if(features.filters.begin(), features.filters.end(), [&](const auto& filter)
                {
                    return filter.column == static_cast<std::uint32_t>(column);
                });
                features.filters.erase(removed, features.filters.end());
                if (action == "filter")
                {
                    SpreadsheetFilter filter;
                    filter.column = column;
                    filter.comparison = args.value("operator", "equal").toString().toStdString();
                    QStringList values;
                    if (args.contains("values"))
                    {
                        if (args.contains("value") || args.contains("value2") ||
                            !args.value("values").canConvert<QStringList>())
                            return false;
                        values = args.value("values").toStringList();
                    }
                    else
                    {
                        values.push_back(args.value("value").toString());
                        if (filter.comparison == "between")
                            values.push_back(args.value("value2").toString());
                        else if (args.contains("value2"))
                            return false;
                    }
                    for (const auto& value : values)
                    {
                        if (!value.isValidUtf16())
                            return false;
                        filter.values.push_back(value.toStdString());
                    }
                    features.filters.push_back(std::move(filter));
                    std::sort(features.filters.begin(), features.filters.end(),
                        [](const auto& left, const auto& right)
                    {
                        return left.column < right.column;
                    });
                }
            }
            else
            {
                features.filter.reset();
                features.filters.clear();
            }
            return commitFeatures(features);
        }
        if (action == "condition" || action == "clearConditions")
        {
            if (action == "clearConditions")
                features.conditions.clear();
            else
            {
                if (document_.styles_path.empty())
                {
                    setError(QStringLiteral("请先保存一次单元格样式，再设置条件格式。"));
                    return false;
                }
                bool ok = false;
                const auto value = args.value("value").toDouble(&ok);
                if (!ok || !std::isfinite(value))
                    return false;
                features.conditions.push_back({r, args.value("operator").toString().toStdString(), value,
                    args.value("fill").toString().toStdString()});
            }
            return commitFeatures(features);
        }
        if (action == "hideRows" || action == "hideColumns" || action == "showAll")
        {
            if (action == "showAll")
            {
                features.hidden_rows.clear();
                features.hidden_columns.clear();
            }
            else
            {
                const bool rows = action == "hideRows";
                auto& hidden = rows ? features.hidden_rows : features.hidden_columns;
                const auto last = rows ? r.last.row : r.last.column;
                for (auto i = rows ? r.first.row : r.first.column; i <= last; ++i)
                    hidden.insert(i);
            }
            return commitFeatures(features);
        }
        if (std::uint64_t(r.last.row - r.first.row + 1) * (r.last.column - r.first.column + 1) >
            maximum_spreadsheet_batch_cells)
            return false;
        std::vector<SpreadsheetEditCommand> cells;
        std::vector<SpreadsheetFormatCommand> formats;
        std::vector<SpreadsheetDimensionCommand> dimensions;
        if (action == "border")
        {
            const auto kind = args.value("kind").toString();
            if (!QStringList{"all", "outer", "inner", "horizontal", "vertical", "top", "bottom", "left",
                    "right", "none"}
                    .contains(kind))
                return false;
            for (const auto& merge : features.merges)
                if (r.first.row <= merge.last.row && merge.first.row <= r.last.row &&
                    r.first.column <= merge.last.column && merge.first.column <= r.last.column &&
                    (r.first.row > merge.first.row || r.last.row < merge.last.row ||
                        r.first.column > merge.first.column || r.last.column < merge.last.column))
                {
                    setError(QStringLiteral("请完整选中合并单元格后设置边框。"));
                    return false;
                }
            for (auto y = r.first.row; y <= r.last.row; ++y)
                for (auto x = r.first.column; x <= r.last.column; ++x)
                {
                    SpreadsheetRange bounds{{y, x}, {y, x}};
                    for (const auto& merge : features.merges)
                        if (merge.first.row == y && merge.first.column == x)
                            bounds = merge;
                    SpreadsheetFormat patch;
                    const auto existing = document_.format_edits.find(sheet);
                    if (existing != document_.format_edits.end())
                    {
                        const auto cell = existing->second.find({y, x});
                        if (cell != existing->second.end())
                            patch = cell->second;
                    }
                    const std::vector<std::pair<std::string, bool>> edges{
                        {"Left", bounds.first.column == r.first.column},
                        {"Right", bounds.last.column == r.last.column},
                        {"Top", bounds.first.row == r.first.row}, {"Bottom", bounds.last.row == r.last.row}};
                    bool changed = false;
                    for (const auto& [edge, outer] : edges)
                    {
                        const auto name = QString::fromStdString(edge).toLower();
                        const bool horizontal = edge == "Top" || edge == "Bottom";
                        const bool apply = kind == "all" || kind == "none" || (kind == "outer" && outer) ||
                            (kind == "inner" && !outer) || (kind == "horizontal" && horizontal && !outer) ||
                            (kind == "vertical" && !horizontal && !outer) || (kind == name && outer);
                        if (!apply)
                            continue;
                        patch["border" + edge] =
                            kind == "none" ? "none" : args.value("style", "thin").toString().toStdString();
                        patch["border" + edge + "Color"] =
                            args.value("color", "#68717A").toString().toStdString();
                        changed = true;
                    }
                    if (changed)
                        formats.push_back({sheet, {y, x}, std::move(patch)});
                }
            return commitChanges({}, formats, {});
        }
        if (action == "aggregate")
        {
            const auto kind = args.value("kind").toString();
            if (kind != "sum" && kind != "average" && kind != "count" && kind != "min" && kind != "max")
                return false;
            if (r.last.row + 1 >= maximum_spreadsheet_rows)
                return false;
            for (auto x = r.first.column; x <= r.last.column; ++x)
            {
                const auto target = SpreadsheetAddress{r.last.row + 1, x};
                if (spreadsheet_cell(document_, sheet, target).value.kind != SpreadsheetValueKind::Empty)
                {
                    setError(QStringLiteral("汇总结果写入选区下方，请先留出空白行。"));
                    return false;
                }
                const auto formula = kind.toUpper().toStdString() + "(" +
                    spreadsheet_address({r.first.row, x}) + ":" + spreadsheet_address({r.last.row, x}) + ")";
                cells.push_back({sheet, target, {SpreadsheetValueKind::Formula, formula}});
            }
            return commitCells(cells);
        }
        const auto query = args.value("query").toString();
        const auto replacement = args.value("replacement").toString();
        if (action == "replace" && (query.isEmpty() || !query.isValidUtf16() || !replacement.isValidUtf16()))
            return false;
        for (auto y = r.first.row; y <= r.last.row; ++y)
            for (auto x = r.first.column; x <= r.last.column; ++x)
            {
                SpreadsheetAddress a{y, x};
                const auto cell = spreadsheet_cell(document_, sheet, a);
                if (action == "fillDown" || action == "fillRight" || action == "sequence")
                {
                    const auto source = SpreadsheetAddress{
                        action == "fillRight" ? y : r.first.row, action == "fillRight" ? r.first.column : x};
                    auto value = spreadsheet_source_value(document_, sheet, source);
                    if (value.kind == SpreadsheetValueKind::Formula)
                    {
                        const auto formula = spreadsheet_translate_formula(
                            value.text, int(y) - int(source.row), int(x) - int(source.column));
                        if (!formula)
                        {
                            setError(QStringLiteral("此公式尚不支持安全调整引用，未进行填充。"));
                            return false;
                        }
                        value.text = *formula;
                    }
                    if (action == "sequence")
                    {
                        if (value.kind != SpreadsheetValueKind::Number)
                        {
                            setError(QStringLiteral("递增序列需要选区首行为数字。"));
                            return false;
                        }
                        const auto n = QString::fromStdString(value.text).toDouble() + (y - r.first.row);
                        if (!std::isfinite(n))
                            return false;
                        value.text = QString::number(n, 'g', 17).toStdString();
                    }
                    cells.push_back({sheet, a, value});
                    formats.push_back({sheet, a, spreadsheet_cell_format(document_, sheet, source)});
                }
                else if (action == "convert")
                {
                    if (cell.value.kind == SpreadsheetValueKind::Empty)
                        continue;
                    SpreadsheetValue value;
                    QString error;
                    if (!spreadsheet_input_value(QString::fromStdString(cell.value.text),
                            args.value("kind").toString(), value, error))
                    {
                        setError(error);
                        return false;
                    }
                    cells.push_back({sheet, a, value});
                }
                else if (action == "replace")
                {
                    if (cell.value.kind != SpreadsheetValueKind::Text || cell.formula_cell)
                        continue;
                    const auto text = QString::fromStdString(cell.value.text);
                    if (text.contains(query))
                        cells.push_back({sheet, a,
                            {SpreadsheetValueKind::Text,
                                QString(text).replace(query, replacement).toStdString()}});
                }
                else if (action == "pasteFormat")
                {
                    if (format_sample_.empty())
                        return false;
                    formats.push_back({sheet, a, format_sample_});
                }
                else if (action == "clearFormat" || action == "clearAll")
                {
                    formats.push_back({sheet, a, {{"reset", "1"}}});
                    if (action == "clearAll")
                        cells.push_back({sheet, a, {}});
                }
                else if (action == "grow" || action == "shrink")
                {
                    auto format = spreadsheet_cell_format(document_, sheet, a);
                    format["size"] =
                        QString::number(std::clamp(QString::fromStdString(format["size"]).toDouble() +
                                                (action == "grow" ? 1 : -1),
                                            6.0, 96.0))
                            .toStdString();
                    formats.push_back({sheet, a, std::move(format)});
                }
                else if (action != "autoWidth")
                    return false;
            }
        if (action == "autoWidth")
        {
            for (auto x = r.first.column; x <= r.last.column; ++x)
            {
                double pixels = 28;
                for (auto y = r.first.row; y <= r.last.row; ++y)
                {
                    const auto f = spreadsheet_cell_format(document_, sheet, {y, x});
                    QFont font;
                    if (f.count("font"))
                        font.setFamily(QString::fromStdString(f.at("font")));
                    font.setPointSizeF(QString::fromStdString(f.at("size")).toDouble());
                    font.setBold(f.at("bold") == "1");
                    pixels = std::max(pixels,
                        QFontMetricsF(font).horizontalAdvance(
                            model_.data(model_.index(y, x), Qt::DisplayRole).toString()) +
                            16);
                }
                dimensions.push_back({sheet, true, x, std::clamp((pixels - 6) / 7, 3.0, 80.0)});
            }
        }
        if (cells.empty() && formats.empty() && dimensions.empty())
        {
            clearError();
            return true;
        }
        return commitChanges(cells, formats, dimensions);
    }
}
