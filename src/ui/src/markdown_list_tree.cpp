#include "markdown_list_tree.hpp"
#include "markdown_code.hpp"
#include "markdown_container.hpp"
#include "markdown_table.hpp"

#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <algorithm>

namespace
{
    qulonglong item_identity(const QVariantList& path)
    {
        for (auto node = path.crbegin(); node != path.crend(); ++node)
            if (node->toMap().value("kind") == "listItem")
                return node->toMap().value("identity").toULongLong();
        return 0;
    }
}

namespace mirrorfly
{
    std::vector<MarkdownListNode> markdown_list_tree(const QTextDocument& document)
    {
        std::vector<MarkdownListNode> result;
        MarkdownContainerState state;
        QSet<QTextTable*> tables;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            auto* table = QTextCursor(block).currentTable();
            if (table && tables.contains(table))
                continue;
            QVariantList path;
            if (table)
            {
                tables.insert(table);
                path = state.path(block, table->format().property(markdown_container_property).toList(),
                    markdown_table_quote_level(table));
            }
            else
                path = state.path(block);
            const int depth = static_cast<int>(std::count_if(path.begin(), path.end(), [](const auto& node)
            {
                return node.toMap().value("kind") == "listItem";
            }));
            result.push_back({block, path, depth});
        }
        return result;
    }

    int markdown_list_level(const QTextBlock& block)
    {
        return block.textList() ? block.textList()->format().indent() : block.blockFormat().indent();
    }

    bool markdown_same_list_container(const QTextBlock& first, const QTextBlock& second)
    {
        return QTextCursor(first).currentFrame() == QTextCursor(second).currentFrame() &&
            first.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) ==
            second.blockFormat().intProperty(QTextFormat::BlockQuoteLevel);
    }

    bool markdown_editable_list(const QTextBlock& block)
    {
        const QTextCursor cursor(block);
        return block.textList() && !cursor.currentTable() && !code_frame(cursor) &&
            !block.blockFormat().hasProperty(QTextFormat::BlockCodeFence) &&
            cursor.currentFrame() == cursor.document()->rootFrame();
    }

    bool markdown_task_item(const QTextBlock& block)
    {
        return block.textList() && block.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker;
    }

    QTextBlock markdown_preceding_list(
        const std::vector<MarkdownListNode>& tree, const QTextBlock& first, int depth)
    {
        for (auto node = tree.crbegin(); node != tree.crend(); ++node)
        {
            if (node->block.position() >= first.position())
                continue;
            if (node->depth == 0 && node->block.text().isEmpty() &&
                !QTextCursor(node->block).currentTable() && !code_frame(QTextCursor(node->block)) &&
                !node->block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
                continue;
            if (node->depth < depth)
                return {};
            if (node->depth == depth && markdown_editable_list(node->block))
                return markdown_same_list_container(first, node->block) ? node->block : QTextBlock{};
        }
        return {};
    }

    std::vector<QTextBlock> markdown_list_descendants(
        const std::vector<MarkdownListNode>& tree, const std::vector<QTextBlock>& selected)
    {
        auto result = selected;
        QHash<int, std::size_t> indices;
        for (std::size_t index = 0; index < tree.size(); ++index)
            indices.insert(tree[index].block.position(), index);
        for (const auto& parent : selected)
        {
            if (!indices.contains(parent.position()) || !markdown_editable_list(parent))
                continue;
            const auto root = tree.begin() + static_cast<std::ptrdiff_t>(indices.value(parent.position()));
            const auto identity = item_identity(root->path);
            if (identity == 0)
                continue;
            for (auto node = root + 1; node != tree.end(); ++node)
            {
                const bool owned = std::any_of(node->path.begin(), node->path.end(), [&](const auto& value)
                {
                    return value.toMap().value("identity").toULongLong() == identity;
                });
                if (!owned)
                {
                    if (node->block.text().isEmpty() && !node->block.textList() &&
                        !QTextCursor(node->block).currentTable() && !code_frame(QTextCursor(node->block)) &&
                        !node->block.blockFormat().hasProperty(
                            QTextFormat::BlockTrailingHorizontalRulerWidth))
                        continue;
                    break;
                }
                result.push_back(node->block);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& first, const auto& second)
        {
            return first.position() < second.position();
        });
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    bool markdown_can_indent(const QTextBlock& block)
    {
        if (!markdown_editable_list(block))
            return false;
        const auto tree = markdown_list_tree(*block.document());
        if (markdown_list_level(block) >= 8 ||
            !markdown_preceding_list(tree, block, markdown_list_level(block)).isValid())
            return false;
        const auto descendants = markdown_list_descendants(tree, {block});
        QSet<int> positions;
        for (const auto& child : descendants)
            positions.insert(child.position());
        return std::all_of(tree.begin(), tree.end(), [&](const auto& node)
        {
            return !positions.contains(node.block.position()) || node.depth < 8;
        });
    }
}
