#include "spreadsheet_bridge.hpp"
#include "spreadsheet_limits.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    using namespace mirrorfly;
    std::size_t format_bytes(const SpreadsheetFormat& format)
    {
        std::size_t bytes = sizeof(format);
        for (const auto& [key, value] : format)
            bytes += 128 + key.size() + value.size();
        return bytes;
    }
    std::size_t feature_bytes(const SpreadsheetFeatures& features)
    {
        std::size_t bytes = sizeof(features) + features.merges.size() * sizeof(SpreadsheetRange) +
            (features.hidden_rows.size() + features.hidden_columns.size()) * 64;
        for (const auto& rule : features.conditions)
            bytes += sizeof(rule) + rule.comparison.size() + rule.fill.size();
        for (const auto& filter : features.filters)
        {
            bytes += sizeof(filter) + filter.comparison.size();
            for (const auto& value : filter.values)
                bytes += sizeof(value) + value.size();
        }
        for (const auto& table : features.tables)
        {
            bytes += sizeof(table) + table.path.size() + table.name.size();
            for (const auto& [name, format] : table.style)
                bytes += 128 + name.size() + format_bytes(format);
        }
        return bytes;
    }
    std::size_t document_bytes(const SpreadsheetDocument& document)
    {
        std::size_t bytes = sizeof(document) + document.styles_path.size() + document.workbook_path.size() +
            document.read_only_reason.size();
        if (document.original_parts)
            for (const auto& part : *document.original_parts)
                bytes += sizeof(part) + part.path.size() + part.bytes.size();
        for (const auto& sheet : document.sheets)
        {
            bytes += sizeof(sheet) + sheet.path.size() + sheet.name.size() + sheet.read_only_reason.size() +
                feature_bytes(sheet.features) + sheet.protected_ranges.size() * sizeof(SpreadsheetRange) +
                (sheet.column_widths.size() + sheet.row_heights.size()) * 80;
            for (const auto& [address, cell] : sheet.cells)
            {
                (void)address;
                bytes += sizeof(cell) + 80 + cell.value.text.size() + cell.formula.size() +
                    cell.read_only_reason.size();
            }
        }
        for (const auto& [sheet, edits] : document.edits)
        {
            (void)sheet;
            bytes += 80;
            for (const auto& [address, value] : edits)
            {
                (void)address;
                bytes += sizeof(value) + 80 + value.text.size();
            }
        }
        for (const auto& [sheet, edits] : document.format_edits)
        {
            (void)sheet;
            bytes += 80;
            for (const auto& [address, format] : edits)
            {
                (void)address;
                bytes += 80 + format_bytes(format);
            }
        }
        for (const auto* edits : {&document.column_edits, &document.row_edits})
            for (const auto& [sheet, dimensions] : *edits)
            {
                (void)sheet;
                bytes += 80 + dimensions.size() * 80;
            }
        for (const auto& [sheet, features] : document.feature_edits)
        {
            (void)sheet;
            bytes += 80 + feature_bytes(features);
        }
        for (const auto& format : document.styles)
            bytes += format_bytes(format);
        for (const auto& properties : document.style_properties)
            for (const auto& key : properties)
                bytes += 80 + key.size();
        return bytes;
    }
}

