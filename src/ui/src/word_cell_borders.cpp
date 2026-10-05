#include "word_cell_borders.hpp"
#include "word_format_properties.hpp"
#include "word_units.hpp"

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>
#include <map>

namespace
{
    using namespace mirrorfly;
    const QStringList edges{"left", "top", "right", "bottom"};

    QVariantMap describe(const WordBorder& border)
    {
        return {{"style", QString::fromStdString(border.style)},
            {"color", QString::fromStdString(border.color)}, {"width", border.width}, {"unit", "pt"},
            {"cellSpecific", border.cell_specific}};
    }

    bool same_appearance(const WordBorder& first, const WordBorder& second)
    {
        return first.style == second.style && first.width == second.width &&
            QString::fromStdString(first.color)
                .compare(QString::fromStdString(second.color), Qt::CaseInsensitive) == 0;
    }

    bool supported(const WordBorder& border)
    {
        const QStringList styles{"", "nil", "none", "single", "thick", "double", "dotted", "dashed"};
        return styles.contains(QString::fromStdString(border.style));
    }

    void paint_edge(QTextTableCellFormat& format, int edge, const WordBorder& border)
    {
        const int widths[] = {QTextFormat::TableCellLeftBorder, QTextFormat::TableCellTopBorder,
            QTextFormat::TableCellRightBorder, QTextFormat::TableCellBottomBorder};
        const int styles[] = {QTextFormat::TableCellLeftBorderStyle, QTextFormat::TableCellTopBorderStyle,
            QTextFormat::TableCellRightBorderStyle, QTextFormat::TableCellBottomBorderStyle};
        const int brushes[] = {QTextFormat::TableCellLeftBorderBrush, QTextFormat::TableCellTopBorderBrush,
            QTextFormat::TableCellRightBorderBrush, QTextFormat::TableCellBottomBorderBrush};
        auto style = QTextFrameFormat::BorderStyle_Solid;
        double width = border.width;
        if (border.style.empty() || border.style == "nil" || border.style == "none")
        {
            width = 0;
            style = QTextFrameFormat::BorderStyle_None;
        }
        else if (border.style == "double")
            style = QTextFrameFormat::BorderStyle_Double;
        else if (border.style == "dotted")
            style = QTextFrameFormat::BorderStyle_Dotted;
        else if (border.style == "dashed")
            style = QTextFrameFormat::BorderStyle_Dashed;
        format.setProperty(widths[edge], word_points_to_pixels(width));
        format.setProperty(styles[edge], style);
        format.setProperty(brushes[edge], QBrush(QColor(QString::fromStdString(border.color))));
    }

    WordBorder source_edge(const QTextTableCell& cell, const WordCellBorderRows& rows, int row, int edge)
    {
        if (!cell.isValid() || rows.empty())
            return {};
        if (edge == 1)
            return rows.front()[edge];
        if (edge == 3)
            return rows.back()[edge];
        const auto index = static_cast<std::size_t>(row - cell.row());
        return index < rows.size() ? rows[index][edge] : WordBorder{};
    }
}

namespace mirrorfly
{
    void store_word_cell_borders(QTextTableCellFormat& format, const WordTableCell& cell)
    {
        if (!cell.border_rows.empty())
            format.setProperty(word_cell_borders_property, QVariant::fromValue(cell.border_rows));
    }

    bool extract_word_cell_borders(const QTextTableCellFormat& format, WordTableCell& cell)
    {
        if (cell.border_rows.empty())
            return true;
        const auto rows = format.property(word_cell_borders_property).value<WordCellBorderRows>();
        if (rows.size() != cell.row_span)
            return false;
        cell.border_rows = rows;
        return true;
    }

