#include "markdown_inline_serialization.hpp"
#include "markdown_code.hpp"
#include "markdown_link_range.hpp"

#include <mirrorfly/markdown.hpp>

#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QUuid>

#include <algorithm>

namespace mirrorfly
{
    void prepare_markdown_inline_formats(QTextDocument& document, const QString& markers)
    {
        struct Replacement
        {
            int start;
            int end;
            QString text;
            QTextCharFormat format;
        };
        std::vector<Replacement> replacements;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            if (code_block(block))
            {
                continue;
            }
            const bool in_table = QTextCursor(block).currentTable() != nullptr;
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
            {
                const auto fragment = iterator.fragment();
                if (!fragment.isValid())
                {
                    continue;
                }
                auto text = fragment.text();
                auto format = fragment.charFormat();
                text.replace(QChar(0x00A0), markers[3]);
                if (in_table)
                {
                    text.replace(u'|', markers[2]);
                }
                QString wrapper;
                if (!format.fontFixedPitch())
                {
                    if (format.fontWeight() >= QFont::Bold && block.blockFormat().headingLevel() == 0)
                    {
                        wrapper += markers[0];
                    }
                    if (format.fontItalic())
                    {
                        wrapper += markers[1];
                    }
                }
                if (!wrapper.isEmpty())
                {
                    int first = 0;
                    int last = static_cast<int>(text.size());
                    while (first < last && text[first].isSpace())
                    {
                        ++first;
                    }
                    while (last > first && text[last - 1].isSpace())
                    {
                        --last;
                    }
                    QString suffix = wrapper;
                    std::reverse(suffix.begin(), suffix.end());
                    text =
                        text.left(first) + wrapper + text.mid(first, last - first) + suffix + text.mid(last);
                }
                format.setFontWeight(QFont::Normal);
                format.setFontItalic(false);
                format.setFontStyleName(QString{});
                replacements.push_back(
                    {fragment.position(), fragment.position() + fragment.length(), text, format});
            }
        }
        for (auto iterator = replacements.rbegin(); iterator != replacements.rend(); ++iterator)
        {
            QTextCursor cursor(&document);
            cursor.setPosition(iterator->start);
            cursor.setPosition(iterator->end, QTextCursor::KeepAnchor);
            cursor.insertText(iterator->text, iterator->format);
        }
    }
}
