#include "word_document.hpp"
#include "word_cell_borders.hpp"
#include "word_character.hpp"
#include "word_lists.hpp"
#include "word_paragraph_format.hpp"
#include "word_table_gaps.hpp"
#include "word_tabs.hpp"

#include "word_annotations.hpp"
#include "word_distribution.hpp"
#include "word_document_structure.hpp"
#include "word_editor_document.hpp"
#include "word_format_properties.hpp"
#include "word_units.hpp"
#include <QJsonValue>

#include <QAbstractTextDocumentLayout>
#include <QCollator>
#include <QColor>
#include <QFontDatabase>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace
{
    constexpr int border_color_property = mirrorfly::word_paragraph_border_property;
    constexpr int border_bottom_property = QTextFormat::UserProperty + 32;
    constexpr int character_border_property = mirrorfly::word_character_border_property;
    constexpr int outline_property = QTextFormat::UserProperty + 36;
    constexpr int source_paragraph_property = mirrorfly::word_source_paragraph_property;
    constexpr int source_run_property = mirrorfly::word_source_run_property;
    constexpr int source_image_property = mirrorfly::word_source_image_property;
    using mirrorfly::word_character_format;
    using mirrorfly::word_pixels_to_points;
    using mirrorfly::word_points_to_pixels;
    using mirrorfly::WordEditorDocument;

    void retain_color_spelling(std::string& value, const std::string& original)
    {
        if (QString::fromStdString(value).compare(QString::fromStdString(original), Qt::CaseInsensitive) == 0)
            value = original;
    }

    void insert_runs(QTextCursor& cursor, const mirrorfly::WordParagraph& paragraph)
    {
        if (!paragraph.runs.empty())
            cursor.mergeBlockCharFormat(word_character_format(paragraph.runs.front()));
        for (const auto& run : paragraph.runs)
        {
            if (run.image_id)
            {
                const auto* source = dynamic_cast<const WordEditorDocument*>(cursor.document());
                if (source)
                {
                    const auto image = std::find_if(source->source.images.begin(),
                        source->source.images.end(), [&run](const auto& item)
                    {
                        return item.id == run.image_id;
                    });
                    if (image != source->source.images.end())
                    {
                        QTextImageFormat format;
                        format.setName(QStringLiteral("mirrorfly-word-image:/%1").arg(run.image_id));
                        format.setWidth(image->width * 4 / 3);
                        format.setHeight(image->height * 4 / 3);
                        format.setProperty(
                            source_run_property, QVariant::fromValue<qulonglong>(run.source_id));
                        format.setProperty(
                            source_image_property, QVariant::fromValue<qulonglong>(run.image_id));
                        cursor.insertImage(format);
                    }
                }
                continue;
            }
            QString text = QString::fromStdString(run.text);
            text.replace('\n', QChar::LineSeparator);
            text.replace('\r', QChar::LineSeparator);
            cursor.insertText(text, word_character_format(run));
        }
    }

}

namespace mirrorfly
{
    bool word_image_refresh_in_progress(const QTextDocument& document)
    {
        const auto* editor = dynamic_cast<const WordEditorDocument*>(&document);
        return editor && editor->refreshingImages();
    }

    bool take_word_image_structure_change(QTextDocument& document)
    {
        auto* editor = dynamic_cast<WordEditorDocument*>(&document);
        return editor && editor->takeImageStructureChange();
    }

