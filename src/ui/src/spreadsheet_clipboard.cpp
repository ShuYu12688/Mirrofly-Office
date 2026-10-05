#include "spreadsheet_bridge.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUuid>
#include <cmath>

namespace
{
    const QString mime_type = QStringLiteral("application/x-mirrorfly-cells-v1");

    int integer(const QJsonValue& value, int maximum)
    {
        const auto n = value.toDouble(-1);
        return value.isDouble() && std::isfinite(n) && n >= 0 && n <= maximum && std::floor(n) == n ? int(n)
                                                                                                    : -1;
    }

    QJsonObject encoded(const mirrorfly::SpreadsheetValue& value)
    {
        return {{"kind", static_cast<int>(value.kind)}, {"text", QString::fromStdString(value.text)}};
    }

    bool decoded(const QJsonValue& value, mirrorfly::SpreadsheetValue& result)
    {
        const auto object = value.toObject();
        const auto kind =
            integer(object.value("kind"), static_cast<int>(mirrorfly::SpreadsheetValueKind::Formula));
        const auto text = object.value("text");
        if (kind < 0 || !text.isString() || !text.toString().isValidUtf16() || text.toString().size() > 32767)
            return false;
        result = {static_cast<mirrorfly::SpreadsheetValueKind>(kind), text.toString().toStdString()};
        return true;
    }
}

