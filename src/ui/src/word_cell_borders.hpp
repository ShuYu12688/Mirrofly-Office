#pragma once

#include <mirrorfly/word.hpp>

#include <QTextTable>
#include <QVariantMap>

namespace mirrorfly
{
    using WordCellBorderRows = std::vector<std::array<WordBorder, 4>>;
    struct WordCellBorderDisplay
    {
        bool exact = true;
        std::array<std::vector<WordBorder>, 4> edges;
        bool operator==(const WordCellBorderDisplay& other) const
        {
            return exact == other.exact && edges == other.edges;
        }
    };
    void store_word_cell_borders(QTextTableCellFormat& format, const WordTableCell& cell);
    bool extract_word_cell_borders(const QTextTableCellFormat& format, WordTableCell& cell);
    void refresh_word_table_borders(QTextTable& table);
    QVariantMap inspect_word_cell_borders(const QTextTableCell& cell);
    bool format_word_cell_border(QTextTable& table, const QTextTableCell& cell, const QVariant& value);
}

Q_DECLARE_METATYPE(mirrorfly::WordCellBorderRows)

Q_DECLARE_METATYPE(mirrorfly::WordCellBorderDisplay)
