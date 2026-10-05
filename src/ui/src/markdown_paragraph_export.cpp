#include "markdown_code.hpp"
#include "markdown_link_range.hpp"
#include "markdown_paragraph.hpp"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

namespace mirrorfly
{
    ProtectedMarkdownParagraphs protect_markdown_paragraphs(
        QTextDocument& document, const QString& reserved_markers, bool table_cells)
    {
        ProtectedMarkdownParagraphs result{true, {}};
        auto occupied = document.toRawText() + reserved_markers;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            const QTextCursor probe(block);
            if ((block.text().isEmpty() && block.blockFormat().headingLevel() == 0 &&
                    (block.textList() ||
                        block.blockFormat().property(markdown_container_property).toList().isEmpty())) ||
                code_block(block) || code_frame(probe) || (probe.currentTable() != nullptr) != table_cells ||
                block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
                continue;
            std::vector<MarkdownParagraphRun> runs;
            QTextCharFormat previous;
            std::size_t identity = 0;
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
            {
                const auto fragment = iterator.fragment();
                if (!fragment.isValid())
                    continue;
                const auto format = fragment.charFormat();
                if (format.isImageFormat())
                    return {};
                if (format.isAnchor() && !same_markdown_link(previous, format))
                    ++identity;
                for (const auto& style : markdown_fragment_runs(fragment))
                    runs.push_back({style, format.isAnchor() ? identity : 0,
                        format.isAnchor() ? format.anchorHref().toUtf8().toStdString() : std::string{},
                        format.isAnchor() ? format.toolTip().toUtf8().toStdString() : std::string{}});
                previous = format;
            }
            const auto source = markdown_paragraph_text(runs, table_cells);
            if (source.empty() && !runs.empty())
                return {};
            QString token;
            for (ushort point = 0xE000; point <= 0xF8FF; ++point)
                if (!occupied.contains(QChar(point)))
                {
                    token = QChar(point);
                    occupied += token;
                    break;
                }
            if (token.isEmpty())
                return {};
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            cursor.insertText(token, QTextCharFormat{});
            result.tokens.push_back({token, QString::fromStdString(source)});
        }
        return result;
    }
}
