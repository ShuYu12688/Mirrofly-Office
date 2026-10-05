#include "markdown_code.hpp"
#include "markdown_inline_serialization.hpp"
#include "markdown_link_range.hpp"

#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QUuid>
#include <mirrorfly/markdown.hpp>

namespace mirrorfly
{
    std::vector<MarkdownCodeSpan> protect_markdown_code_spans(QTextDocument& document)
    {
        struct Range
        {
            int start;
            int end;
            bool table;
            QString text;
            QTextCharFormat format;
        };
        std::vector<Range> ranges;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            if (code_block(block))
                continue;
            const bool table = QTextCursor(block).currentTable() != nullptr;
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                if (!fragment.isValid() || !fragment.charFormat().fontFixedPitch())
                    continue;
                if (!ranges.empty() && ranges.back().end == fragment.position() &&
                    ranges.back().format.isAnchor() == fragment.charFormat().isAnchor() &&
                    ranges.back().format.anchorHref() == fragment.charFormat().anchorHref() &&
                    ranges.back().format.toolTip() == fragment.charFormat().toolTip() &&
                    ranges.back().format.fontWeight() == fragment.charFormat().fontWeight() &&
                    ranges.back().format.fontItalic() == fragment.charFormat().fontItalic() &&
                    ranges.back().format.fontStrikeOut() == fragment.charFormat().fontStrikeOut() &&
                    ranges.back().format.property(markdown_link_identity_property) ==
                        fragment.charFormat().property(markdown_link_identity_property))
                {
                    ranges.back().end += fragment.length();
                    ranges.back().text += fragment.text();
                }
                else
                    ranges.push_back({fragment.position(), fragment.position() + fragment.length(), table,
                        fragment.text(), fragment.charFormat()});
            }
        }
        std::vector<MarkdownCodeSpan> spans;
        for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
        {
            auto token = QStringLiteral("MIRRORFLYINLINE") + QUuid::createUuid().toString(QUuid::Id128);
            const auto bytes = it->text.toUtf8().toStdString();
            auto quoted = QString::fromStdString(markdown_code_span(bytes));
            if (it->table)
                quoted.replace(u'|', QStringLiteral("\\|"));
            spans.push_back({token, quoted});
            QTextCursor cursor(&document);
            cursor.setPosition(it->start);
            cursor.setPosition(it->end, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setFontWeight(it->format.fontWeight());
            format.setFontItalic(it->format.fontItalic());
            format.setFontStrikeOut(it->format.fontStrikeOut());
            if (it->format.isAnchor())
            {
                format.setAnchor(true);
                format.setAnchorHref(it->format.anchorHref());
                format.setProperty(
                    markdown_link_identity_property, it->format.property(markdown_link_identity_property));
                if (!it->format.toolTip().isEmpty())
                    format.setToolTip(it->format.toolTip());
            }
            cursor.insertText(token, format);
        }
        return spans;
    }
}
