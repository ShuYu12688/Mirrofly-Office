#include "markdown_blocks.hpp"
#include "markdown_container.hpp"
#include "markdown_list_tree.hpp"
#include <QHash>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
namespace mirrorfly
{
    void markdown_set_list_marker(const QTextBlock& block, QTextBlockFormat::MarkerType marker)
    {
        QTextCursor cursor(block);
        auto format = block.blockFormat();
        format.setMarker(marker);
        cursor.setBlockFormat(format);
    }
    void apply_markdown_list(QTextCursor& cursor, const QString& action)
    {
        const auto blocks = selected_markdown_blocks(cursor);
        QHash<QTextList*, QTextList*> targets;
        QTextList* plain_target = nullptr;
        for (const auto& block : blocks)
        {
            QTextCursor line(block);
            auto* original = block.textList();
            const auto marker = block.blockFormat().marker();
            auto format = original ? original->format() : QTextListFormat{};
            format.setIndent(original ? format.indent() : 1);
            format.setStyle(action == QStringLiteral("ordered") ? QTextListFormat::ListDecimal
                                                                : QTextListFormat::ListDisc);
            if (action == QStringLiteral("ordered") &&
                (!original || original->format().style() != QTextListFormat::ListDecimal))
                format.setNumberSuffix(QStringLiteral("."));
            auto* target = original ? targets.value(original) : plain_target;
            if (target)
                target->add(block);
            else
            {
                target = line.createList(format);
                if (original)
                    targets.insert(original, target);
                else
                    plain_target = target;
            }
            markdown_set_list_marker(block,
                action != QStringLiteral("task") ? QTextBlockFormat::MarkerType::NoMarker
                    : marker == QTextBlockFormat::MarkerType::Checked
                    ? marker
                    : QTextBlockFormat::MarkerType::Unchecked);
        }
    }

    bool move_markdown_list_blocks(const std::vector<QTextBlock>& blocks, int delta, int pivot)
    {
        const auto tree = markdown_list_tree(*blocks.front().document());
        struct Move
        {
            QTextBlock block;
            QTextList* original;
            QTextListFormat format;
        };
        std::vector<Move> moves;
        for (const auto& block : blocks)
        {
            if (!block.textList())
            {
                moves.push_back({block, nullptr, {}});
                continue;
            }
            auto format = block.textList()->format();
            format.setIndent(format.indent() + delta);
            format.setStart(block.textList()->format().start() + block.textList()->itemNumber(block));
            moves.push_back({block, block.textList(), format});
        }
        // A new list changes only the moved blocks; changing the original list would shift siblings too.
        QHash<QTextList*, QTextList*> targets;
        QHash<QTextList*, QVariantMap> groups;
        for (const auto& move : moves)
        {
            if (!move.original)
            {
                if (QTextCursor(move.block).currentTable())
                    continue;
                QTextCursor line(move.block);
                auto format = line.blockFormat();
                format.setIndent(format.indent() + delta);
                line.setBlockFormat(format);
                continue;
            }
            auto* target = targets.value(move.original);
            if (!target && move.format.indent() == pivot + delta)
            {
                const auto candidate =
                    markdown_preceding_list(tree, moves.front().block, move.format.indent());
                if (candidate.textList() && markdown_same_list_container(move.block, candidate) &&
                    markdown_list_level(candidate) == move.format.indent() &&
                    candidate.textList()->format().style() == move.format.style() &&
                    candidate.textList()->format().numberSuffix() == move.format.numberSuffix())
                    target = candidate.textList();
            }
            if (target)
                target->add(move.block);
            else
            {
                QTextCursor line(move.block);
                target = line.createList(move.format);
            }
            targets.insert(move.original, target);
            if (delta > 0 || !move.block.text().isEmpty())
                continue;
            auto block_format = move.block.blockFormat();
            auto path = block_format.property(markdown_container_property).toList();
            for (qsizetype index = path.size(); index > 0; --index)
            {
                auto node = path[index - 1].toMap();
                if (node.value("kind") != "listItem")
                    continue;
                if (!groups.contains(target))
                {
                    const auto sibling_path =
                        target->item(0).blockFormat().property(markdown_container_property).toList();
                    QVariantMap group;
                    if (target->item(0) != move.block && !sibling_path.isEmpty())
                        group = sibling_path.back().toMap();
                    else
                    {
                        group = node;
                        group["listIdentity"] =
                            QVariant::fromValue((qulonglong{1} << 62) + target->objectIndex());
                    }
                    groups.insert(target, group);
                }
                node["listIdentity"] = groups.value(target).value("listIdentity");
                node["tight"] = groups.value(target).value("tight", true);
                path[index - 1] = node;
                break;
            }
            block_format.setProperty(markdown_container_property, path);
            QTextCursor(move.block).setBlockFormat(block_format);
        }
        return true;
    }
}
