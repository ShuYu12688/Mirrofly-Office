#include "markdown_inline.hpp"
#include "markdown_break.hpp"
#include "markdown_code.hpp"
#include "markdown_link_range.hpp"

#include <mirrorfly/markdown.hpp>

#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

#include <algorithm>

namespace
{
    QTextCharFormat inline_code_format(const QVariantMap& theme, bool enabled, int heading_level)
    {
        QTextCharFormat format;
        format.setFontStyleName({});
        format.setFontFixedPitch(enabled);
        format.setFontFamilies({theme.value(enabled ? "editorFontFamily" : "fontFamily").toString()});
        format.setFontWeight(!enabled && heading_level > 0 ? QFont::DemiBold : QFont::Normal);
        format.setFontItalic(false);
        format.setFontStrikeOut(false);
        format.setFontUnderline(false);
        format.setAnchor(false);
        format.setAnchorHref({});
        format.setForeground(QColor(theme.value("textPrimary").toString()));
        QBrush background(Qt::NoBrush);
        if (enabled)
        {
            const auto value = theme.value("markdownInlineCodeBackground", theme.value("accentSoft"));
            background = QBrush(QColor(value.toString()));
        }
        format.setBackground(background);
        return format;
    }
}

namespace mirrorfly
{
    void apply_markdown_inline_style(QTextCursor& cursor, const QString& action)
    {
        if (!cursor.hasSelection())
        {
            const int position = cursor.position();
            cursor.insertText(QStringLiteral("文字"));
            cursor.setPosition(position, QTextCursor::KeepAnchor);
        }
        QTextCharFormat format;
        format.setFontStyleName(QString{});
        if (action == QStringLiteral("bold"))
        {
            format.setFontWeight(
                cursor.charFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
        }
        else if (action == QStringLiteral("italic"))
        {
            format.setFontItalic(!cursor.charFormat().fontItalic());
        }
        else if (action == QStringLiteral("strike"))
        {
            format.setFontStrikeOut(!cursor.charFormat().fontStrikeOut());
        }
        cursor.mergeCharFormat(format);
    }

    void style_markdown_inline_code(QTextDocument& document, const QVariantMap& theme)
    {
        struct Range
        {
            int start;
            int length;
            QString text;
        };
        std::vector<Range> ranges;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            if (code_block(block))
                continue;
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                if (!fragment.isValid() || !fragment.charFormat().fontFixedPitch())
                    continue;
                auto text = fragment.text();
                ranges.push_back({fragment.position(), fragment.length(), text});
            }
        }
        for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
        {
            QTextCursor cursor(&document);
            cursor.setPosition(it->start);
            cursor.setPosition(it->start + it->length, QTextCursor::KeepAnchor);
            if (cursor.selectedText() != it->text)
                cursor.insertText(it->text, cursor.charFormat());
            cursor.setPosition(it->start);
            cursor.setPosition(it->start + static_cast<int>(it->text.size()), QTextCursor::KeepAnchor);
            auto format = inline_code_format(theme, true, 0);
            format.clearProperty(QTextFormat::FontWeight);
            format.clearProperty(QTextFormat::FontItalic);
            format.clearProperty(QTextFormat::FontStrikeOut);
            format.clearProperty(QTextFormat::IsAnchor);
            format.clearProperty(QTextFormat::AnchorHref);
            cursor.mergeCharFormat(format);
        }
    }

    bool markdown_inline_code_range(QTextCursor& cursor)
    {
        if (code_frame(cursor) || code_block(cursor.block()))
            return false;
        const int begin = cursor.block().position();
        const int finish = begin + cursor.block().length() - 1;
        const auto format_at = [&](int position)
        {
            QTextCursor probe(cursor.document());
            if (position < begin || position >= finish)
                return QTextCharFormat{};
            probe.setPosition(position);
            probe.setPosition(position + 1, QTextCursor::KeepAnchor);
            return probe.charFormat();
        };
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        if (start == end && !format_at(start).fontFixedPitch() && start > begin)
            --start;
        const auto format = format_at(start);
        if (!format.fontFixedPitch() || format.isImageFormat())
            return false;
        const auto matches = [&](int position)
        {
            const auto other = format_at(position);
            return other.fontFixedPitch() && !other.isImageFormat() &&
                ((!format.isAnchor() && !other.isAnchor()) || same_markdown_link(format, other));
        };
        for (int position = start; position < end; ++position)
            if (!matches(position))
                return false;
        end = std::max(end, start + 1);
        while (start > begin && matches(start - 1))
            --start;
        while (end < finish && matches(end))
            ++end;
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        return true;
    }

    bool edit_markdown_inline_code(QTextCursor& cursor, const QString& action, const QVariantMap& theme)
    {
        auto existing = cursor;
        const bool all_code = markdown_inline_code_range(existing);
        if (action == "removeInlineCode" && !all_code)
            return false;
        if (action == "removeInlineCode")
            cursor = existing;
        if (!cursor.hasSelection())
        {
            if (all_code)
                cursor = existing;
            else
            {
                const int start = cursor.position();
                cursor.insertText(QStringLiteral("代码"));
                cursor.setPosition(start, QTextCursor::KeepAnchor);
            }
        }
        const int selection_start = cursor.selectionStart();
        const int selection_end = cursor.selectionEnd();
        if (!all_code)
            for (int position = selection_end - 1; position >= selection_start; --position)
                if (cursor.document()->characterAt(position) == QChar::LineSeparator)
                {
                    QTextCursor newline(cursor.document());
                    newline.setPosition(position);
                    newline.setPosition(position + 1, QTextCursor::KeepAnchor);
                    auto literal = newline.charFormat();
                    literal.clearProperty(markdown_hard_break_property);
                    newline.insertText(" ", literal);
                }
        cursor.setPosition(selection_start);
        cursor.setPosition(selection_end, QTextCursor::KeepAnchor);
        auto format = inline_code_format(theme, !all_code, cursor.blockFormat().headingLevel());
        format.clearProperty(QTextFormat::IsAnchor);
        format.clearProperty(QTextFormat::AnchorHref);
        format.clearProperty(QTextFormat::FontWeight);
        format.clearProperty(QTextFormat::FontItalic);
        format.clearProperty(QTextFormat::FontStrikeOut);
        format.setProperty(markdown_hard_break_property, false);
        cursor.mergeCharFormat(format);
        return true;
    }
}
