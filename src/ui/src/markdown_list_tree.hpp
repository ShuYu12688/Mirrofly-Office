#pragma once

#include <QTextBlock>
#include <QTextFormat>
#include <QVariantList>
#include <vector>

class QTextDocument;

namespace mirrorfly
{
    struct MarkdownListNode
    {
        QTextBlock block;
        QVariantList path;
        int depth = 0;
    };
    std::vector<MarkdownListNode> markdown_list_tree(const QTextDocument& document);
    int markdown_list_level(const QTextBlock& block);
    bool markdown_editable_list(const QTextBlock& block);
    bool markdown_task_item(const QTextBlock& block);
    bool markdown_same_list_container(const QTextBlock& first, const QTextBlock& second);
    QTextBlock markdown_preceding_list(
        const std::vector<MarkdownListNode>& tree, const QTextBlock& first, int depth);
    std::vector<QTextBlock> markdown_list_descendants(
        const std::vector<MarkdownListNode>& tree, const std::vector<QTextBlock>& selected);
    bool markdown_can_indent(const QTextBlock& block);
    void markdown_set_list_marker(const QTextBlock& block, QTextBlockFormat::MarkerType marker);
    bool move_markdown_list_blocks(const std::vector<QTextBlock>& blocks, int delta, int pivot);
}
