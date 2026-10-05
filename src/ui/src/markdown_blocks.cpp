#include "markdown_blocks.hpp"
#include "markdown_list_tree.hpp"
#include <QSet>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <algorithm>
namespace mirrorfly
{
    bool edit_markdown_list(QTextCursor& cursor, const QString& action, const QVariantMap& options)
    {
        auto blocks = selected_markdown_blocks(cursor);
        if (blocks.empty() || !markdown_editable_list(blocks.front()))
            return false;
        if (action == QStringLiteral("taskSet"))
        {
            const auto checked = options.value(QStringLiteral("checked"));
            if (checked.metaType().id() != QMetaType::Bool ||
                !std::all_of(blocks.begin(), blocks.end(), markdown_task_item))
                return false;
            auto marker = QTextBlockFormat::MarkerType::Unchecked;
            if (checked.toBool())
                marker = QTextBlockFormat::MarkerType::Checked;
            for (const auto& block : blocks)
                markdown_set_list_marker(block, marker);
            return true;
        }
        const auto tree = markdown_list_tree(*cursor.document());
        const int pivot = markdown_list_level(blocks.front());
        const int delta = action == QStringLiteral("listIndent") ? 1 : -1;
        if ((delta > 0 && !markdown_preceding_list(tree, blocks.front(), pivot).isValid()) ||
            (delta < 0 && pivot <= 1))
            return false;
        const auto first = std::find_if(tree.begin(), tree.end(), [&](const auto& node)
        {
            return node.block == blocks.front();
        });
        if (first == tree.end())
            return false;
        qulonglong identity = 0;
        for (const auto& node : first->path)
            if (node.toMap().value("kind") == "listItem")
                identity = node.toMap().value("identity").toULongLong();
        for (const auto& block : blocks)
        {
            const auto found = std::find_if(tree.begin(), tree.end(), [&](const auto& node)
            {
                return node.block == block;
            });
            if (found == tree.end() || found->depth < pivot)
                return false;
            const bool owned = std::any_of(found->path.begin(), found->path.end(), [&](const auto& node)
            {
                return node.toMap().value("identity").toULongLong() == identity;
            });
            if (!owned && !markdown_same_list_container(blocks.front(), block))
                return false;
        }
        blocks = markdown_list_descendants(tree, blocks);
        QSet<int> moving;
        for (const auto& block : blocks)
            moving.insert(block.position());
        for (const auto& node : tree)
            if (moving.contains(node.block.position()) && node.depth + delta > 8)
                return false;
        return move_markdown_list_blocks(blocks, delta, pivot);
    }

    QVariantMap inspect_markdown_list(const QTextCursor& cursor)
    {
        const auto block = cursor.block();
        const bool editable = markdown_editable_list(block);
        return {{QStringLiteral("inList"), editable},
            {QStringLiteral("listLevel"), markdown_list_level(block)},
            {QStringLiteral("taskItem"), editable && markdown_task_item(block)},
            {QStringLiteral("taskChecked"),
                block.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked},
            {QStringLiteral("canIndentList"), markdown_can_indent(block)},
            {QStringLiteral("canOutdentList"), editable && markdown_list_level(block) > 1}};
    }
}
