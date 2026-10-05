#include "markdown_code.hpp"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <algorithm>

namespace
{
    constexpr int code_frame_property = QTextFormat::UserProperty + 61;
    QColor color(const QVariantMap& theme, const char* key)
    {
        return QColor(theme.value(QString::fromLatin1(key)).toString());
    }
}

namespace mirrorfly
{
    void insert_markdown_code_frame(QTextCursor& cursor, const QVariantMap& options, const QVariantMap& theme)
    {
        const int quote_level = cursor.blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
        QString text = cursor.hasSelection() ? cursor.selectedText() : QString{};
        text.replace(QChar::ParagraphSeparator, QChar(u'\n'));
        cursor.removeSelectedText();
        auto* inserted = cursor.insertFrame(frame_style(theme));
        cursor = inserted->firstCursorPosition();
        const int begin = cursor.position();
        cursor.insertText(text);
        cursor.endEditBlock();
        cursor.joinPreviousEditBlock();
        style_code_frame(inserted,
            options.value(QStringLiteral("language"), QStringLiteral("text")).toString(), theme, quote_level);
        cursor.setPosition(begin, QTextCursor::KeepAnchor);
    }

    QString language_name(QString language)
    {
        static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9_+.#-]{0,32}"));
        return allowed.match(language).captured();
    }

    bool code_block(const QTextBlock& block)
    {
        const auto format = block.blockFormat();
        return format.hasProperty(QTextFormat::BlockCodeFence) ||
            format.hasProperty(QTextFormat::BlockCodeLanguage);
    }

    QTextFrame* code_frame(QTextCursor cursor)
    {
        auto* frame = cursor.currentFrame();
        while (frame != nullptr)
        {
            if (frame->frameFormat().boolProperty(code_frame_property))
            {
                return frame;
            }
            frame = frame->parentFrame();
        }
        return nullptr;
    }

    QTextFrameFormat frame_style(const QVariantMap& theme)
    {
        QTextFrameFormat format;
        format.setProperty(code_frame_property, true);
        format.setBackground(color(theme, "accentSoft"));
        // Qt 6.8.3 dereferences a null table while rendering borders on ordinary text frames.
        format.setPadding(12);
        format.setTopMargin(12);
        format.setBottomMargin(12);
        return format;
    }

    void style_code_frame(
        QTextFrame* frame, const QString& language, const QVariantMap& theme, int quote_level)
    {
        if (quote_level >= 0)
        {
            auto format = frame->frameFormat();
            format.setLeftMargin(quote_level * theme.value("markdownQuoteIndent", 18).toInt());
            frame->setFrameFormat(format);
        }
        std::vector<int> block_positions;
        for (auto iterator = frame->begin(); !iterator.atEnd(); ++iterator)
        {
            const auto block = iterator.currentBlock();
            if (block.isValid())
            {
                block_positions.push_back(block.position());
            }
        }
        auto* document = frame->document();
        for (const int position : block_positions)
        {
            const auto block = document->findBlock(position);
            if (!block.isValid() || QTextCursor(block).currentFrame() != frame)
            {
                continue;
            }
            QTextCursor cursor(block);
            auto format = block.blockFormat();
            format.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
            format.setProperty(QTextFormat::BlockCodeLanguage, language_name(language));
            format.setHeadingLevel(0);
            if (quote_level >= 0)
            {
                format.setProperty(QTextFormat::BlockQuoteLevel, quote_level);
            }
            format.setTopMargin(0);
            format.setBottomMargin(0);
            format.setLeftMargin(0);
            format.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
            format.clearBackground();
            cursor.setBlockFormat(format);
            QTextCharFormat characters;
            characters.setFontFamilies({theme.value(QStringLiteral("editorFontFamily")).toString()});
            characters.setProperty(
                QTextFormat::FontPixelSize, theme.value(QStringLiteral("editorFontSize"), 16));
            characters.setFontFixedPitch(true);
            characters.setFontWeight(QFont::Normal);
            characters.setFontItalic(false);
            characters.setAnchor(false);
            characters.clearBackground();
            characters.setForeground(color(theme, "textPrimary"));
            cursor.setBlockCharFormat(characters);
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.mergeCharFormat(characters);
        }
    }

    std::vector<CodeRange> code_ranges(const QTextDocument& document)
    {
        std::vector<CodeRange> ranges;
        for (auto block = document.begin(); block.isValid();)
        {
            if (!code_block(block))
            {
                block = block.next();
                continue;
            }
            CodeRange range{block.position(), block.position() + block.length() - 1,
                block.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage),
                block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel), block.text()};
            auto next = block.next();
            while (next.isValid() && code_block(next) &&
                next.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage) == range.language &&
                next.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) == range.quote_level &&
                QTextCursor(next).currentFrame() == QTextCursor(block).currentFrame())
            {
                range.text += u'\n' + next.text();
                range.end = next.position() + next.length() - 1;
                next = next.next();
            }
            ranges.push_back(range);
            block = next;
        }
        return ranges;
    }

    QString fenced_text(const QString& text, const QString& language, const QString& prefix)
    {
        int longest = 0;
        int current = 0;
        for (QChar character : text)
        {
            current = character == u'`' ? current + 1 : 0;
            longest = std::max(longest, current);
        }
        const QString fence(std::max(3, longest + 1), u'`');
        QString body = text;
        body.replace(QStringLiteral("\n"), u'\n' + prefix);
        if (body.isEmpty())
            return prefix + fence + language_name(language) + '\n' + prefix + fence;
        return prefix + fence + language_name(language) + u'\n' + prefix + body + u'\n' + prefix + fence;
    }

}
