#include "markdown_blocks.hpp"
#include "markdown_break.hpp"
#include "markdown_code.hpp"
#include "markdown_document.hpp"
#include "markdown_image.hpp"
#include "markdown_inline.hpp"
#include "markdown_link.hpp"
#include "markdown_rule.hpp"
#include "markdown_table.hpp"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <algorithm>

namespace mirrorfly
{
    QVariantMap inspect_markdown_document(QTextDocument& document, int position)
    {
        QVariantList outline;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            const int level = block.blockFormat().headingLevel();
            if (level > 0)
            {
                outline.append(QVariantMap{{QStringLiteral("title"), block.text()},
                    {QStringLiteral("level"), level}, {QStringLiteral("position"), block.position()}});
            }
        }
        QTextCursor cursor(&document);
        cursor.setPosition(std::clamp(position, 0, document.characterCount() - 1));
        auto* frame = code_frame(cursor);
        auto* table = cursor.currentTable();
        auto state = inspect_markdown_list(cursor);
        auto code_cursor = cursor;
        const bool in_inline_code = markdown_inline_code_range(code_cursor);
        state.insert("inInlineCode", in_inline_code);
        state.insert("inlineCodeStart", in_inline_code ? code_cursor.selectionStart() : -1);
        state.insert("inlineCodeEnd", in_inline_code ? code_cursor.selectionEnd() : -1);
        const auto link = inspect_markdown_link(cursor);
        for (auto iterator = link.begin(); iterator != link.end(); ++iterator)
            state.insert(iterator.key(), iterator.value());
        const auto image = inspect_markdown_image(cursor);
        for (auto iterator = image.begin(); iterator != image.end(); ++iterator)
            state.insert(iterator.key(), iterator.value());
        state.insert(QStringLiteral("outline"), outline);
        state.insert("headingLevel", cursor.blockFormat().headingLevel());
        state.insert("canStyleHeading",
            !frame && !table && !cursor.block().text().contains(QChar::LineSeparator) &&
                !state.value("taskItem").toBool() &&
                !cursor.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth));
        state.insert("canHardBreak", can_insert_markdown_hard_break(cursor));
        state.insert("canThematicBreak", can_insert_markdown_rule(cursor));
        int quote_level = cursor.blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
        if (table)
            quote_level = markdown_table_quote_level(table);
        state.insert(QStringLiteral("quoteLevel"), quote_level);
        state.insert(QStringLiteral("quoteMinimum"), table ? markdown_table_quote_minimum(table) : 0);
        const QVariantMap structure{{QStringLiteral("inCode"), frame != nullptr},
            {QStringLiteral("codeLanguage"),
                frame ? cursor.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage) : QString{}},
            {QStringLiteral("inTable"), table != nullptr},
            {QStringLiteral("tableRows"), table ? table->rows() : 0},
            {QStringLiteral("tableColumns"), table ? table->columns() : 0},
            {QStringLiteral("tableColumn"), table ? table->cellAt(cursor).column() : -1},
            {QStringLiteral("tableAlignment"),
                table ? markdown_table_alignment(table, table->cellAt(cursor).column()) : QString{}}};
        for (auto iterator = structure.begin(); iterator != structure.end(); ++iterator)
            state.insert(iterator.key(), iterator.value());
        return state;
    }

}
