#include "markdown_link_serialization.hpp"
#include "markdown_break.hpp"
#include "markdown_link_range.hpp"
#include "markdown_paragraph.hpp"

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
    ProtectedMarkdownLinks protect_markdown_links(QTextDocument& document)
    {
        struct Range
        {
            int start;
            int end;
            std::vector<MarkdownInlineRun> runs;
            bool table;
            QTextCharFormat format;
        };
        std::vector<Range> ranges;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
            {
                const auto fragment = iterator.fragment();
                if (!fragment.isValid())
                    continue;
                const bool anchor = fragment.charFormat().isAnchor();
                if (!anchor)
                {
                    const auto links = markdown_links(fragment.text().toUtf8().toStdString());
                    if (std::none_of(links.begin(), links.end(), [](const auto& link)
                    {
                        return link.kind == "automatic" || link.kind == "autolink";
                    }))
                        continue;
                }
                const auto runs = markdown_fragment_runs(fragment);
                if (!ranges.empty() && ranges.back().end == fragment.position() &&
                    same_markdown_link(ranges.back().format, fragment.charFormat()))
                {
                    ranges.back().end += fragment.length();
                    ranges.back().runs.insert(ranges.back().runs.end(), runs.begin(), runs.end());
                }
                else
                    ranges.push_back({fragment.position(), fragment.position() + fragment.length(), runs,
                        QTextCursor(block).currentTable() != nullptr, fragment.charFormat()});
            }
        std::vector<MarkdownLinkToken> tokens;
        for (auto iterator = ranges.rbegin(); iterator != ranges.rend(); ++iterator)
        {
            const auto& range = *iterator;
            const auto suffix = markdown_link_suffix(range.format.anchorHref().toUtf8().toStdString(),
                range.format.toolTip().toUtf8().toStdString());
            if (range.format.isAnchor() && suffix.empty())
                return {};
            MarkdownLinkToken token;
            token.label_token =
                QStringLiteral("MIRRORFLYLINKLABEL") + QUuid::createUuid().toString(QUuid::Id128);
            token.label = QString::fromStdString(markdown_inline_label(range.runs, range.table));
            if (token.label.isEmpty())
                return {};
            token.suffix = QString::fromStdString(suffix);
            QTextCharFormat format;
            QTextCursor cursor(&document);
            cursor.setPosition(range.start);
            cursor.setPosition(range.end, QTextCursor::KeepAnchor);
            cursor.insertText(token.label_token, format);
            tokens.push_back(std::move(token));
        }
        return {true, std::move(tokens)};
    }

    bool restore_markdown_links(QString& source, const std::vector<MarkdownLinkToken>& tokens)
    {
        std::vector<MarkdownInlineToken> replacements;
        for (const auto& token : tokens)
        {
            replacements.push_back({token.label_token,
                token.suffix.isEmpty() ? token.label : u'[' + token.label + u']' + token.suffix});
        }
        return restore_markdown_inline_tokens(source, replacements);
    }
}