    std::unique_ptr<QTextDocument> create_word_document(const WordDocument& source, qreal text_width,
        bool asynchronous_images, const QTextOption& text_option,
        const std::function<void(std::size_t, std::size_t)>& progress)
    {
        auto document = std::make_unique<WordEditorDocument>(source, asynchronous_images);
        document->setUndoRedoEnabled(false);
        document->setLayoutEnabled(false);
        auto option = text_option;
        option.setTabStopDistance(word_points_to_pixels(source.default_tab_stop));
        document->setDefaultTextOption(option);
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPointSizeF(12);
        document->setDefaultFont(font);
        document->setDocumentMargin(0);
        if (std::isfinite(text_width) && text_width > 0)
            document->setTextWidth(text_width);
        auto flow = source.blocks;
        std::size_t completed = 0;
        if (progress)
            progress(0, source.paragraphs.size());
        if (flow.empty())
        {
            for (std::size_t index = 0; index < source.paragraphs.size(); ++index)
                flow.push_back({WordBlock::Kind::Paragraph, index});
        }
        const auto insert = [&](const auto& self, QTextCursor cursor,
                                const std::vector<WordBlock>& blocks) -> void
        {
            bool first = true;
            WordListBuilder list_builder;
            for (const auto& item : blocks)
            {
                if (item.kind == WordBlock::Kind::Table)
                {
                    const auto& table = source.tables.at(item.index);
                    if (table.rows == 0 || table.column_widths.empty())
                        continue;
                    QTextTableFormat format;
                    format.setProperty(
                        word_source_table_property, QVariant::fromValue<qulonglong>(table.source_id));
                    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
                    format.setBorderCollapse(true);
                    format.setProperty(word_table_border_layout_property, table.border_layout_supported);
                    format.setBorder(table.border_width * 4 / 3);
                    format.setBorderBrush(QColor(QString::fromStdString(table.border_color)));
                    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                    format.setCellSpacing(0);
                    format.setCellPadding(3.5 * 4 / 3);
                    format.setHeaderRowCount(static_cast<int>(table.header_rows));
                    QList<QTextLength> widths;
                    const double total =
                        std::accumulate(table.column_widths.begin(), table.column_widths.end(), 0.0);
                    for (const auto width : table.column_widths)
                        widths.push_back(QTextLength(QTextLength::PercentageLength, width * 100 / total));
                    format.setColumnWidthConstraints(widths);
                    auto* target = cursor.insertTable(
                        static_cast<int>(table.rows), static_cast<int>(table.column_widths.size()), format);
                    for (const auto& cell : table.cells)
                    {
                        if (cell.row_span > 1 || cell.column_span > 1)
                            target->mergeCells(static_cast<int>(cell.row), static_cast<int>(cell.column),
                                static_cast<int>(cell.row_span), static_cast<int>(cell.column_span));
                        auto target_cell =
                            target->cellAt(static_cast<int>(cell.row), static_cast<int>(cell.column));
                        auto cell_format = target_cell.format().toTableCellFormat();
                        cell_format.setBorder(format.border());
                        cell_format.setBorderBrush(format.borderBrush());
                        cell_format.setBorderStyle(format.borderStyle());
                        store_word_cell_borders(cell_format, cell);
                        auto vertical = QTextCharFormat::AlignTop;
                        if (cell.vertical_alignment == 1)
                            vertical = QTextCharFormat::AlignMiddle;
                        else if (cell.vertical_alignment == 2)
                            vertical = QTextCharFormat::AlignBottom;
                        cell_format.setVerticalAlignment(vertical);
                        if (!cell.background.empty())
                            cell_format.setBackground(QColor(QString::fromStdString(cell.background)));
                        cell_format.setLeftPadding(cell.margins[0] * 4 / 3);
                        cell_format.setTopPadding(cell.margins[1] * 4 / 3);
                        cell_format.setRightPadding(cell.margins[2] * 4 / 3);
                        cell_format.setBottomPadding(cell.margins[3] * 4 / 3);
                        self(self, target_cell.firstCursorPosition(), cell.blocks);
                        // Block character formats share the empty cell marker; restore cell decoration last.
                        target_cell.setFormat(cell_format);
                    }
                    mark_word_table_gaps(*target, table);
                    refresh_word_table_borders(*target);
                    cursor = target->lastCursorPosition();
                    cursor.movePosition(QTextCursor::NextBlock);
                    first = true;
                    continue;
                }
                const auto& paragraph = source.paragraphs.at(item.index);
                const auto block = word_paragraph_format(paragraph);
                if (!first)
                    cursor.insertBlock(block);
                else
                    cursor.setBlockFormat(block);
                first = false;
                insert_runs(cursor, paragraph);
                ++completed;
                if (progress && (completed % 64 == 0 || completed == source.paragraphs.size()))
                    progress(completed, source.paragraphs.size());
                list_builder.append(cursor, paragraph);
            }
        };
        QTextCursor transaction(document.get());
        transaction.beginEditBlock();
        insert(insert, transaction, flow);
        transaction.endEditBlock();

        document->watchImages();
        document->setLayoutEnabled(true);
        update_word_annotation_layout(*document);
        install_word_distribution(*document);
        document->setUndoRedoEnabled(true);
        document->clearUndoRedoStacks();
        document->setModified(false);
        return document;
    }

    bool prepare_word_document_images(
        QTextDocument& document, const std::function<void(std::size_t, std::size_t)>& progress)
    {
        auto* resources = document.findChild<WordImageResources*>();
        return resources && resources->prepare(progress);
    }