namespace mirrorfly
{
    bool SpreadsheetBridge::copyCells(bool cut)
    {
        if (!active_ || locked())
            return false;
        const auto r = selectedRange();
        const auto height = r.last.row - r.first.row + 1;
        const auto width = r.last.column - r.first.column + 1;
        if (std::uint64_t(height) * width > maximum_spreadsheet_batch_cells)
            return false;
        const auto text = selectionText();
        if (text.isEmpty())
            return false;
        QJsonArray cells;
        QString html = QStringLiteral("<table>");
        for (auto row = r.first.row; row <= r.last.row; ++row)
        {
            html += "<tr>";
            for (auto column = r.first.column; column <= r.last.column; ++column)
            {
                const SpreadsheetAddress address{row, column};
                const auto cell = spreadsheet_cell(document_, sheet_, address);
                const auto source = spreadsheet_source_value(document_, sheet_, address);
                QJsonObject format;
                QString css;
                for (const auto& [key, value] : spreadsheet_cell_format(document_, sheet_, address))
                {
                    format.insert(QString::fromStdString(key), QString::fromStdString(value));
                    const auto v = QString::fromStdString(value);
                    if (key == "size")
                        css += "font-size:" + v + "pt;";
                    if (key == "bold" && value == "1")
                        css += "font-weight:bold;";
                    if (key == "italic" && value == "1")
                        css += "font-style:italic;";
                    if (key == "fill" && value != "none")
                        css += "background-color:" + v + ";";
                    if (key == "text")
                        css += "color:" + v + ";";
                }
                auto copied_value = cell.value;
                if (cell.formula_cell && !cell.formula_supported && document_.caches_stale)
                    copied_value = {SpreadsheetValueKind::Error, "#N/A"};
                cells.append(QJsonObject{
                    {"source", encoded(source)}, {"value", encoded(copied_value)}, {"format", format}});
                auto display = QString::fromStdString(copied_value.text);
                html += "<td style=\"" + css.toHtmlEscaped() + "\">" +
                    display.toHtmlEscaped().replace('\n', "<br>") + "</td>";
            }
            html += "</tr>";
        }
        html += "</table>";
        QJsonObject object{{"version", 1}, {"row", int(r.first.row)}, {"column", int(r.first.column)},
            {"height", int(height)}, {"width", int(width)}, {"cut", cut}, {"cells", cells}};
        if (cut)
            object.insert("token", QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (bytes.size() > 1024 * 1024 || html.toUtf8().size() > 1024 * 1024)
        {
            setError(QStringLiteral("复制的内容与格式超过 1 MiB，请缩小区域。"));
            return false;
        }
        auto data = std::make_unique<QMimeData>();
        data->setText(text);
        data->setHtml(html);
        data->setData(mime_type, bytes);
        pending_cut_.reset();
        if (cut)
            pending_cut_ = PendingCut{bytes, static_cast<std::size_t>(sheet_), r, generation_};
        QGuiApplication::clipboard()->setMimeData(data.release());
        clearError();
        emit documentChanged();
        return true;
    }

    bool SpreadsheetBridge::pasteCells(const QString& mode)
    {
        if (!active_ || locked())
            return false;
        const auto* mime = QGuiApplication::clipboard()->mimeData();
        if (!mime)
            return false;
        if (!mime->hasFormat(mime_type))
        {
            if (mode == "formats")
            {
                setError(QStringLiteral("剪贴板没有可读取的单元格格式。"));
                return false;
            }
            return pasteText(mime->text());
        }
        const auto fail = [&]()
        {
            setError(QStringLiteral("剪贴板的表格数据无效或超过范围，原内容已保留。"));
            return false;
        };
        const auto bytes = mime->data(mime_type);
        if (bytes.size() > 1024 * 1024)
            return fail();
        if (mode == "all" && pending_cut_ && pending_cut_->bytes == bytes)
        {
            if (pending_cut_->generation != generation_)
            {
                setError(QStringLiteral("剪切后工作簿已有修改，请重新选择源区域并剪切；原内容仍保留。"));
                return false;
            }
            const auto range = selectedRange();
            const auto cut = *pending_cut_;
            try
            {
                auto proposed = document_;
                const auto result = apply_spreadsheet_move(
                    proposed, {cut.sheet, cut.range, static_cast<std::size_t>(sheet_), range.first});
                if (result.error != SpreadsheetError::None)
                {
                    setError(QString::fromStdString(result.message));
                    return false;
                }
                if (result.changed && !commitDocument(std::move(proposed), sheet_))
                    return false;
                pending_cut_.reset();
                anchor_ = range.first;
                cell_ = {range.first.row + cut.range.last.row - cut.range.first.row,
                    range.first.column + cut.range.last.column - cut.range.first.column};
                emit documentChanged();
                emit cellChanged();
                return true;
            }
            catch (const std::bad_alloc&)
            {
                setError(QStringLiteral("剪切移动内存不足，原内容仍保留。"));
                return false;
            }
        }
        const auto object = QJsonDocument::fromJson(bytes).object();
        const int row = integer(object.value("row"), maximum_spreadsheet_rows - 1);
        const int column = integer(object.value("column"), maximum_spreadsheet_columns - 1);
        const int height = integer(object.value("height"), maximum_spreadsheet_batch_cells);
        const int width = integer(object.value("width"), maximum_spreadsheet_batch_cells);
        if (object.value("version").toInt() != 1 || row < 0 || column < 0 || height < 1 || width < 1 ||
            std::uint64_t(height) * width > maximum_spreadsheet_batch_cells)
            return fail();
        const auto items = object.value("cells").toArray();
        if (items.size() != height * width)
            return fail();
        const auto r = selectedRange();
        const bool fill = height == 1 && width == 1;
        const auto target_height = fill ? r.last.row - r.first.row + 1 : static_cast<unsigned>(height);
        const auto target_width = fill ? r.last.column - r.first.column + 1 : static_cast<unsigned>(width);
        if (std::uint64_t(target_height) * target_width > maximum_spreadsheet_batch_cells ||
            r.first.row + target_height > maximum_spreadsheet_rows ||
            r.first.column + target_width > maximum_spreadsheet_columns)
            return fail();
        std::vector<SpreadsheetEditCommand> cells;
        std::vector<SpreadsheetFormatCommand> formats;
        for (unsigned y = 0; y < target_height; ++y)
            for (unsigned x = 0; x < target_width; ++x)
            {
                const auto item = items[fill ? 0 : int(y * width + x)].toObject();
                const SpreadsheetAddress target{r.first.row + y, r.first.column + x};
                if (mode != "formats")
                {
                    SpreadsheetValue value;
                    if (!decoded(item.value(mode == "values" ? "value" : "source"), value) ||
                        (mode == "values" && value.kind == SpreadsheetValueKind::Formula))
                        return fail();
                    if (value.kind == SpreadsheetValueKind::Formula)
                    {
                        const auto translated = spreadsheet_translate_formula(value.text,
                            int(target.row) - row - (fill ? 0 : int(y)),
                            int(target.column) - column - (fill ? 0 : int(x)));
                        if (!translated)
                            return fail();
                        value.text = *translated;
                    }
                    cells.push_back({static_cast<std::size_t>(sheet_), target, std::move(value)});
                }
                if (mode != "values")
                {
                    if (!item.value("format").isObject())
                        return fail();
                    const auto encoded_format = item.value("format").toObject();
                    if (encoded_format.size() > 32)
                        return fail();
                    SpreadsheetFormat format{{"reset", "1"}};
                    for (auto entry = encoded_format.begin(); entry != encoded_format.end(); ++entry)
                    {
                        if (!entry.value().isString())
                            return fail();
                        format[entry.key().toStdString()] = entry.value().toString().toStdString();
                    }
                    formats.push_back({static_cast<std::size_t>(sheet_), target, std::move(format)});
                }
            }
        if (!commitChanges(cells, formats, {}))
            return false;
        pending_cut_.reset();
        anchor_ = r.first;
        cell_ = {r.first.row + target_height - 1, r.first.column + target_width - 1};
        emit cellChanged();
        return true;
    }
}