namespace mirrorfly
{
    bool SpreadsheetBridge::applySheetTool(const QString& action, const QVariantMap& args)
    {
        if (!active_ || locked())
            return false;
        if (action == "addSheet")
        {
            if (args.contains("name") && args.value("name").metaType().id() != QMetaType::QString)
                return false;
            return addSheet(args.value("name").toString());
        }
        SpreadsheetSheetCommand command;
        command.index = static_cast<std::size_t>(sheet_);
        if (action == "renameSheet" || action == "copySheet")
        {
            command.action =
                action == "renameSheet" ? SpreadsheetSheetAction::Rename : SpreadsheetSheetAction::Copy;
            if (args.contains("name") && args.value("name").metaType().id() != QMetaType::QString)
                return false;
            auto name = args.value("name").toString().trimmed();
            if (action == "copySheet" && name.isEmpty())
            {
                int number = 1;
                do
                {
                    name = QStringLiteral("工作表副本%1").arg(number++);
                } while (sheetNames().contains(name, Qt::CaseInsensitive));
            }
            if (!name.isValidUtf16())
                return false;
            const auto names = sheetNames();
            for (int index = 0; index < names.size(); ++index)
                if ((action == "copySheet" || index != sheet_) &&
                    names[index].compare(name, Qt::CaseInsensitive) == 0)
                    return false;
            command.name = name.toStdString();
        }
        else if (action == "deleteSheet")
            command.action = SpreadsheetSheetAction::Delete;
        else if (action == "hideSheet")
            command.action = SpreadsheetSheetAction::Hide;
        else if (action == "showSheet" || action == "moveSheet")
        {
            bool ok = false;
            const auto value = args.value("index");
            if (value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString)
                return false;
            const auto index = value.toDouble(&ok);
            if (!ok || !std::isfinite(index) || index < 0 || index >= document_.sheets.size() ||
                std::floor(index) != index)
                return false;
            if (action == "showSheet")
            {
                command.action = SpreadsheetSheetAction::Show;
                command.index = static_cast<std::size_t>(index);
            }
            else
            {
                command.action = SpreadsheetSheetAction::Move;
                command.destination = static_cast<std::size_t>(index);
            }
        }
        else
            return false;
        try
        {
            auto proposed = document_;
            const auto result = apply_spreadsheet_sheet_command(proposed, command);
            if (result.error != SpreadsheetError::None)
            {
                setError(QString::fromStdString(result.message));
                return false;
            }
            if (!result.changed)
                return true;
            auto selected = command.index;
            if (command.action == SpreadsheetSheetAction::Copy)
                ++selected;
            else if (command.action == SpreadsheetSheetAction::Move)
                selected = command.destination;
            if (selected >= proposed.sheets.size() || proposed.sheets[selected].hidden)
            {
                selected = 0;
                while (selected < proposed.sheets.size() && proposed.sheets[selected].hidden)
                    ++selected;
            }
            return commitDocument(std::move(proposed), selected);
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("工作表操作内存不足，原工作簿已保留。"));
            return false;
        }
    }

    bool SpreadsheetBridge::commitDocument(SpreadsheetDocument proposed, std::size_t sheet)
    {
        if (!active_ || locked() || sheet >= proposed.sheets.size())
            return false;
        HistoryEntry entry;
        entry.sheet = sheet;
        entry.previous_sheet = sheet_;
        entry.address = anchor_;
        entry.last = cell_;
        entry.before_generation = generation_;
        entry.after_generation = next_generation_;
        entry.document_bytes = document_bytes(document_) + document_bytes(proposed);
        if (historyBytes(entry) > maximum_spreadsheet_history_bytes)
        {
            setError(QStringLiteral("此次结构操作需要的撤销记录超过 32 MiB，原工作簿已保留。"));
            return false;
        }
        try
        {
            entry.document_before = std::make_shared<const SpreadsheetDocument>(document_);
            entry.document_after = std::make_shared<const SpreadsheetDocument>(proposed);
            undo_.push_back(std::move(entry));
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("无法分配完整的撤销记录，原工作簿已保留。"));
            return false;
        }
        document_ = std::move(proposed);
        generation_ = next_generation_++;
        redo_.clear();
        trimHistory();
        sheet_ = static_cast<int>(sheet);
        const auto& current = document_.sheets[sheet];
        cell_.row = std::min(cell_.row, current.rows - 1);
        cell_.column = std::min(cell_.column, current.columns - 1);
        anchor_.row = std::min(anchor_.row, current.rows - 1);
        anchor_.column = std::min(anchor_.column, current.columns - 1);
        ++layout_revision_;
        model_.setDocument(&document_, sheet);
        clearError();
        emit documentChanged();
        emit cellChanged();
        emit stateChanged();
        return true;
    }
    bool SpreadsheetBridge::applyAxisTool(const QString& action, const QVariantMap& args)
    {
        const auto range = selectedRange();
        const bool column = action.endsWith("Columns");
        const bool insert = action.startsWith("insert");
        const auto first = column ? range.first.column : range.first.row;
        const auto last = column ? range.last.column : range.last.row;
        bool converted = false;
        const auto value = args.value("count", last - first + 1);
        if (value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString)
            return false;
        const auto requested = value.toDouble(&converted);
        if (!converted || !std::isfinite(requested) || requested < 1 || requested > 4096 ||
            std::floor(requested) != requested ||
            (args.contains("after") && args.value("after").metaType().id() != QMetaType::Bool))
            return false;
        SpreadsheetAxisCommand command{static_cast<std::size_t>(sheet_), column, insert,
            insert && args.value("after").toBool() ? last + 1 : first, static_cast<std::uint32_t>(requested)};
        try
        {
            auto proposed = document_;
            const auto result = apply_spreadsheet_axis_command(proposed, command);
            if (result.error != SpreadsheetError::None)
            {
                setError(QString::fromStdString(result.message));
                return false;
            }
            return !result.changed || commitDocument(std::move(proposed), sheet_);
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("行列操作内存不足，原工作簿已保留。"));
            return false;
        }
    }
}
