#include "markdown_code_serialization.hpp"
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QUuid>
namespace mirrorfly
{
    MarkdownCodeSnapshot protect_markdown_code_blocks(QTextDocument& document)
    {
        MarkdownCodeSnapshot snapshot;
        snapshot.ranges = code_ranges(document);
        snapshot.tokens.resize(snapshot.ranges.size());
        for (std::size_t reverse = snapshot.ranges.size(); reverse > 0; --reverse)
        {
            const auto index = reverse - 1;
            snapshot.tokens[index] = "MIRRORFLYCODE" + QUuid::createUuid().toString(QUuid::Id128);
            QTextCursor cursor(&document);
            cursor.setPosition(snapshot.ranges[index].start);
            auto format = cursor.blockFormat();
            auto* frame = code_frame(cursor);
            const int begin = frame ? frame->firstPosition() - 1 : snapshot.ranges[index].start;
            const int end = frame ? frame->lastPosition() + 1 : snapshot.ranges[index].end;
            cursor.setPosition(begin);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
            QTextCharFormat characters;
            characters.setFontFixedPitch(false);
            characters.setFontFamilies(document.defaultFont().families());
            // Flatten only the serialization clone: Qt writes an additional fence around native frames.
            cursor.insertText(
                frame ? '\n' + snapshot.tokens[index] + '\n' : snapshot.tokens[index], characters);
            if (frame)
            {
                if (cursor.block().text().isEmpty())
                {
                    if (auto* list = cursor.block().textList())
                        list->remove(cursor.block());
                    cursor.setBlockFormat(QTextBlockFormat{});
                    cursor.setBlockCharFormat(characters);
                }
                cursor.setPosition(begin + 1);
            }
            cursor.setBlockCharFormat(characters);
            if (auto* list = cursor.block().textList())
                list->remove(cursor.block());
            format.clearProperty(QTextFormat::BlockCodeFence);
            format.clearProperty(QTextFormat::BlockCodeLanguage);
            format.setHeadingLevel(0);
            format.setIndent(0);
            cursor.setBlockFormat(format);
        }
        return snapshot;
    }
    std::vector<MarkdownInlineToken> markdown_code_bodies(const MarkdownCodeSnapshot& snapshot)
    {
        std::vector<MarkdownInlineToken> result;
        for (std::size_t index = 0; index < snapshot.ranges.size(); ++index)
            result.push_back({snapshot.tokens[index],
                fenced_text(snapshot.ranges[index].text, snapshot.ranges[index].language, {})});
        return result;
    }
}
