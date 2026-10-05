#include "markdown_blocks.hpp"
#include "markdown_code.hpp"
#include "markdown_list_tree.hpp"
#include "markdown_table.hpp"

#include <QFont>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <algorithm>

namespace
{
    int markdown_list_level(const QTextBlock& block)
    {
        return block.textList() ? block.textList()->format().indent() : 0;
    }
}

namespace mirrorfly
{
    std::vector<QTextBlock> selected_markdown_blocks(const QTextCursor& cursor)
    {
        std::vector<QTextBlock> blocks;
        const int last = cursor.selectionEnd() - (cursor.hasSelection() ? 1 : 0);
        for (auto block = cursor.document()->findBlock(cursor.selectionStart()); block.isValid();
            block = block.next())
        {
            blocks.push_back(block);
            if (block.position() + block.length() > last)
                break;
        }
        return blocks;
    }

    bool set_markdown_quote(QTextCursor& cursor, int quote_level, const QVariantMap& theme)
    {
        if (quote_level < 0 || quote_level > 8)
            return false;
        if (auto* table = cursor.currentTable())
            return set_markdown_table_quote(table, quote_level, theme);
        auto blocks = selected_markdown_blocks(cursor);
        if (blocks.empty())
            return false;
        const int previous = blocks.front().blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
        const int delta = quote_level - previous;
        const int pivot = markdown_list_level(blocks.front());
        QList<QTextTable*> tables;
        QList<QTextFrame*> frames;
        const auto owned = [&](QTextTable* table)
        {
            return std::any_of(blocks.begin(), blocks.end(), [&](const QTextBlock& block)
            {
                return markdown_table_owned_by(table, block);
            });
        };
        if (auto* frame = code_frame(cursor))
        {
            blocks.clear();
            frames.append(frame);
            for (auto block = frame->firstCursorPosition().block();
                block.isValid() && block.position() <= frame->lastPosition(); block = block.next())
                if (QTextCursor(block).currentFrame() == frame)
                    blocks.push_back(block);
        }
        else
        {
            if (pivot > 0)
            {
                blocks = markdown_list_descendants(markdown_list_tree(*cursor.document()), blocks);
                const auto removed = std::remove_if(blocks.begin(), blocks.end(), [&](const auto& block)
                {
                    auto* table = QTextCursor(block).currentTable();
                    if (table && !tables.contains(table))
                        tables.append(table);
                    if (auto* frame = code_frame(QTextCursor(block)); frame && !frames.contains(frame))
                        frames.append(frame);
                    return table != nullptr;
                });
                blocks.erase(removed, blocks.end());
            }
            else
                for (auto next = blocks.back().next(); next.isValid();)
                {
                    QTextCursor probe(next);
                    if (auto* table = probe.currentTable())
                    {
                        if (!owned(table) && (previous <= 0 || markdown_table_quote_level(table) <= previous))
                            break;
                        tables.append(table);
                        next = cursor.document()->findBlock(table->lastPosition() + 1);
                        if (next.isValid() && next.text().isEmpty())
                            next = next.next();
                        continue;
                    }
                    const int next_quote = next.blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
                    const bool nested_quote = previous > 0 && next_quote > previous;
                    const bool nested_list = pivot > 0 &&
                        (markdown_list_level(next) > pivot || next.blockFormat().indent() >= pivot);
                    if (!nested_quote && !nested_list)
                        break;
                    if (auto* child_frame = code_frame(probe))
                        if (!frames.contains(child_frame))
                            frames.append(child_frame);
                    blocks.push_back(next);
                    next = next.next();
                }
        }
        for (const auto& block : blocks)
        {
            const int changed = block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) + delta;
            if (changed < 0 || changed > 8 ||
                !can_change_markdown_owner_quote(
                    block, block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel), changed))
                return false;
        }
        for (auto* table : tables)
            if (!owned(table) &&
                (markdown_table_quote_level(table) + delta > 8 ||
                    markdown_table_quote_level(table) + delta < markdown_table_quote_minimum(table)))
                return false;
        for (const auto& block : blocks)
        {
            QTextCursor line(block);
            auto format = block.blockFormat();
            const int old = format.intProperty(QTextFormat::BlockQuoteLevel);
            const int changed = old + delta;
            format.setProperty(QTextFormat::BlockQuoteLevel, changed);
            format.setLeftMargin(changed * theme.value("markdownQuoteIndent", 18).toInt());
            line.setBlockFormat(format);
            change_markdown_owner_quote(block, old, changed, theme);
        }
        for (auto* table : tables)
            if (!owned(table))
                set_markdown_table_quote(table, markdown_table_quote_level(table) + delta, theme);
        for (auto* frame : frames)
        {
            auto format = frame->frameFormat();
            const int changed =
                frame->firstCursorPosition().blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
            format.setLeftMargin(changed * theme.value("markdownQuoteIndent", 18).toInt());
            frame->setFrameFormat(format);
        }
        return true;
    }

    void apply_markdown_block(
        QTextCursor& cursor, const QString& action, const QVariantMap& options, const QVariantMap& theme)
    {
        auto& document = *cursor.document();
        auto block = document.findBlock(cursor.selectionStart());
        const int last = cursor.selectionEnd();
        do
        {
            QTextCursor line(block);
            auto format = block.blockFormat();
            const int level = action == QStringLiteral("heading")
                ? std::clamp(options.value(QStringLiteral("headingLevel"), 2).toInt(), 1, 6)
                : 0;
            format.setHeadingLevel(level);
            format.setTopMargin(level > 0 ? 16 : 4);
            format.setBottomMargin(10);
            line.setBlockFormat(format);
            style_markdown_heading(block, level, theme);
            block = block.next();
        } while (block.isValid() && block.position() < last);
    }

}