    void refresh_word_table_borders(QTextTable& table)
    {
        std::map<int, WordCellBorderRows> sources;
        for (int row = 0; row < table.rows(); ++row)
            for (int column = 0; column < table.columns(); ++column)
            {
                const auto cell = table.cellAt(row, column);
                if (cell.row() == row && cell.column() == column)
                    sources.emplace(cell.firstPosition(),
                        cell.format().property(word_cell_borders_property).value<WordCellBorderRows>());
            }
        std::vector<std::pair<QTextTableCell, QTextTableCellFormat>> pending;
        bool exact_table = table.format().property(word_table_border_layout_property).toBool();
        for (int row = 0; row < table.rows(); ++row)
            for (int column = 0; column < table.columns(); ++column)
            {
                auto cell = table.cellAt(row, column);
                if (cell.row() != row || cell.column() != column || sources[cell.firstPosition()].empty())
                    continue;
                auto format = cell.format().toTableCellFormat();
                WordCellBorderDisplay display;
                bool exact = true;
                for (int edge = 0; edge < 4; ++edge)
                {
                    const bool vertical = edge == 0 || edge == 2;
                    const auto count = vertical ? cell.rowSpan() : cell.columnSpan();
                    WordBorder first;
                    bool mixed = false;
                    auto& segments = display.edges[edge];
                    segments.reserve(static_cast<std::size_t>(count));
                    for (int segment = 0; segment < count; ++segment)
                    {
                        const auto r = row + (vertical ? segment : 0);
                        const auto c = column + (vertical ? 0 : segment);
                        const auto nr = edge == 1 ? row - 1 : edge == 3 ? row + cell.rowSpan() : r;
                        const auto nc = edge == 0 ? column - 1 : edge == 2 ? column + cell.columnSpan() : c;
                        const auto neighbor = nr >= 0 && nr < table.rows() && nc >= 0 && nc < table.columns()
                            ? table.cellAt(nr, nc)
                            : QTextTableCell{};
                        const auto own = source_edge(cell, sources[cell.firstPosition()], r, edge);
                        const auto other = neighbor.isValid()
                            ? source_edge(neighbor, sources[neighbor.firstPosition()], nr, (edge + 2) % 4)
                            : WordBorder{};
                        const auto resolved =
                            edge < 2 ? resolve_word_border(other, own) : resolve_word_border(own, other);
                        if (segment == 0)
                            first = resolved;
                        else
                            mixed = mixed || !same_appearance(first, resolved);
                        exact = exact && supported(resolved);
                        segments.push_back(resolved);
                    }
                    exact = exact && !mixed;
                    paint_edge(format, edge, first);
                }
                exact_table = exact_table && exact;
                display.exact = exact;
                format.setProperty(word_cell_border_display_property, QVariant::fromValue(display));
                pending.emplace_back(cell, format);
            }
        for (auto& item : pending)
        {
            if (!exact_table)
            {
                auto display =
                    item.second.property(word_cell_border_display_property).value<WordCellBorderDisplay>();
                display.exact = false;
                item.second.setProperty(word_cell_border_display_property, QVariant::fromValue(display));
            }
            if (item.second != item.first.format())
                item.first.setFormat(item.second);
        }
    }

    QVariantMap inspect_word_cell_borders(const QTextTableCell& cell)
    {
        if (!cell.isValid())
            return {};
        const auto rows = cell.format().property(word_cell_borders_property).value<WordCellBorderRows>();
        QVariantMap result;
        for (int edge = 0; edge < 4 && !rows.empty(); ++edge)
        {
            const auto first = edge == 3 ? rows.back()[edge] : rows.front()[edge];
            bool mixed = false;
            if (edge == 0 || edge == 2)
                for (const auto& row : rows)
                    mixed = mixed || !same_appearance(first, row[edge]);
            auto value = describe(first);
            value.insert("mixed", mixed);
            result.insert(edges[edge], value);
        }
        const auto display =
            cell.format().property(word_cell_border_display_property).value<WordCellBorderDisplay>();
        QVariantMap display_edges;
        for (std::size_t edge = 0; edge < display.edges.size(); ++edge)
        {
            QVariantList segments;
            bool mixed = false;
            for (const auto& border : display.edges[edge])
            {
                mixed = mixed || !same_appearance(display.edges[edge].front(), border);
                if (segments.size() < 8)
                    segments.push_back(describe(border));
            }
            display_edges.insert(edges[static_cast<qsizetype>(edge)],
                QVariantMap{{"mixed", mixed}, {"segments", segments},
                    {"segmentCount", static_cast<qulonglong>(display.edges[edge].size())},
                    {"segmentsComplete", display.edges[edge].size() <= 8}});
        }
        return {{"source", result},
            {"display", QVariantMap{{"exact", display.exact}, {"edges", display_edges}}},
            {"editable", !rows.empty()}};
    }

    bool format_word_cell_border(QTextTable& table, const QTextTableCell& selected, const QVariant& value)
    {
        // QML var objects reach the bridge through QVariant's registered map conversion.
        const auto object = QJsonObject::fromVariantMap(value.toMap());
        if (object.size() != 4 || !object.value("edge").isString() || !object.value("style").isString() ||
            !object.value("color").isString() || !object.value("width").isDouble())
            return false;
        const auto edge = edges.indexOf(object.value("edge").toString());
        if (edge < 0)
            return false;
        WordDocument candidate;
        candidate.tables.resize(1);
        candidate.tables[0].cells.resize(1);
        auto& cell = candidate.tables[0].cells[0];
        cell.row_span = static_cast<std::size_t>(selected.rowSpan());
        cell.border_rows = selected.format().property(word_cell_borders_property).value<WordCellBorderRows>();
        const auto edited = set_word_cell_border(candidate, 0, 0, static_cast<WordBorderEdge>(edge),
            {object.value("style").toString().toStdString(), object.value("color").toString().toStdString(),
                object.value("width").toDouble()});
        if (!edited.success || !edited.changed)
            return edited.success;
        auto cursor = selected.firstCursorPosition();
        cursor.beginEditBlock();
        auto target = selected;
        auto format = target.format().toTableCellFormat();
        store_word_cell_borders(format, cell);
        target.setFormat(format);
        refresh_word_table_borders(table);
        cursor.endEditBlock();
        return true;
    }
}
