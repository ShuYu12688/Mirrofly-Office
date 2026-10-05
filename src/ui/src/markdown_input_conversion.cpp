#include "markdown_blocks.hpp"
#include "markdown_input.hpp"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
namespace mirrorfly
{
    bool convert_explicit_markdown_block(QTextCursor& cursor, const QVariantMap& theme)
    {
        const auto block = cursor.block();
        if (!block.isValid() || cursor.hasSelection() ||
            cursor.position() != block.position() + block.length() - 1)
        {
            return false;
        }
        enum class BlockKind
        {
            Heading,
            Quote,
            Bullet,
            Ordered,
            Task
        };
        static const QRegularExpression heading(QStringLiteral("^(#{1,6})[ \\t]+(.*)$"));
        static const QRegularExpression quote(QStringLiteral("^((?:>[ \\t]*)+)(.*)$"));
        static const QRegularExpression task(QStringLiteral("^[-+*][ \\t]+\\[([ xX])\\][ \\t]+(.*)$"));
        static const QRegularExpression bullet(QStringLiteral("^[-+*][ \\t]+(.*)$"));
        static const QRegularExpression ordered(QStringLiteral("^(1)[.)][ \\t]+(.*)$"));
        BlockKind kind;
        int heading_level = 0;
        int prefix_length = 0;
        bool task_checked = false;
        int quote_level = 0;
        auto match = heading.match(block.text());
        if (match.hasMatch())
        {
            kind = BlockKind::Heading;
            heading_level = match.captured(1).size();
            prefix_length = match.capturedStart(2);
        }
        else if ((match = quote.match(block.text())).hasMatch() && match.captured(1).count(u'>') <= 8)
        {
            kind = BlockKind::Quote;
            prefix_length = match.capturedStart(2);
            quote_level = static_cast<int>(match.captured(1).count(u'>'));
        }
        else if ((match = task.match(block.text())).hasMatch())
        {
            kind = BlockKind::Task;
            prefix_length = match.capturedStart(2);
            task_checked = match.captured(1).compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0;
        }
        else if ((match = bullet.match(block.text())).hasMatch())
        {
            kind = BlockKind::Bullet;
            prefix_length = match.capturedStart(1);
        }
        else if ((match = ordered.match(block.text())).hasMatch())
        {
            kind = BlockKind::Ordered;
            prefix_length = match.capturedStart(2);
        }
        else
        {
            return false;
        }

        QTextCursor line(block);
        line.setPosition(block.position());
        line.setPosition(block.position() + prefix_length, QTextCursor::KeepAnchor);
        line.removeSelectedText();
        line.setPosition(block.position());
        if (kind == BlockKind::Heading || kind == BlockKind::Quote)
        {
            if (auto* list = line.block().textList())
            {
                list->remove(line.block());
            }
            auto format = line.blockFormat();
            format.setHeadingLevel(heading_level);
            format.setProperty(QTextFormat::BlockQuoteLevel, quote_level);
            format.setLeftMargin(quote_level * theme.value("markdownQuoteIndent", 18).toInt());
            format.setTopMargin(heading_level > 0 ? 16 : 4);
            format.setBottomMargin(10);
            line.setBlockFormat(format);
            mirrorfly::style_markdown_heading(line.block(), heading_level, theme);
        }
        else
        {
            QTextListFormat format;
            format.setStyle(
                kind == BlockKind::Ordered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
            format.setIndent(1);
            if (kind == BlockKind::Ordered)
            {
                format.setStart(1);
            }
            line.createList(format);
            auto block_format = line.blockFormat();
            block_format.setMarker(kind == BlockKind::Task
                    ? (task_checked ? QTextBlockFormat::MarkerType::Checked
                                    : QTextBlockFormat::MarkerType::Unchecked)
                    : QTextBlockFormat::MarkerType::NoMarker);
            line.setBlockFormat(block_format);
        }
        cursor.setPosition(block.position());
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
        if (kind == BlockKind::Heading || kind == BlockKind::Quote)
        {
            normal_markdown_block(cursor, theme);
        }
        else if (kind == BlockKind::Task)
        {
            auto format = cursor.blockFormat();
            format.setMarker(QTextBlockFormat::MarkerType::Unchecked);
            cursor.setBlockFormat(format);
        }
        return true;
    }

}
