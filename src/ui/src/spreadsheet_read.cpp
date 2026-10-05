#include "document_read.hpp"
#include "spreadsheet_bridge.hpp"

#include <set>

namespace mirrorfly
{
    QVariantMap SpreadsheetBridge::readContent(const QVariantMap& query) const
    {
        const DocumentReadQuery read(query);
        if (!read.valid)
            return read_error("invalid_read_query");
        if (!active_)
            return read_error("no_document");
        if (read.view == "overview")
        {
            QVariantList items;
            const int count = static_cast<int>(document_.sheets.size());
            const int end = std::min(count, read.offset + std::min(read.limit, 16));
            for (int index = read.offset; index < end; ++index)
            {
                const auto& sheet = document_.sheets[index];
                if (!append_read_item(items,
                        {{"index", index}, {"name", QString::fromStdString(sheet.name)}, {"rows", sheet.rows},
                            {"columns", sheet.columns}, {"editable", sheet.editable}}))
                    break;
            }
            auto result = read_items(items, read.offset, count);
            result.insert("current", sheet_);
            result.insert("selection", rangeInfo());
            result.insert("views", QStringList{"overview", "content", "text", "formula", "format"});
            return result;
        }
        const int index = read.index < 0 ? sheet_ : read.index;
        if (index < 0 || index >= static_cast<int>(document_.sheets.size()))
            return read_error("invalid_index");
        if (read.view == "format")
        {
            const auto address = parse_spreadsheet_address(read.id.toStdString());
            if (!address)
                return {{"ok", false}, {"error", "invalid_cell_address"},
                    {"hint",
                        "Use one A1 address, e.g. id=A2; ranges such as A1:A5 are not cell IDs. "
                        "Use view=content for bounded cell listings or read each cell separately."}};
            QVariantMap format;
            for (const auto& [key, value] : spreadsheet_cell_format(document_, index, *address))
                format.insert(QString::fromStdString(key), QString::fromStdString(value));
            const auto display = model_.displayFormat(index, *address);
            return {{"ok", true}, {"index", index}, {"id", read.id}, {"format", format}, {"display", display},
                {"scope",
                    "format includes resolved table styles, not just direct cell formatting; "
                    "display also includes supported conditional colors."},
                {"columnWidth", spreadsheet_dimension(document_, index, true, address->column)},
                {"rowHeight", spreadsheet_dimension(document_, index, false, address->row)}};
        }
        if (read.view == "text" || read.view == "formula")
        {
            const auto address = parse_spreadsheet_address(read.id.toStdString());
            if (!address)
                return {{"ok", false}, {"error", "invalid_cell_address"},
                    {"hint",
                        "Use one A1 address, e.g. id=A2; ranges such as A1:A5 are not cell IDs. "
                        "Use view=content for bounded cell listings or read each cell separately."}};
            const auto cell = spreadsheet_cell(document_, index, *address);
            const auto& text = read.view == "formula" ? cell.formula : cell.value.text;
            return read_text(
                QString::fromStdString(text), read.offset, query.contains("limit") ? read.limit : 1600);
        }
        if (read.view != "content")
            return read_error("unsupported_view");
        // Enumerate stored cells, not the GUI selection or the million-row Excel coordinate space.
        std::set<SpreadsheetAddress> addresses;
        if (!read.id.isEmpty())
        {
            const auto address = parse_spreadsheet_address(read.id.toStdString());
            if (!address)
                return {{"ok", false}, {"error", "invalid_cell_address"},
                    {"hint", "content id filters one cell, e.g. A8; omit id for a paginated cell listing."}};
            addresses = {*address};
        }
        else
        {
            for (const auto& [address, cell] : document_.sheets[index].cells)
                addresses.insert(address);
            if (const auto edits = document_.edits.find(index); edits != document_.edits.end())
                for (const auto& [address, value] : edits->second)
                    addresses.insert(address);
        }
        const int count = static_cast<int>(addresses.size());
        if (read.offset > count)
            return read_error("invalid_offset");
        auto cell_it = addresses.begin();
        std::advance(cell_it, read.offset);
        QVariantList items;
        for (; cell_it != addresses.end() && items.size() < std::min(read.limit, 16); ++cell_it)
        {
            const auto cell = spreadsheet_cell(document_, index, *cell_it);
            const QVariantMap item{{"id", QString::fromStdString(spreadsheet_address(*cell_it))},
                {"row", cell_it->row}, {"column", cell_it->column},
                {"textPreview", read_preview(QString::fromStdString(cell.value.text), 240)},
                {"formulaPreview", read_preview(QString::fromStdString(cell.formula), 160)},
                {"editable", cell.editable}};
            if (!append_read_item(items, item))
                break;
        }
        auto result = read_items(items, read.offset, count);
        result.insert("index", index);
        result.insert("textQuery", "view=text or formula,id=cell address; offset is UTF-16");
        return result;
    }
}
