#include "markdown_rule.hpp"
#include "markdown_code.hpp"
#include "markdown_container.hpp"
#include "markdown_input.hpp"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QUuid>

namespace mirrorfly
{
    std::vector<MarkdownInlineToken> protect_markdown_rules(QTextDocument& document)
    {
        std::vector<MarkdownInlineToken> tokens;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
            {
                QTextCursor cursor(block);
                auto format = block.blockFormat();
                format.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
                cursor.setBlockFormat(format);
                const auto token = "MIRRORFLYRULE" + QUuid::createUuid().toString(QUuid::Id128);
                cursor.insertText(token, QTextCharFormat{});
                tokens.push_back({token, "- - -"});
            }
        return tokens;
    }

    bool can_insert_markdown_rule(const QTextCursor& cursor)
    {
        return !cursor.hasSelection() && !cursor.currentTable() && !code_frame(cursor) &&
            !cursor.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    }

    void insert_markdown_rule(QTextCursor& cursor, const QVariantMap& theme)
    {
        const int quote = cursor.blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
        QVariantList quotes;
        for (const auto& node : cursor.blockFormat().property(markdown_container_property).toList())
            if (node.toMap().value("kind") == "quote")
                quotes.append(node);
        auto last = cursor.block();
        if (last.textList() || last.blockFormat().indent() > 0)
        {
            auto* root_list = last.textList();
            for (auto previous = last.previous(); root_list && previous.isValid();
                previous = previous.previous())
            {
                if (!previous.textList() && previous.blockFormat().indent() == 0 &&
                    !previous.text().isEmpty())
                    break;
                if (previous.textList() &&
                    previous.textList()->format().indent() < root_list->format().indent())
                    root_list = previous.textList();
                if (root_list->format().indent() == 1)
                    break;
            }
            for (auto next = last.next(); next.isValid(); next = next.next())
            {
                if (root_list && next.textList() && next.textList() != root_list &&
                    next.textList()->format().indent() <= root_list->format().indent())
                    break;
                if (!next.textList() && next.blockFormat().indent() == 0 && !next.text().isEmpty())
                    break;
                last = next;
            }
        }
        cursor.setPosition(last.position() + last.length() - 1);
        cursor.insertBlock();
        if (auto* list = cursor.block().textList())
            list->remove(cursor.block());
        normal_markdown_block(cursor, theme);
        auto format = cursor.blockFormat();
        format.setProperty(QTextFormat::BlockQuoteLevel, quote);
        format.setProperty(markdown_container_property, quotes);
        format.setProperty(markdown_container_head_property, false);
        format.setLeftMargin(quote * theme.value("markdownQuoteIndent", 18).toInt());
        format.setProperty(
            QTextFormat::BlockTrailingHorizontalRulerWidth, QTextLength(QTextLength::PercentageLength, 100));
        cursor.setBlockFormat(format);
        cursor.insertBlock();
        normal_markdown_block(cursor, theme);
        format = cursor.blockFormat();
        format.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
        format.setProperty(QTextFormat::BlockQuoteLevel, quote);
        format.setProperty(markdown_container_property, quotes);
        format.setProperty(markdown_container_head_property, false);
        format.setLeftMargin(quote * theme.value("markdownQuoteIndent", 18).toInt());
        cursor.setBlockFormat(format);
    }
}
