#include "word_document_structure.hpp"
#include "word_cell_borders.hpp"
#include "word_format_properties.hpp"
#include "word_units.hpp"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <algorithm>
#include <set>

namespace mirrorfly
{
    bool extract_word_structure(const QTextDocument& editor, const WordDocument& original,
        WordDocument& current, const std::map<int, std::size_t>& blocks, std::string& error)
    {
        std::set<std::size_t> seen_tables;
        using Iterator = QTextFrame::iterator;
        using Flow = std::vector<WordBlock>;
        const auto read = [&](const auto& self, Iterator iterator, Iterator end, Flow& output) -> bool
        {
            output.clear();
            for (; iterator != end; ++iterator)
            {
                if (auto* frame = iterator.currentFrame())
                {
                    auto* table = qobject_cast<QTextTable*>(frame);
                    const auto id = frame->frameFormat().property(word_source_table_property).toULongLong();
                    if (!table || !id || id > original.tables.size() || !seen_tables.insert(id - 1).second)
                    {
                        error = "新建、复制或嵌套框架尚不能写回原 DOCX，原文件未改动。";
                        return false;
                    }
                    auto& value = current.tables[id - 1];
                    if (table->rows() != static_cast<int>(value.rows) ||
                        table->columns() != static_cast<int>(value.column_widths.size()))
                    {
                        error = "表格行列结构已变化，暂未开放此类修改。";
                        return false;
                    }
                    output.push_back({WordBlock::Kind::Table, static_cast<std::size_t>(id - 1)});
                    for (auto& cell : value.cells)
                    {
                        const auto actual =
                            table->cellAt(static_cast<int>(cell.row), static_cast<int>(cell.column));
                        if (!actual.isValid() || actual.row() != static_cast<int>(cell.row) ||
                            actual.column() != static_cast<int>(cell.column) ||
                            actual.rowSpan() != static_cast<int>(cell.row_span) ||
                            actual.columnSpan() != static_cast<int>(cell.column_span))
                        {
                            error = "表格合并区域已变化，暂未开放此类修改。";
                            return false;
                        }
                        if (!self(self, actual.begin(), actual.end(), cell.blocks))
                            return false;
                        const auto format = actual.format().toTableCellFormat();
                        if (!extract_word_cell_borders(format, cell))
                        {
                            error = "单元格边框来源不完整，未写回不确定样式。";
                            return false;
                        }
                        const auto fill = format.background().style() == Qt::NoBrush
                            ? QString{}
                            : format.background().color().name();
                        if (fill.compare(QString::fromStdString(cell.background), Qt::CaseInsensitive) != 0)
                            cell.background = fill.toStdString();
                        cell.vertical_alignment = 0;
                        if (format.verticalAlignment() == QTextCharFormat::AlignMiddle)
                            cell.vertical_alignment = 1;
                        else if (format.verticalAlignment() == QTextCharFormat::AlignBottom)
                            cell.vertical_alignment = 2;
                        const qreal padding[] = {format.leftPadding(), format.topPadding(),
                            format.rightPadding(), format.bottomPadding()};
                        for (std::size_t edge = 0; edge < 4; ++edge)
                            if (qAbs(word_pixels_to_points(padding[edge]) - cell.margins[edge]) > 0.001)
                                cell.margins[edge] = word_pixels_to_points(padding[edge]);
                    }
                }
                else if (const auto block = iterator.currentBlock(); block.isValid())
                {
                    const auto found = blocks.find(block.position());
                    if (found != blocks.end())
                        output.push_back({WordBlock::Kind::Paragraph, found->second});
                }
            }
            return true;
        };
        if (!read(read, editor.rootFrame()->begin(), editor.rootFrame()->end(), current.blocks))
            return false;
        if (seen_tables.size() != original.tables.size())
        {
            error = "原表格被删除，暂未开放结构删除，草稿已保留。";
            return false;
        }

        std::set<std::size_t> visible;
        const auto collect = [&visible](const std::vector<WordBlock>& flow)
        {
            for (const auto& block : flow)
                if (block.kind == WordBlock::Kind::Paragraph)
                    visible.insert(block.index);
        };
        collect(original.blocks);
        for (const auto& table : original.tables)
            for (const auto& cell : table.cells)
                collect(cell.blocks);
        std::vector<std::size_t> hidden;
        for (std::size_t index = 0; index < original.paragraphs.size(); ++index)
            if (!visible.count(index))
                hidden.push_back(index);
        std::vector<WordParagraph> paragraphs;
        std::vector<std::size_t> remap(current.paragraphs.size());
        std::size_t next_hidden = 0;
        for (std::size_t index = 0; index < current.paragraphs.size(); ++index)
        {
            const auto& value = current.paragraphs[index];
            const auto id = value.source_id ? value.source_id : value.origin_id;
            while (
                id && next_hidden < hidden.size() && original.paragraphs[hidden[next_hidden]].source_id < id)
                paragraphs.push_back(original.paragraphs[hidden[next_hidden++]]);
            remap[index] = paragraphs.size();
            paragraphs.push_back(std::move(current.paragraphs[index]));
        }
        while (next_hidden < hidden.size())
            paragraphs.push_back(original.paragraphs[hidden[next_hidden++]]);
        current.paragraphs = std::move(paragraphs);
        const auto reindex = [&remap](std::vector<WordBlock>& flow)
        {
            for (auto& block : flow)
                if (block.kind == WordBlock::Kind::Paragraph)
                    block.index = remap[block.index];
        };
        reindex(current.blocks);
        for (auto& table : current.tables)
            for (auto& cell : table.cells)
                reindex(cell.blocks);
        std::size_t begin = 0;
        for (std::size_t section = 0; section < current.sections.size(); ++section)
        {
            std::size_t end = current.paragraphs.size();
            if (section + 1 < current.sections.size())
            {
                const auto& old = original.sections[section];
                const auto boundary = old.first_paragraph + old.paragraph_count;
                const auto found = std::find_if(current.paragraphs.begin(), current.paragraphs.end(),
                    [boundary](const auto& paragraph)
                {
                    return paragraph.source_id == boundary;
                });
                if (found == current.paragraphs.end())
                {
                    error = "操作合并了分节边界，暂不能安全写回。";
                    return false;
                }
                end = static_cast<std::size_t>(found - current.paragraphs.begin()) + 1;
            }
            if (end < begin)
            {
                error = "分节顺序发生变化，原布局已保留。";
                return false;
            }
            current.sections[section].first_paragraph = begin;
            current.sections[section].paragraph_count = end - begin;
            begin = end;
        }
        return true;
    }
}
