#include "markdown_code.hpp"
#include "markdown_container.hpp"
#include "markdown_paragraph.hpp"
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
namespace
{
    void restore_empty_head(QTextDocument& document, QTextFrame* frame,
        const std::vector<mirrorfly::MarkdownContainerInfo>& containers, QSet<qulonglong>& heads)
    {
        auto owner = document.findBlock(frame->firstPosition()).previous();
        if (!owner.isValid() || !owner.text().isEmpty() || QTextCursor(owner).currentTable() ||
            mirrorfly::code_frame(QTextCursor(owner)))
            return;
        mirrorfly::MarkdownParagraphInfo paragraph;
        paragraph.containers = containers;
        while (!paragraph.containers.empty() && paragraph.containers.back().kind != "listItem")
            paragraph.containers.pop_back();
        if (paragraph.containers.empty() || heads.contains(paragraph.containers.back().identity))
            return;
        const auto& item = paragraph.containers.back();
        for (const auto& node : paragraph.containers)
        {
            paragraph.list_depth += node.kind == "listItem" ? 1 : 0;
            paragraph.quote_level += node.kind == "quote" ? 1 : 0;
        }
        if (auto* inherited = owner.textList();
            inherited && inherited->format().indent() != paragraph.list_depth)
            inherited->remove(owner);
        if (!owner.textList())
        {
            QTextList* siblings = nullptr;
            for (auto following = document.findBlock(frame->lastPosition()).next(); following.isValid();
                following = following.next())
            {
                const auto path =
                    following.blockFormat().property(mirrorfly::markdown_container_property).toList();
                if (following.textList() && !path.isEmpty() &&
                    path.back().toMap().value("listIdentity").toULongLong() == item.list_identity)
                {
                    siblings = following.textList();
                    break;
                }
            }
            if (siblings)
            {
                siblings->add(owner);
                auto format = siblings->format();
                format.setStart(static_cast<int>(item.ordinal));
                siblings->setFormat(format);
            }
            else
            {
                QTextListFormat format;
                format.setStyle(item.ordered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
                format.setIndent(paragraph.list_depth);
                format.setStart(static_cast<int>(item.ordinal));
                format.setNumberSuffix(QString(QChar::fromLatin1(item.delimiter)));
                QTextCursor(owner).createList(format);
            }
        }
        auto format = owner.blockFormat();
        format.clearProperty(QTextFormat::BlockCodeFence);
        format.clearProperty(QTextFormat::BlockCodeLanguage);
        format.setProperty(QTextFormat::BlockQuoteLevel, paragraph.quote_level);
        QTextCursor(owner).setBlockFormat(format);
        mirrorfly::set_markdown_containers(owner, paragraph);
        heads.insert(item.identity);
    }
}
namespace mirrorfly
{
    bool load_markdown_block_containers(
        QTextDocument& document, const QString& source, const QVariantMap& theme)
    {
        const auto metadata = markdown_blocks(source.toUtf8().toStdString());
        QSet<QTextFrame*> visited;
        QSet<qulonglong> heads;
        std::size_t index = 0;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            const auto head_path = block.blockFormat().property(markdown_container_property).toList();
            if (block.textList() && !head_path.isEmpty())
                heads.insert(head_path.back().toMap().value("identity").toULongLong());
            const QTextCursor cursor(block);
            auto* table = cursor.currentTable();
            auto* frame = table ? table : code_frame(cursor);
            QString kind;
            if (frame)
            {
                if (visited.contains(frame))
                    continue;
                visited.insert(frame);
                kind = table ? "table" : "code";
            }
            else if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
                kind = "thematicBreak";
            else
                continue;
            if (index >= metadata.size() || QString::fromStdString(metadata[index].kind) != kind)
                return false;
            const auto& item = metadata[index++];
            if (table)
            {
                if (item.columns != static_cast<unsigned>(table->columns()) ||
                    item.cells.size() != static_cast<std::size_t>(table->rows() * table->columns()))
                    return false;
                for (std::size_t cell_index = 0; cell_index < item.cells.size(); ++cell_index)
                {
                    const auto cell = table->cellAt(static_cast<int>(cell_index) / table->columns(),
                        static_cast<int>(cell_index) % table->columns());
                    auto characters = cell.firstCursorPosition();
                    auto cell_format = characters.blockFormat();
                    characters.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                    insert_markdown_runs(characters, item.cells[cell_index],
                        "MIRRORFLYCELL" + QString::number(index) + ":" + QString::number(cell_index) + ":",
                        theme);
                    cell_format.clearProperty(QTextFormat::BlockCodeFence);
                    cell_format.clearProperty(QTextFormat::BlockCodeLanguage);
                    cell_format.clearProperty(QTextFormat::BlockQuoteLevel);
                    cell_format.setIndent(0);
                    characters.setBlockFormat(cell_format);
                }
                block = document.findBlock(table->firstPosition());
            }
            const auto path = markdown_container_values(item.containers);
            if (frame)
            {
                auto format = frame->frameFormat();
                format.setProperty(markdown_container_property, path);
                frame->setFrameFormat(format);
                restore_empty_head(document, frame, item.containers, heads);
            }
            if (!table)
            {
                for (auto child = block; child.isValid(); child = child.next())
                {
                    if (frame && child.position() > frame->lastPosition())
                        break;
                    auto format = child.blockFormat();
                    format.setProperty(markdown_container_property, path);
                    format.setProperty(markdown_container_head_property, false);
                    int depth = 0;
                    int quotes = 0;
                    for (const auto& node : item.containers)
                    {
                        depth += node.kind == "listItem" ? 1 : 0;
                        quotes += node.kind == "quote" ? 1 : 0;
                    }
                    format.setIndent(depth);
                    format.setProperty(QTextFormat::BlockQuoteLevel, quotes);
                    QTextCursor(child).setBlockFormat(format);
                    if (!frame)
                        break;
                }
            }
        }
        return index == metadata.size();
    }
}
