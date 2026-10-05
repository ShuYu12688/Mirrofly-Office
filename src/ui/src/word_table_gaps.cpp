#include "word_table_gaps.hpp"
#include "word_format_properties.hpp"

#include <QTextCursor>
#include <QTextTable>

namespace mirrorfly
{
    void mark_word_table_gaps(QTextTable& table, const WordTable& source)
    {
        const auto columns = source.column_widths.size();
        std::vector<bool> occupied(source.rows * columns, false);
        for (const auto& cell : source.cells)
            for (auto row = cell.row; row < cell.row + cell.row_span; ++row)
                for (auto column = cell.column; column < cell.column + cell.column_span; ++column)
                    occupied[row * columns + column] = true;
        for (std::size_t row = 0; row < source.rows; ++row)
            for (std::size_t column = 0; column < columns; ++column)
            {
                if (occupied[row * columns + column])
                    continue;
                auto cell = table.cellAt(static_cast<int>(row), static_cast<int>(column));
                auto format = cell.format().toTableCellFormat();
                // gridBefore/gridAfter gaps are layout space, not source document cells.
                format.setProperty(word_table_gap_property, true);
                format.setBorder(0);
                format.setPadding(0);
                format.clearBackground();
                cell.setFormat(format);
            }
    }

    bool word_table_gap(const QTextCursor& cursor)
    {
        const auto* table = cursor.currentTable();
        return table && table->cellAt(cursor).format().property(word_table_gap_property).toBool();
    }
}