    void clear_word_package_identity(QTextDocument& document)
    {
        QTextCursor transaction(&document);
        transaction.beginEditBlock();
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            QTextCursor cursor(block);
            auto block_format = block.blockFormat();
            block_format.clearProperty(source_paragraph_property);
            cursor.setBlockFormat(block_format);
            QTextCharFormat identity;
            identity.setProperty(source_run_property, QVariant::fromValue<qulonglong>(0));
            identity.setProperty(source_image_property, QVariant::fromValue<qulonglong>(0));
            cursor.mergeBlockCharFormat(identity);
            cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(identity);
        }
        const auto frames = [&](const auto& self, QTextFrame* frame) -> void
        {
            for (auto* child : frame->childFrames())
            {
                auto format = child->frameFormat();
                format.clearProperty(word_source_table_property);
                child->setFrameFormat(format);
                self(self, child);
            }
        };
        frames(frames, document.rootFrame());
        transaction.endEditBlock();
    }

    WordResult extract_word_document(const QTextDocument& document, const QTextDocument* source)
    {
        WordResult result;
        result.document.paragraphs.clear();
        const auto* imported = dynamic_cast<const WordEditorDocument*>(source ? source : &document);
        const bool preserved = imported && imported->source.source_package;
        if (preserved)
        {
            result.document = imported->source;
            result.document.paragraphs.clear();
        }
        if (!document.rootFrame()->childFrames().isEmpty() && (!preserved || imported->source.tables.empty()))
        {
            result.error = "当前 DOCX 编辑区只保存正文段落，请撤销表格或图片粘贴后再保存。";
            return result;
        }

        result.document.default_tab_stop =
            word_pixels_to_points(document.defaultTextOption().tabStopDistance());
        std::set<std::uint64_t> seen_paragraphs;
        std::map<int, std::size_t> block_indices;
        std::map<std::uint64_t, const WordRun*> source_runs, source_images;
        std::set<int> auxiliary_blocks;
        if (preserved)
        {
            for (const auto& paragraph : imported->source.paragraphs)
                for (const auto& run : paragraph.runs)
                    if (run.image_id)
                        source_images[run.image_id] = &run;
                    else
                        source_runs[run.source_id] = &run;
            const auto collect = [&](const auto& self, QTextFrame* frame) -> void
            {
                for (auto* child : frame->childFrames())
                {
                    if (qobject_cast<QTextTable*>(child))
                    {
                        auxiliary_blocks.insert(child->firstPosition() - 1);
                        auxiliary_blocks.insert(child->lastPosition() + 1);
                    }
                    self(self, child);
                }
            };
            collect(collect, document.rootFrame());
        }
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            WordParagraph paragraph;
            const auto format = block.blockFormat();
            paragraph.source_id = format.property(source_paragraph_property).toULongLong();
            if (preserved && word_table_gap(QTextCursor(block)))
            {
                const auto cell = QTextCursor(block).currentTable()->cellAt(block.position());
                if (paragraph.source_id || !block.text().isEmpty() ||
                    cell.firstCursorPosition().position() != cell.lastCursorPosition().position())
                {
                    result.error = "此处是原表格的布局空缺，不是可编辑单元格。";
                    return result;
                }
                continue;
            }
            if (preserved && paragraph.source_id == 0 && block.text().isEmpty() &&
                auxiliary_blocks.count(block.position()))
                continue;
            if (preserved && paragraph.source_id > imported->source.paragraphs.size())
            {
                result.error = "段落引用了未知来源，草稿已保留。";
                return result;
            }
            if (preserved && paragraph.source_id)
            {
                paragraph = imported->source.paragraphs[paragraph.source_id - 1];
                paragraph.runs.clear();
                if (!seen_paragraphs.insert(paragraph.source_id).second)
                {
                    const auto capability =
                        word_paragraph_capabilities(imported->source, paragraph.source_id - 1);
                    if (!capability.split)
                    {
                        result.error = capability.reason;
                        return result;
                    }
                    paragraph.origin_id = paragraph.source_id;
                    paragraph.source_id = 0;
                }
            }
            extract_word_paragraph_format(format, paragraph);
            extract_word_list(block, paragraph);
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
            {
                const auto fragment = iterator.fragment();
                if (!fragment.isValid())
                {
                    continue;
                }
                const auto style = fragment.charFormat();
                if (style.isImageFormat() && preserved && style.property(source_image_property).toULongLong())
                {
                    const auto image_id = style.property(source_image_property).toULongLong();
                    const auto found = source_images.find(image_id);
                    if (found == source_images.end())
                    {
                        result.error = "图片的原包定位已变化，草稿已保留。";
                        return result;
                    }
                    paragraph.runs.push_back(*found->second);
                    continue;
                }
                if (style.isImageFormat() || style.isAnchor())
                {
                    result.error = "当前只保存正文及基础样式，请撤销图片或链接粘贴。";
                    return result;
                }

                WordRun run;
                run.source_id = style.property(source_run_property).toULongLong();
                auto text = fragment.text();
                text.replace(QChar::LineSeparator, '\n');
                if (!text.isValidUtf16() || text.contains(QChar::ObjectReplacementCharacter))
                {
                    result.error = "正文包含无效字符或不支持的对象，请撤销最近的输入。";
                    return result;
                }

                run.text = text.toUtf8().toStdString();
                const auto families = style.fontFamilies().toStringList();
                if (!families.isEmpty())
                {
                    run.font = families.front().toStdString();
                    run.east_asia_font = families.back().toStdString();
                }

                run.size =
                    style.fontPointSize() > 0 ? style.fontPointSize() : document.defaultFont().pointSizeF();
                run.bold = style.fontWeight() >= QFont::Bold;
                run.italic = style.fontItalic();
                run.double_underline = style.property(word_double_underline_property).toBool();
                run.double_strike = style.property(word_double_strike_property).toBool();
                run.underline = style.fontUnderline() || run.double_underline;
                run.strike = style.fontStrikeOut() || run.double_strike;
                run.outline = style.property(outline_property).toBool();
                run.border_color = style.property(character_border_property).toString().toStdString();
                run.character_spacing = style.fontLetterSpacingType() == QFont::AbsoluteSpacing
                    ? style.fontLetterSpacing() * 3 / 4
                    : 0;
                run.script = style.verticalAlignment() == QTextCharFormat::AlignSuperScript ? 1
                    : style.verticalAlignment() == QTextCharFormat::AlignSubScript          ? -1
                                                                                            : 0;
                run.color = word_character_color(style).toStdString();
                if (style.background().style() != Qt::NoBrush)
                    run.background = style.background().color().name().toStdString();
                if (preserved)
                {
                    const auto found = source_runs.find(run.source_id);
                    if (found != source_runs.end())
                    {
                        retain_color_spelling(run.color, found->second->color);
                        retain_color_spelling(run.background, found->second->background);
                    }
                }
                append_word_ruby_runs(paragraph, run, style);
            }

            if (preserved && paragraph.source_id > 0 &&
                paragraph.source_id <= imported->source.paragraphs.size())
            {
                // Zero-width field markers and references have no QTextFragment, but remain in the package.
                const auto& original = imported->source.paragraphs[paragraph.source_id - 1].runs;
                retain_color_spelling(
                    paragraph.background, imported->source.paragraphs[paragraph.source_id - 1].background);
                for (const auto& run : original)
                {
                    if (!run.text.empty() || run.image_id)
                        continue;
                    const auto existing = std::find_if(paragraph.runs.begin(), paragraph.runs.end(),
                        [&run](const auto& candidate)
                    {
                        return candidate.source_id == run.source_id && !candidate.image_id;
                    });
                    if (existing != paragraph.runs.end())
                        continue;
                    const auto next = std::find_if(paragraph.runs.begin(), paragraph.runs.end(),
                        [&run](const auto& candidate)
                    {
                        return candidate.source_id >= run.source_id;
                    });
                    paragraph.runs.insert(next, run);
                }
            }
            block_indices[block.position()] = result.document.paragraphs.size();
            result.document.paragraphs.push_back(std::move(paragraph));
        }

        if (preserved)
        {
            if (!extract_word_structure(
                    document, imported->source, result.document, block_indices, result.error))
                return result;
            result.error = validate_word_image_placement(imported->source, result.document);
            if (!result.error.empty())
                return result;
            std::set<std::uint64_t> retained;
            for (const auto& paragraph : result.document.paragraphs)
                if (paragraph.source_id)
                    retained.insert(paragraph.source_id);
            for (std::size_t index = 0; index < imported->source.paragraphs.size(); ++index)
                if (!retained.count(imported->source.paragraphs[index].source_id))
                {
                    const auto capability = word_paragraph_capabilities(imported->source, index);
                    if (!capability.remove)
                    {
                        result.error = capability.reason;
                        return result;
                    }
                }
        }
        result.error = validate_word(result.document);
        result.success = result.error.empty();
        return result;
    }

    QVariantMap inspect_word_document(const QTextDocument& document, int position)
    {
        QTextCursor cursor(const_cast<QTextDocument*>(&document));
        cursor.setPosition(std::clamp(position, 0, document.characterCount() - 1));
        const auto format = cursor.charFormat();
        const auto families = format.fontFamilies().toStringList();
        double character_spacing = 0;
        if (format.fontLetterSpacingType() == QFont::AbsoluteSpacing)
            character_spacing = format.fontLetterSpacing() * 3 / 4;
        const auto block = cursor.blockFormat();
        const auto tabs = inspect_word_tabs(block);
        const auto paragraph_text = cursor.block().text();
        const bool indented_tabs =
            block.textIndent() != 0 && (!tabs.isEmpty() || paragraph_text.contains(QChar::Tabulation));
        const bool aligned_soft_break = paragraph_text.contains(QChar::LineSeparator) &&
            paragraph_text.contains(QChar::Tabulation) &&
            std::any_of(tabs.begin(), tabs.end(), [](const QVariant& tab)
        {
            const auto alignment = tab.toMap().value("alignment").toString();
            return alignment == "center" || alignment == "right";
        });
        const bool tab_layout_supported = !indented_tabs && !aligned_soft_break;
        QString tab_layout_reason;
        if (indented_tabs)
            tab_layout_reason =
                QStringLiteral("首行或悬挂缩进与制表位组合的预览及PDF排版可能偏移；DOCX保留原位置。");
        if (aligned_soft_break)
            tab_layout_reason +=
                QStringLiteral("段内软换行与居中或右对齐制表位组合的预览及PDF排版可能偏移；DOCX保留原位置。");
        const auto* table = cursor.currentTable();
        const auto cell = table ? table->cellAt(cursor) : QTextTableCell{};
        const auto cell_format = cell.isValid() ? cell.format().toTableCellFormat() : QTextTableCellFormat{};
        int cell_alignment = 0;
        if (cell_format.verticalAlignment() == QTextCharFormat::AlignMiddle)
            cell_alignment = 1;
        else if (cell_format.verticalAlignment() == QTextCharFormat::AlignBottom)
            cell_alignment = 2;
        int spacing_rule = 0;
        if (block.lineHeightType() == QTextBlockFormat::FixedHeight)
            spacing_rule = 1;
        else if (block.lineHeightType() == QTextBlockFormat::MinimumHeight)
            spacing_rule = 2;
        const bool gap = word_table_gap(cursor);
        const auto underline_style = format.property(word_double_underline_property).toBool()
            ? "double"
            : (format.fontUnderline() ? "single" : "none");
        const auto strike_style = format.property(word_double_strike_property).toBool()
            ? "double"
            : (format.fontStrikeOut() ? "single" : "none");
        return {{"inTable", cell.isValid()}, {"tableGap", gap},
            {"cellBorders", inspect_word_cell_borders(cell)}, {"cellRow", cell.isValid() ? cell.row() : -1},
            {"cellColumn", cell.isValid() ? cell.column() : -1},
            {"cellRowSpan", cell.isValid() ? cell.rowSpan() : 0},
            {"cellColumnSpan", cell.isValid() ? cell.columnSpan() : 0},
            {"readOnlyReason",
                gap ? QStringLiteral("此处是原表格的布局空缺，不是可编辑单元格。") : QString{}},
            {"tableBorderColor", table ? table->format().borderBrush().color().name() : QString{}},
            {"tableBorderWidth", table ? word_pixels_to_points(table->format().border()) : 0},
            {"cellFill",
                cell_format.background().style() == Qt::NoBrush ? QString{}
                                                                : cell_format.background().color().name()},
            {"cellAlign", cell_alignment}, {"cellLeft", word_pixels_to_points(cell_format.leftPadding())},
            {"cellTop", word_pixels_to_points(cell_format.topPadding())},
            {"cellRight", word_pixels_to_points(cell_format.rightPadding())},
            {"cellBottom", word_pixels_to_points(cell_format.bottomPadding())},
            {"family", families.isEmpty() ? document.defaultFont().family() : families.front()},
            {"font", QVariant::fromValue(format.font())}, {"emptyParagraph", cursor.block().text().isEmpty()},
            {"size", format.fontPointSize() > 0 ? format.fontPointSize() : 12},
            {"bold", format.fontWeight() >= QFont::Bold}, {"italic", format.fontItalic()},
            {"underline", format.fontUnderline() || format.property(word_double_underline_property).toBool()},
            {"underlineStyle", underline_style}, {"strikeStyle", strike_style},
            {"heading", cursor.blockFormat().headingLevel()},
            {"strike", format.fontStrikeOut() || format.property(word_double_strike_property).toBool()},
            {"outline", format.property(outline_property).toBool()},
            {"characterBorder", format.property(character_border_property)},
            {"characterSpacing", character_spacing},
            {"rtl", cursor.blockFormat().layoutDirection() == Qt::RightToLeft},
            {"ruby", format.property(word_ruby_property)},
            {"script",
                format.verticalAlignment() == QTextCharFormat::AlignSuperScript     ? 1
                    : format.verticalAlignment() == QTextCharFormat::AlignSubScript ? -1
                                                                                    : 0},
            {"color", word_character_color(format)},
            {"highlight",
                format.background().style() == Qt::NoBrush ? QString{} : format.background().color().name()},
            {"paragraphFill",
                cursor.blockFormat().background().style() == Qt::NoBrush
                    ? QString{}
                    : cursor.blockFormat().background().color().name()},
            {"paragraphBorder", cursor.blockFormat().property(border_color_property)},
            {"align",
                cursor.blockFormat().property(word_distributed_property).toBool()
                    ? 4
                    : word_alignment_index(cursor.blockFormat().alignment())},
            {"spacing", spacing_rule ? 1.0 : block.lineHeight() / 100}, {"lineSpacingRule", spacing_rule},
            {"lineSpacingPoints", spacing_rule ? word_pixels_to_points(block.lineHeight()) : 0.0},
            {"list", static_cast<int>(word_list_kind(cursor.block().textList()))},
            {"listLevel", cursor.block().textList() ? cursor.block().textList()->format().indent() - 1 : 0},
            {"listStyle", inspect_word_list(cursor.block())}, {"tabStops", tabs},
            {"tabLayoutSupported", tab_layout_supported}, {"tabLayoutReason", tab_layout_reason},
            {"defaultTabStop", word_pixels_to_points(document.defaultTextOption().tabStopDistance())},
            {"leftIndent", word_pixels_to_points(block.leftMargin())},
            {"rightIndent", word_pixels_to_points(block.rightMargin())},
            {"firstLineIndent", word_pixels_to_points(block.textIndent())},
            {"spaceBefore", word_pixels_to_points(block.topMargin())},
            {"spaceAfter", word_pixels_to_points(block.bottomMargin())}};
    }

    bool format_word_document(
        QTextDocument& document, int start, int end, const QString& action, const QVariant& value)
    {
        if (start < 0 || end < start || end >= document.characterCount())
        {
            return false;
        }
        QTextCursor position(&document);
        position.setPosition(start);
        if (start == end && word_table_gap(position))
            return false;
        if (QStringList{"bold", "italic", "underline", "strike", "outline", "rtl"}.contains(action) &&
            value.metaType().id() != QMetaType::Bool)
            return false;
        if (QStringList{"heading", "align", "list", "listLevel", "script", "cellAlign"}.contains(action))
        {
            const auto number = QJsonValue::fromVariant(value);
            if (!number.isDouble() || !std::isfinite(number.toDouble()) ||
                number.toDouble() < -2147483648.0 || number.toDouble() > 2147483647.0 ||
                std::floor(number.toDouble()) != number.toDouble())
                return false;
        }
        if (action == "ruby" || action == "rubyAuto" || action == "rubyClear")
            return format_word_ruby(
                document, start, end, action == "ruby" ? value.toString() : QString{}, action == "rubyAuto");
        if (action == "outline" || action == "color")
            return word_format_effect(document, start, end, action, value);
        if (action == "sort")
            return sort_word_paragraphs(document, start, end, value.toString());
        QTextCursor cursor(&document);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        if (action.startsWith("cell"))
        {
            auto* table = cursor.currentTable();
            auto cell = table ? table->cellAt(start) : QTextTableCell{};
            if (!cell.isValid() || end > cell.lastCursorPosition().position())
                return false;
            if (action == "cellBorder")
                return format_word_cell_border(*table, cell, value);
            auto format = cell.format().toTableCellFormat();
            if (action == "cellFill")
            {
                const auto text = value.toString();
                const QColor color(text);
                if (!text.isEmpty() && (text.size() != 7 || !text.startsWith('#') || !color.isValid()))
                    return false;
                format.setBackground(text.isEmpty() ? QBrush(Qt::NoBrush) : QBrush(color));
            }
            else if (action == "cellAlign")
            {
                bool valid = false;
                const auto align = value.toInt(&valid);
                if (!valid || align < 0 || align > 2)
                    return false;
                auto vertical = QTextCharFormat::AlignTop;
                if (align == 1)
                    vertical = QTextCharFormat::AlignMiddle;
                else if (align == 2)
                    vertical = QTextCharFormat::AlignBottom;
                format.setVerticalAlignment(vertical);
            }
            else
            {
                bool valid = false;
                const auto points = value.toDouble(&valid);
                if (!valid || !std::isfinite(points) || points < 0 || points > 144)
                    return false;
                const auto pixels = word_points_to_pixels(points);
                if (action == "cellLeft")
                    format.setLeftPadding(pixels);
                else if (action == "cellTop")
                    format.setTopPadding(pixels);
                else if (action == "cellRight")
                    format.setRightPadding(pixels);
                else if (action == "cellBottom")
                    format.setBottomPadding(pixels);
                else
                    return false;
            }
            cell.setFormat(format);
            return true;
        }
        QTextCharFormat character;
        QTextBlockFormat block;
        bool paragraph = false;
        if (action == "tabStops")
            return format_word_tabs(document, cursor, value);
        if (action == "listMarker" || action == "listStart")
            return format_word_list_variant(document, cursor, action, value);
        if (action == "list" || action == "listLevel")
        {
            if (!cursor.hasSelection())
            {
                cursor.select(QTextCursor::BlockUnderCursor);
            }
            const auto* current = cursor.block().textList();
            const int kind = action == "list" ? value.toInt() : static_cast<int>(word_list_kind(current));
            const int level =
                action == "listLevel" ? value.toInt() : (current ? current->format().indent() - 1 : 0);
            return format_word_list(document, cursor, kind, level);
        }
        if (action == "font")
        {
            const auto family = value.toString();
            if (!QFontDatabase::families().contains(family))
            {
                return false;
            }
            character.setFontFamilies({family});
        }

        else if (action == "size")
        {
            bool valid = false;
            const auto size = value.toDouble(&valid);
            if (!valid || !std::isfinite(size) || size < 6 || size > 96)
            {
                return false;
            }
            character.setFontPointSize(size);
        }

        else if (action == "bold")
        {
            character.setFontWeight(value.toBool() ? QFont::Bold : QFont::Normal);
        }
        else if (action == "italic")
        {
            character.setFontItalic(value.toBool());
        }
        else if (action == "underline")
        {
            character.setFontUnderline(value.toBool());
            character.setProperty(word_double_underline_property, false);
        }
        else if (action == "strike")
        {
            character.setFontStrikeOut(value.toBool());
            character.setProperty(word_double_strike_property, false);
        }
        else if (action == "underlineStyle" || action == "strikeStyle")
        {
            if (value.metaType().id() != QMetaType::QString ||
                !QStringList{"none", "single", "double"}.contains(value.toString()))
                return false;
            const bool underline = action == "underlineStyle";
            if (underline)
                character.setFontUnderline(value.toString() == "single");
            else
                character.setFontStrikeOut(value.toString() == "single");
            character.setProperty(underline ? word_double_underline_property : word_double_strike_property,
                value.toString() == "double");
        }
        else if (action == "script")
        {
            bool valid = false;
            const auto script = value.toInt(&valid);
            if (!valid || script < -1 || script > 1)
                return false;
            character.setVerticalAlignment(word_script_alignment(script));
        }
        else if (action == "characterSpacing")
        {
            bool valid = false;
            const auto spacing = value.toDouble(&valid);
            if (!valid || !std::isfinite(spacing) || spacing < -3 || spacing > 20)
                return false;
            character.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            character.setFontLetterSpacing(spacing * 4 / 3);
        }
        else if (action == "rtl")
        {
            block.setLayoutDirection(value.toBool() ? Qt::RightToLeft : Qt::LeftToRight);
            paragraph = true;
        }
        else if (action == "color" || action == "highlight" || action == "characterBorder" ||
            action == "paragraphFill" || action == "paragraphBorder" || action == "paragraphBottomBorder")
        {
            const auto text = value.toString();
            const QColor color(text);
            if (!text.isEmpty() && (text.size() != 7 || !text.startsWith('#') || !color.isValid()))
                return false;
            const QBrush brush = text.isEmpty() ? QBrush(Qt::NoBrush) : QBrush(color);
            if (action == "color")
                character.setForeground(brush);
            else if (action == "highlight")
                character.setBackground(brush);
            else if (action == "characterBorder")
                character.setProperty(character_border_property, text);
            else if (action == "paragraphFill")
            {
                block.setBackground(brush);
                paragraph = true;
            }
            else
            {
                block.setProperty(border_color_property, text);
                block.setProperty(border_bottom_property, action == "paragraphBottomBorder");
                paragraph = true;
            }
        }
        else if (action == "grow" || action == "shrink")
        {
            const auto size = cursor.charFormat().fontPointSize();
            character.setFontPointSize(
                std::clamp((size > 0 ? size : 12) + (action == "grow" ? 1 : -1), 6.0, 96.0));
        }
        else if (action == "clear")
        {
            if (!cursor.hasSelection())
                cursor.select(QTextCursor::BlockUnderCursor);
            cursor.beginEditBlock();
            cursor.mergeCharFormat(word_style_format({}));
            cursor.mergeBlockCharFormat(word_style_format({}));
            cursor.endEditBlock();
            return true;
        }
        else if (action == "style")
        {
            WordParagraph sample;
            WordRun run;
            const auto style = value.toString();
            if (style == "heading1" || style == "heading2" || style == "heading3")
            {
                sample.heading = style.back().digitValue();
                run.size = sample.heading == 1 ? 22 : sample.heading == 2 ? 18 : 15;
                run.bold = true;
                sample.space_after = 12;
            }
            else if (style == "strong" || style == "points")
                run.bold = true;
            else if (style == "emphasis")
                run.italic = true;
            else if (style == "quote")
            {
                run.italic = true;
                sample.left_indent = 24;
            }
            else if (style == "header" || style == "pageNumber")
            {
                sample.alignment = 1;
                run.size = 9;
            }
            else if (style != "normal" && style != "web" && style != "defaultFont")
                return false;
            sample.runs.push_back(run);
            return paint_word_format(document, start, end, sample);
        }
        else if (action == "heading")
        {
            const int heading = value.toInt();
            if (heading < 0 || heading > 3)
            {
                return false;
            }
            block.setHeadingLevel(heading);
            block.setBottomMargin(word_points_to_pixels(heading ? 12 : 8));
            paragraph = true;
            character.setFontPointSize(heading == 1 ? 22 : (heading == 2 ? 18 : (heading == 3 ? 15 : 12)));
            character.setFontWeight(heading ? QFont::Bold : QFont::Normal);
        }

        else if (action == "align")
        {
            if (value.toInt() < 0 || value.toInt() > 4)
            {
                return false;
            }
            block.setAlignment(word_alignment(value.toInt()));
            block.setProperty(word_distributed_property, value.toInt() == 4);
            paragraph = true;
        }

        else if (action == "spacing")
        {
            if (!std::isfinite(value.toDouble()) || value.toDouble() < 1 || value.toDouble() > 2)
            {
                return false;
            }
            block.setLineHeight(value.toDouble() * 100, QTextBlockFormat::ProportionalHeight);
            paragraph = true;
        }

        else if (action == "lineSpacing")
        {
            const auto settings = value.toMap();
            bool rule_valid = false, amount_valid = false;
            const auto rule_number = settings.value("rule").toDouble(&rule_valid);
            const auto amount = settings.value("value").toDouble(&amount_valid);
            if (settings.size() != 2 || !rule_valid || !amount_valid || !std::isfinite(rule_number) ||
                rule_number < 0 || rule_number > 2 || std::floor(rule_number) != rule_number ||
                !std::isfinite(amount) || amount < 1 ||
                amount > (rule_number ? maximum_word_spacing_points : 2))
                return false;
            const auto rule = static_cast<int>(rule_number);
            block.setLineHeight(rule ? word_points_to_pixels(amount) : amount * 100,
                rule == 1       ? QTextBlockFormat::FixedHeight
                    : rule == 2 ? QTextBlockFormat::MinimumHeight
                                : QTextBlockFormat::ProportionalHeight);
            paragraph = true;
        }

        else if (action == "leftIndent" || action == "rightIndent" || action == "firstLineIndent" ||
            action == "spaceBefore" || action == "spaceAfter")
        {
            bool valid = false;
            const auto amount = value.toDouble(&valid);
            const auto maximum = action == "leftIndent" || action == "rightIndent"
                ? maximum_word_indent_points
                : maximum_word_spacing_points;
            if (!valid || !std::isfinite(amount) || amount < (action == "firstLineIndent" ? -144 : 0) ||
                amount > maximum)
                return false;
            if ((action == "leftIndent" || action == "firstLineIndent") &&
                !word_indent_range_valid(document, cursor, action == "leftIndent", amount))
                return false;
            const auto pixels = word_points_to_pixels(amount);
            if (action == "leftIndent")
                block.setLeftMargin(pixels);
            else if (action == "rightIndent")
                block.setRightMargin(pixels);
            else if (action == "firstLineIndent")
                block.setTextIndent(pixels);
            else if (action == "spaceBefore")
                block.setTopMargin(pixels);
            else
                block.setBottomMargin(pixels);
            paragraph = true;
        }

        else
        {
            return false;
        }
        // Without a selection, the explicit tool formats the current paragraph.
        if (!cursor.hasSelection())
        {
            cursor.select(QTextCursor::BlockUnderCursor);
        }
        if (action == "heading")
        {
            const auto first = document.findBlock(cursor.selectionStart());
            const auto last =
                document.findBlock(std::max(cursor.selectionStart(), cursor.selectionEnd() - 1));
            cursor.setPosition(first.position());
            cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        }

        cursor.beginEditBlock();
        if (paragraph)
        {
            const auto last = std::max(cursor.selectionStart(), cursor.selectionEnd() - 1);
            for (auto item = document.findBlock(cursor.selectionStart());
                item.isValid() && item.position() <= last; item = item.next())
            {
                auto updated = item.blockFormat();
                const auto tabs = word_tabs_from_format(updated);
                updated.merge(block);
                if (action == "leftIndent" || action == "firstLineIndent")
                    apply_word_tabs(updated, tabs);
                QTextCursor(item).setBlockFormat(updated);
            }
        }
        if (!character.isEmpty())
        {
            cursor.mergeCharFormat(character);
            if (cursor.block().text().isEmpty())
            {
                cursor.mergeBlockCharFormat(character);
            }
        }
        update_word_annotation_layout(document);
        cursor.endEditBlock();
        refresh_word_distribution(document, cursor.selectionStart(), cursor.selectionEnd());
        return true;
    }

    bool paint_word_format(QTextDocument& document, int start, int end, const WordParagraph& sample)
    {
        if (start < 0 || end < start || end >= document.characterCount() ||
            !validate_word(WordDocument{{sample}}).empty())
            return false;
        QTextCursor cursor(&document);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        if (!cursor.hasSelection())
            cursor.select(QTextCursor::BlockUnderCursor);
        cursor.beginEditBlock();
        auto block = word_paragraph_format(sample);
        block.clearProperty(source_paragraph_property);
        cursor.mergeBlockFormat(block);
        const auto character = word_style_format(sample.runs.empty() ? WordRun{} : sample.runs.front());
        cursor.mergeCharFormat(character);
        cursor.mergeBlockCharFormat(character);
        format_word_list(document, cursor, static_cast<int>(sample.list), sample.list_level);
        update_word_annotation_layout(document);
        cursor.endEditBlock();
        refresh_word_distribution(document, cursor.selectionStart(), cursor.selectionEnd());
        return true;
    }

    bool insert_word_paragraphs(
        QTextDocument& document, std::size_t paragraph_index, const std::vector<WordParagraph>& paragraphs)
    {
        if (paragraphs.empty() || paragraph_index > static_cast<std::size_t>(document.blockCount()))
        {
            return false;
        }
        const bool replace_empty =
            document.blockCount() == 1 && document.begin().text().isEmpty() && paragraph_index == 0;
        QTextCursor cursor(&document);
        if (replace_empty)
        {
            cursor.setPosition(0);
        }
        else
        {
            const auto previous = document.findBlockByNumber(static_cast<int>(paragraph_index) - 1);
            if (!previous.isValid())
            {
                return false;
            }
            cursor.setPosition(previous.position() + previous.length() - 1);
        }
        cursor.beginEditBlock();
        WordListBuilder list_builder;
        for (std::size_t index = 0; index < paragraphs.size(); ++index)
        {
            const auto& paragraph = paragraphs[index];
            if (index != 0 || !replace_empty)
            {
                cursor.insertBlock(word_paragraph_format(paragraph));
            }
            else
            {
                cursor.setBlockFormat(word_paragraph_format(paragraph));
            }
            insert_runs(cursor, paragraph);
            list_builder.append(cursor, paragraph);
        }
        cursor.endEditBlock();
        return true;
    }

    bool sort_word_paragraphs(QTextDocument& document, int start, int end, const QString& order)
    {
        if (start < 0 || end < start || end >= document.characterCount() ||
            !QStringList{"textAscending", "textDescending", "numberAscending", "numberDescending"}.contains(
                order))
            return false;
        const auto extracted = extract_word_document(document);
        if (!extracted.success)
            return false;
        if (extracted.document.source_package)
            return false;
        const auto first = start == end ? document.begin() : document.findBlock(start);
        const auto last = start == end ? document.lastBlock() : document.findBlock(std::max(start, end - 1));
        struct Item
        {
            WordParagraph paragraph;
            QString text;
            double number = 0;
            int index = 0;
        };
        std::vector<Item> items;
        const bool numeric = order.startsWith("number");
        const bool descending = order.endsWith("Descending");
        for (auto index = first.blockNumber(); index <= last.blockNumber(); ++index)
        {
            Item item;
            item.paragraph = extracted.document.paragraphs.at(index);
            item.index = index;
            for (const auto& run : item.paragraph.runs)
                item.text += QString::fromStdString(run.text);
            if (numeric)
            {
                bool valid = false;
                item.number = item.text.trimmed().toDouble(&valid);
                if (!valid || !std::isfinite(item.number))
                    return false;
            }
            items.push_back(std::move(item));
        }
        QCollator collator(QLocale(QLocale::Chinese, QLocale::China));
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        std::stable_sort(items.begin(), items.end(), [&](const auto& left, const auto& right)
        {
            auto comparison = collator.compare(left.text, right.text);
            if (numeric)
                comparison = (left.number > right.number) - (left.number < right.number);
            return descending ? comparison > 0 : comparison < 0;
        });
        bool changed = false;
        for (std::size_t index = 0; index < items.size(); ++index)
            changed = changed || items[index].index != first.blockNumber() + static_cast<int>(index);
        if (!changed)
            return true;
        QTextCursor cursor(&document);
        cursor.setPosition(first.position());
        cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        WordListBuilder list_builder;
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const auto& paragraph = items[index].paragraph;
            if (index)
                cursor.insertBlock(word_paragraph_format(paragraph), QTextCharFormat{});
            else
                cursor.setBlockFormat(word_paragraph_format(paragraph));
            insert_runs(cursor, paragraph);
            list_builder.append(cursor, paragraph);
        }
        update_word_annotation_layout(document);
        cursor.endEditBlock();
        return true;
    }
}
