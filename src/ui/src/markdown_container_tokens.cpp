#include "markdown_code.hpp"
#include "markdown_container.hpp"
#include "markdown_table.hpp"
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QUuid>
#include <algorithm>
namespace mirrorfly
{
    std::vector<MarkdownInlineToken> protect_markdown_empty_heads(QTextDocument& document)
    {
        std::vector<MarkdownInlineToken> result;
        QSet<qulonglong> heads;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            if (!block.textList() || !block.blockFormat().boolProperty(markdown_container_head_property) ||
                QTextCursor(block).currentTable() || code_frame(QTextCursor(block)))
                continue;
            const auto path = block.blockFormat().property(markdown_container_property).toList();
            qulonglong identity = 0;
            for (auto node = path.crbegin(); node != path.crend(); ++node)
                if (node->toMap().value("kind") == "listItem")
                {
                    identity = node->toMap().value("identity").toULongLong();
                    break;
                }
            if (identity == 0 || heads.contains(identity))
                continue;
            heads.insert(identity);
            if (!block.text().isEmpty())
                continue;
            const auto token =
                QStringLiteral("MIRRORFLYEMPTYOWNER") + QUuid::createUuid().toString(QUuid::Id128);
            // Protect the visible owner before removing its adjacent code-frame boundary.
            QTextCursor(block).insertText(token, QTextCharFormat{});
            result.push_back({token, {}});
        }
        return result;
    }

    std::vector<MarkdownContainerToken> collect_markdown_containers(
        QTextDocument& document, const QStringList& identifiers)
    {
        std::vector<MarkdownContainerToken> result;
        MarkdownContainerState state;
        const QSet<QString> known(identifiers.cbegin(), identifiers.cend());
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            auto token = block.text();
            auto* table = QTextCursor(block).currentTable();
            if (table && token.startsWith("MIRRORFLYTABLE"))
                token = token.left(QStringLiteral("MIRRORFLYTABLE").size() + 32);
            if (known.contains(token))
            {
                QVariantList path;
                if (table)
                    path = state.path(block, table->format().property(markdown_container_property).toList(),
                        markdown_table_quote_level(table));
                else
                    path = state.path(block);
                result.push_back({token, path, table ? 0 : block.blockFormat().headingLevel(),
                    token.startsWith("MIRRORFLYEMPTYOWNER")});
            }
        }
        return result;
    }

    bool restore_markdown_block_tokens(QString& source, const std::vector<MarkdownInlineToken>& bodies,
        const std::vector<MarkdownContainerToken>& containers)
    {
        for (const auto& body : bodies)
        {
            const auto found = std::find_if(containers.begin(), containers.end(), [&](const auto& item)
            {
                return item.token == body.token;
            });
            if (found == containers.end() || !source.contains(body.token))
                return false;
            if (body.source.isEmpty() && found->empty_head && found + 1 != containers.end() &&
                (found + 1)->token.startsWith("MIRRORFLYCODE") && (found + 1)->path == found->path)
            {
                const auto begin = source.indexOf(body.token);
                const auto next = source.indexOf((found + 1)->token, begin + body.token.size());
                if (next < 0)
                    return false;
                // Put the fence on the marker line so an empty child does not interrupt a paragraph.
                source.remove(begin, next - begin);
                continue;
            }
            QString continuation;
            for (const auto& value : found->path)
                continuation += value.toMap().value("continuation").toString();
            auto replacement = body.source;
            replacement.replace('\n', '\n' + continuation);
            source.replace(body.token, replacement);
        }
        return true;
    }
}
