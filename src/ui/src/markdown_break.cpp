#include "markdown_break.hpp"
#include "markdown_code.hpp"

#include <mirrorfly/markdown.hpp>

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

#include <algorithm>

namespace mirrorfly
{
    PreparedMarkdownBreaks prepare_markdown_hard_breaks(const QString& source)
    {
        PreparedMarkdownBreaks result;
        auto bytes = source.toUtf8().toStdString();
        const auto breaks = markdown_hard_breaks(bytes);
        for (auto iterator = breaks.rbegin(); iterator != breaks.rend(); ++iterator)
        {
            QString token;
            do
            {
                token = "MIRRORFLYBREAKIMPORT" + QUuid::createUuid().toString(QUuid::Id128);
            } while (source.contains(token));
            bytes.replace(iterator->start, iterator->end - iterator->start, token.toStdString());
            result.tokens.push_back(token);
        }
        result.source = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
        return result;
    }

    bool restore_markdown_hard_break_import(QTextDocument& document, const PreparedMarkdownBreaks& prepared)
    {
        for (const auto& token : prepared.tokens)
        {
            auto cursor = document.find(token);
            if (cursor.isNull())
                return false;
            auto format = cursor.charFormat();
            format.setProperty(markdown_hard_break_property, true);
            cursor.insertText(QString(QChar::LineSeparator), format);
        }
        return true;
    }

    std::vector<MarkdownInlineToken> protect_markdown_hard_breaks(
        QTextDocument& document, const QString& reserved_markers)
    {
        std::vector<MarkdownInlineToken> tokens;
        const auto raw = document.toRawText();
        auto occupied = raw + reserved_markers;
        for (auto position = raw.lastIndexOf(QChar::LineSeparator); position >= 0;
            position = raw.lastIndexOf(QChar::LineSeparator, position - 1))
        {
            QTextCursor cursor(&document);
            cursor.setPosition(position);
            cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
            QString token;
            // A compact marker prevents the native writer's 80-column wrapping from moving a break.
            for (ushort point = 0xE000; point <= 0xF8FF; ++point)
                if (!occupied.contains(QChar(point)))
                {
                    token = QChar(point);
                    occupied += token;
                    break;
                }
            if (token.isEmpty())
                token = "MIRRORFLYBREAKSAVE" + QUuid::createUuid().toString(QUuid::Id128);
            const auto format = cursor.charFormat();
            cursor.insertText(token, format);
            const auto replacement = format.boolProperty(markdown_hard_break_property)
                ? QStringLiteral("\\\n")
                : QStringLiteral("&#8232;");
            tokens.push_back({token, replacement});
            if (position == 0)
                break;
        }
        return tokens;
    }

    bool restore_markdown_inline_tokens(QString& source, const std::vector<MarkdownInlineToken>& tokens)
    {
        struct Replacement
        {
            qsizetype position;
            const MarkdownInlineToken* token;
            QString prefix;
        };
        std::vector<Replacement> replacements;
        for (const auto& token : tokens)
        {
            const auto position = source.indexOf(token.token);
            if (position < 0 || source.indexOf(token.token, position + token.token.size()) >= 0)
                return false;
            replacements.push_back({position, &token, {}});
        }
        std::sort(replacements.begin(), replacements.end(), [](const auto& first, const auto& second)
        {
            return first.position < second.position;
        });
        const auto lines = markdown_continuations(source.toUtf8().toStdString());
        auto line = lines.begin();
        qsizetype previous = 0;
        std::size_t byte_offset = 0;
        for (auto& replacement : replacements)
        {
            byte_offset += source.mid(previous, replacement.position - previous).toUtf8().size();
            previous = replacement.position;
            while (line != lines.end() && byte_offset > line->end)
                ++line;
            if (line == lines.end() || byte_offset < line->start)
                return false;
            replacement.prefix = QString::fromStdString(line->prefix);
        }
        for (auto iterator = replacements.rbegin(); iterator != replacements.rend(); ++iterator)
        {
            auto value = iterator->token->source;
            value.replace(u'\n', u'\n' + iterator->prefix);
            auto count = iterator->token->token.size();
            if (iterator->token->source.endsWith(u'\n'))
            {
                const auto tail_start = iterator->position + count;
                auto tail_end = source.indexOf(u'\n', tail_start);
                if (tail_end < 0)
                    tail_end = source.size();
                const auto tail = source.mid(tail_start, tail_end - tail_start);
                value += QString::fromStdString(markdown_escape_paragraph_start(tail.toUtf8().toStdString()));
                count += tail.size();
            }
            source.replace(iterator->position, count, value);
        }
        return true;
    }

    bool can_insert_markdown_hard_break(QTextCursor cursor)
    {
        if (cursor.hasSelection() || code_frame(cursor) || cursor.currentTable() ||
            cursor.blockFormat().headingLevel() != 0)
            return false;
        const int end = cursor.block().position() + cursor.block().length() - 1;
        const int position = cursor.position();
        if (position == cursor.block().position() || position == end)
            return true;
        cursor.setPosition(position - 1);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        const bool left_code = cursor.charFormat().fontFixedPitch();
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        return !left_code || !cursor.charFormat().fontFixedPitch();
    }
}
