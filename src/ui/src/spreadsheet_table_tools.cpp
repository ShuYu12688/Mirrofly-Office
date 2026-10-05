#include "spreadsheet_bridge.hpp"

#include <algorithm>

namespace mirrorfly
{
    bool SpreadsheetBridge::applyTableTool(const QString& action, const QVariantMap& args)
    {
        const auto r = selectedRange();
        auto features = spreadsheet_features(document_, sheet_);
        if (!features.tables_supported)
        {
            setError(QStringLiteral("此导入表格的结构或样式暂未支持，当前保留原表格。"));
            return false;
        }
        const auto requested_name = args.value("name").toString().trimmed().toStdString();
        auto found = std::find_if(features.tables.begin(), features.tables.end(), [&](const auto& table)
        {
            if (action != "tableStyle" && !requested_name.empty())
                return table.name == requested_name;
            return cell_.row >= table.range.first.row && cell_.row <= table.range.last.row &&
                cell_.column >= table.range.first.column && cell_.column <= table.range.last.column;
        });
        if (action != "tableStyle")
        {
            if (found == features.tables.end())
            {
                setError(QStringLiteral("请先选择表格内的单元格，或填写表格名称。"));
                return false;
            }
            if (action == "removeTable")
                features.tables.erase(found);
            else
            {
                const auto previous = found->range;
                found->range = r;
                if (features.filter &&
                    spreadsheet_address(features.filter->first) == spreadsheet_address(previous.first) &&
                    spreadsheet_address(features.filter->last) == spreadsheet_address(previous.last))
                {
                    features.filter = r;
                    features.filters.clear();
                }
            }
            return commitFeatures(features);
        }
        SpreadsheetTable table;
        if (found != features.tables.end())
            table = *found;
        else
            table.range = r;
        if (!requested_name.empty())
            table.name = requested_name;
        if (table.name.empty())
        {
            std::set<std::string> names;
            for (std::size_t index = 0; index < document_.sheets.size(); ++index)
                for (const auto& existing : spreadsheet_features(document_, index).tables)
                    names.insert(QString::fromStdString(existing.name).toCaseFolded().toStdString());
            unsigned suffix = 1;
            do
            {
                table.name = "Table" + std::to_string(suffix++);
            } while (names.count(QString::fromStdString(table.name).toCaseFolded().toStdString()));
        }
        for (const auto* key : {"rowStripes", "columnStripes", "firstColumn", "lastColumn"})
            if (args.contains(key) && args.value(key).metaType().id() != QMetaType::Bool)
                return false;
        table.row_stripes = args.value("rowStripes", table.row_stripes).toBool();
        table.column_stripes = args.value("columnStripes", table.column_stripes).toBool();
        table.first_column = args.value("firstColumn", table.first_column).toBool();
        table.last_column = args.value("lastColumn", table.last_column).toBool();
        if (args.contains("palette"))
        {
            const auto palette = args.value("palette").toMap();
            if (palette.size() != 5)
                return false;
            const auto body = palette.value("body").toString().toStdString();
            const auto body_text = palette.value("bodyText").toString().toStdString();
            const auto header = palette.value("header").toString().toStdString();
            const auto header_text = palette.value("headerText").toString().toStdString();
            const auto band = palette.value("alternate").toString().toStdString();
            table.style = {{"wholeTable", {{"fill", body}, {"text", body_text}}},
                {"headerRow", {{"fill", header}, {"text", header_text}, {"bold", "1"}}},
                {"firstRowStripe", {{"fill", band}}}, {"firstColumnStripe", {{"fill", band}}},
                {"firstColumn", {{"bold", "1"}}}, {"lastColumn", {{"bold", "1"}}}};
        }
        if (found == features.tables.end())
            features.tables.push_back(std::move(table));
        else
            *found = std::move(table);
        return commitFeatures(features);
    }
}
