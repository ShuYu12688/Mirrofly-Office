#include "markdown_break.hpp"
#include "markdown_link.hpp"
#include "markdown_link_range.hpp"
#include "markdown_paragraph.hpp"

#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

namespace mirrorfly
{
    PreparedMarkdownLinks prepare_markdown_links(const QString& source)
    {
        PreparedMarkdownLinks result;
        auto prepared = source.toUtf8().toStdString();
        for (const auto& link : markdown_links(prepared))
        {
            MarkdownLinkImportToken token;
            do
            {
                token.placeholder = "MIRRORFLYLINKIMPORT" + QUuid::createUuid().toString(QUuid::Id128);
            } while (source.contains(token.placeholder));
            token.link = link;
            result.tokens.push_back(std::move(token));
        }
        for (auto iterator = result.tokens.rbegin(); iterator != result.tokens.rend(); ++iterator)
        {
            const auto& link = iterator->link;
            const auto suffix = markdown_link_suffix(link.url, link.title);
            prepared.replace(
                link.start, link.end - link.start, "[" + iterator->placeholder.toStdString() + "]" + suffix);
        }
        result.source = QString::fromUtf8(prepared.data(), static_cast<qsizetype>(prepared.size()));
        return result;
    }

    bool restore_markdown_link_import(
        QTextDocument& document, const PreparedMarkdownLinks& prepared, const QVariantMap& theme)
    {
        int position = 0;
        for (const auto& token : prepared.tokens)
        {
            auto cursor = document.find(token.placeholder, position, QTextDocument::FindCaseSensitively);
            if (cursor.isNull())
                return false;
            std::vector<MarkdownParagraphRun> runs;
            for (const auto& style : token.link.runs)
                runs.push_back({style, 1, token.link.url, token.link.title});
            insert_markdown_runs(cursor, runs, token.placeholder, theme);
            position = cursor.position();
        }
        return true;
    }
}
