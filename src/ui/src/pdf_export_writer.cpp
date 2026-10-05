#include "pdf_export_writer.hpp"
#include "canvas_paint.hpp"
#include "markdown_document.hpp"
#include "presentation_scene.hpp"
#include "spreadsheet_border_renderer.hpp"
#include "spreadsheet_model.hpp"
#include "spreadsheet_text_renderer.hpp"
#include "word_annotations.hpp"
#include "word_distribution.hpp"
#include "word_document.hpp"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    using namespace mirrorfly;

    class BoundedBuffer final : public QBuffer
    {
    public:
        bool failed = false;

    protected:
        qint64 writeData(const char* data, qint64 size) override
        {
            if (size < 0 || pos() > static_cast<qint64>(maximum_pdf_bytes) - size)
            {
                failed = true;
                return -1;
            }
            return QBuffer::writeData(data, size);
        }
    };

    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class ExportPages
    {
    public:
        ExportPages(const PdfExportSource& source, PdfExportProgress& state, const QSizeF& size)
            : writer(&buffer), progress(state)
        {
            require(buffer.open(QIODevice::ReadWrite), "无法创建导出缓冲区。");
            writer.setResolution(72);
            writer.setCreator("Mirrorfly Office");
            writer.setTitle(QString::fromStdString(source.title));
            writer.setPageSize(QPageSize(size, QPageSize::Point, "Mirrorfly", QPageSize::ExactMatch));
            writer.setPageMargins(QMarginsF(), QPageLayout::Point);
            require(painter.begin(&writer), "无法开始 PDF 排版。");
            painter.setRenderHints(
                QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        }

        void page()
        {
            require(!progress.cancelled.load(), "导出已取消。");
            require(count < static_cast<int>(maximum_pdf_pages), "导出超过 250 页，请缩小范围或调整布局。");
            if (count > 0)
                require(writer.newPage(), "PDF 页面写入失败或已达大小限制。");
            ++count;
            progress.completed.store(count);
        }

        PdfBytesResult finish()
        {
            require(!progress.cancelled.load(), "导出已取消。");
            require(painter.end(), "PDF 写入失败。");
            require(!buffer.failed, "导出超过 64 MiB，未生成截断文件。");
            require(buffer.data().size() <= static_cast<qsizetype>(maximum_pdf_bytes), "导出超过 64 MiB。");
            const auto& data = buffer.data();
            PdfBytesResult result;
            result.bytes.assign(data.begin(), data.end());
            return result;
        }

        BoundedBuffer buffer;
        QPdfWriter writer;
        QPainter painter;
        PdfExportProgress& progress;
        int count = 0;
    };

    PdfBytesResult text_export(const PdfExportSource& source, const PdfExportOptions& options,
        const QVariantMap& theme, PdfExportProgress& progress)
    {
        const QSizeF paper = options.landscape ? QSizeF(842, 595) : QSizeF(595, 842);
        ExportPages output(source, progress, paper);
        std::unique_ptr<QTextDocument> document;
        if (const auto* word = std::get_if<WordDocument>(&source.content))
            document = create_word_document(*word);
        else
        {
            const auto& text = std::get<PdfTextSource>(source.content);
            document = create_editor_document();
            QFont font(theme.value("fontFamily").toString());
            font.setPointSizeF(11);
            document->setDefaultFont(font);
            if (text.markdown)
            {
                const auto content = QString::fromStdString(text.text);
                const auto error = markdown_support_error(content);
                require(error.isEmpty(), "此 Markdown 包含暂不支持的排版，请先简化后导出。");
                load_markdown_document(*document, content, theme);
            }
            else
                document->setPlainText(QString::fromStdString(text.text));
        }
        require(document != nullptr, "无法准备文档排版。");
        document->documentLayout()->setPaintDevice(&output.writer);
        document->setDocumentMargin(0);
        if (std::holds_alternative<WordDocument>(source.content))
            update_word_annotation_layout(*document);
        const QSizeF body(paper.width() - 72, paper.height() - 72);
        document->setPageSize(body);
        if (std::holds_alternative<WordDocument>(source.content))
            refresh_word_distribution(*document);
        const int pages = std::max(1, document->pageCount());
        require(pages <= static_cast<int>(maximum_pdf_pages), "文档超过 250 页，请缩小导出内容。");
        progress.total.store(pages);
        for (int index = 0; index < pages; ++index)
        {
            output.page();
            auto& painter = output.painter;
            painter.save();
            painter.translate(36, 36);
            painter.setClipRect(QRectF(QPointF(), body));
            painter.translate(0, -index * body.height());
            document->drawContents(&painter, QRectF(0, index * body.height(), body.width(), body.height()));
            if (std::holds_alternative<WordDocument>(source.content))
                for (const auto& item : word_paragraph_decorations(*document))
                    paint_word_decoration(painter, item.toMap(), 0.75);
            painter.restore();
        }
        return output.finish();
    }

    PdfBytesResult slides_export(const PdfExportSource& source, const PdfExportOptions& options,
        const QVariantMap& theme, PdfExportProgress& progress)
    {
        const auto& scene = std::get<std::shared_ptr<const PresentationScene>>(source.content);
        require(scene && !scene->slides.empty() && scene->width > 0 && scene->height > 0,
            "演示文稿没有可导出的页面。");
        auto document = prepare_presentation(scene);
        ExportPages output(source, progress, QSizeF(scene->width, scene->height));
        progress.total.store(options.scope == "current" ? 1 : static_cast<int>(scene->slides.size()));
        for (std::size_t index = 0; index < scene->slides.size(); ++index)
        {
            if (options.scope == "current" && index != source.current)
                continue;
            if (options.scope != "current" && scene->slides[index].hidden)
                continue;
            output.page();
            paint_presentation_slide(
                output.painter, document, index, theme, QRectF(0, 0, scene->width, scene->height));
        }
        require(output.count > 0, "没有可导出的可见幻灯片。");
        progress.total.store(output.count);
        return output.finish();
    }

    QVariantMap graph_scene(const MindMapDocument& graph)
    {
        QVariantList nodes;
        QVariantList edges;
        for (const auto& node : graph.nodes)
            nodes.push_back(QVariantMap{{"id", QString::fromStdString(node.id)},
                {"text", QString::fromStdString(node.text)}, {"x", node.x}, {"y", node.y},
                {"width", node.width}, {"height", node.height},
                {"border", QString::fromStdString(node.border)}, {"fill", QString::fromStdString(node.fill)},
                {"shape", QString::fromStdString(node.shape)}, {"stroke", node.border_width}});
        for (const auto& edge : graph.edges)
            edges.push_back(QVariantMap{{"id", QString::fromStdString(edge.id)},
                {"from", QString::fromStdString(edge.from)}, {"to", QString::fromStdString(edge.to)},
                {"label", QString::fromStdString(edge.label)}});
        return {{"nodes", nodes}, {"edges", edges}, {"freeLayout", true}};
    }

    PdfBytesResult graph_export(const PdfExportSource& source, const PdfExportOptions& options,
        const QVariantMap& theme, PdfExportProgress& progress)
    {
        auto graph = std::get<MindMapDocument>(source.content);
        if (!graph.free_layout)
            require(validate_mindmap(graph).error == MindMapError::None, "无法转换导图布局。");
        require(!graph.nodes.empty(), "导图没有节点。");
        QRectF bounds;
        for (const auto& node : graph.nodes)
            bounds = bounds.united(QRectF(node.x, node.y, node.width, node.height));
        bounds.adjust(-100, -100, 100, 100);
        const QSizeF paper(1191, 842);
        const QSizeF body(paper.width() - 72, paper.height() - 72);
        const bool tiled = options.layout == "tiles";
        const int columns = tiled ? static_cast<int>(std::ceil(bounds.width() / body.width())) : 1;
        const int rows = tiled ? static_cast<int>(std::ceil(bounds.height() / body.height())) : 1;
        require(
            columns * rows <= static_cast<int>(maximum_pdf_pages), "导图分页超过 250 页，请使用整图缩放。");
        ExportPages output(source, progress, paper);
        progress.total.store(columns * rows);
        const auto scene = graph_scene(graph);
        const double scale =
            tiled ? 1 : std::min(body.width() / bounds.width(), body.height() / bounds.height());
        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                output.page();
                const QRectF viewport(bounds.x() + column * body.width(), bounds.y() + row * body.height(),
                    body.width() / scale, body.height() / scale);
                auto& painter = output.painter;
                painter.save();
                painter.translate(36, 36);
                painter.setClipRect(QRectF(QPointF(), body));
                painter.scale(scale, scale);
                painter.translate(-viewport.x(), -viewport.y());
                paint_mindmap(painter, scene, theme, viewport);
                painter.restore();
            }
        }
        return output.finish();
    }

    SpreadsheetRange used_range(const SpreadsheetDocument& document, std::size_t sheet)
    {
        SpreadsheetRange range;
        bool found = false;
        const auto include = [&](SpreadsheetAddress address)
        {
            if (!found)
                range.first = range.last = address;
            found = true;
            range.first.row = std::min(range.first.row, address.row);
            range.first.column = std::min(range.first.column, address.column);
            range.last.row = std::max(range.last.row, address.row);
            range.last.column = std::max(range.last.column, address.column);
        };
        for (const auto& item : document.sheets[sheet].cells)
            include(item.first);
        const auto edits = document.edits.find(sheet);
        if (edits != document.edits.end())
            for (const auto& item : edits->second)
                include(item.first);
        const auto formats = document.format_edits.find(sheet);
        if (formats != document.format_edits.end())
            for (const auto& item : formats->second)
                include(item.first);
        for (auto merge : spreadsheet_features(document, sheet).merges)
        {
            include(merge.first);
            include(merge.last);
        }
        for (const auto& table : spreadsheet_features(document, sheet).tables)
        {
            include(table.range.first);
            include(table.range.last);
        }
        if (!found)
            range.last = {19, 7};
        return range;
    }

    void sheet_cell(QPainter& painter, const QRectF& rect, SpreadsheetModel& model, int row, int column)
    {
        const auto index = model.index(row, column);
        const auto style = model.data(index, Qt::UserRole + 3).toMap();
        const QColor fill(style.value("fill").toString());
        painter.fillRect(rect, fill.isValid() ? fill : QColor(Qt::white));
        painter.setPen(QPen(QColor("#CBD1D5"), .35));
        painter.drawRect(rect);
        paint_spreadsheet_borders(painter, rect, style, .75);
        QFont font(style.value("font").toString());
        const double size = style.value("size").toDouble();
        font.setPointSizeF(size > 0 ? size : 11);
        font.setBold(style.value("bold").toString() == "1");
        font.setItalic(style.value("italic").toString() == "1");
        font.setUnderline(style.value("underline").toString() == "1");
        font.setStrikeOut(style.value("strike").toString() == "1");
        painter.setFont(font);
        const QColor ink(style.value("text").toString());
        painter.setPen(ink.isValid() ? ink : QColor(Qt::black));
        int alignment = Qt::AlignVCenter | Qt::AlignLeft;
        if (style.value("align").toString() == "center")
            alignment = Qt::AlignVCenter | Qt::AlignHCenter;
        if (style.value("align").toString() == "right")
            alignment = Qt::AlignVCenter | Qt::AlignRight;
        alignment &= ~Qt::AlignVertical_Mask;
        alignment |= style.value("valign") == "top" ? Qt::AlignTop
            : style.value("valign") == "bottom"     ? Qt::AlignBottom
                                                    : Qt::AlignVCenter;
        if (style.value("wrap").toString() == "1")
            alignment |= Qt::TextWordWrap;
        painter.save();
        painter.setClipRect(rect.adjusted(2, 1, -2, -1), Qt::IntersectClip);
        const auto text_rect = rect.adjusted(3 + style.value("indent").toInt() * 7.5, 1, -3, -1);
        if (style.value("align") == "justify" || style.value("align") == "distributed" ||
            style.value("shrinkToFit").toString() == "1" || style.value("textRotation").toInt() != 0)
            paint_spreadsheet_aligned_text(painter, text_rect, model.data(index).toString(), style);
        else
            painter.drawText(text_rect, alignment, model.data(index).toString());
        painter.restore();
    }

    PdfBytesResult sheets_export(
        const PdfExportSource& source, const PdfExportOptions& options, PdfExportProgress& progress)
    {
        const auto& document = std::get<SpreadsheetDocument>(source.content);
        require(!document.sheets.empty(), "工作簿没有工作表。");
        const auto calculation =
            std::make_shared<const SpreadsheetCalculation>(spreadsheet_calculate_all(document));
        require(calculation->complete, "公式计算超过整表工作量限制，请简化公式后再导出。");
        const QSizeF paper = options.landscape ? QSizeF(842, 595) : QSizeF(595, 842);
        const QSizeF body(paper.width() - 64, paper.height() - 100);
        ExportPages output(source, progress, paper);
        std::uint64_t cell_count = 0;
        for (std::size_t sheet = 0; sheet < document.sheets.size(); ++sheet)
        {
            if (options.scope == "all" && document.sheets[sheet].hidden)
                continue;
            if (options.scope != "all" && sheet != source.current)
                continue;
            const auto range = options.scope == "selection" ? source.selection : used_range(document, sheet);
            require(range.first.row <= range.last.row && range.first.column <= range.last.column,
                "导出选区无效。");
            const auto rows = range.last.row - range.first.row + 1;
            const auto columns = range.last.column - range.first.column + 1;
            cell_count += static_cast<std::uint64_t>(rows) * columns;
            require(cell_count <= 100000, "导出矩形超过 10 万格，请选择较小区域（稀疏表也按矩形计数）。");
            const auto& features = spreadsheet_features(document, sheet);
            for (auto m : features.merges)
            {
                const bool intersects = m.first.row <= range.last.row && range.first.row <= m.last.row &&
                    m.first.column <= range.last.column && range.first.column <= m.last.column;
                require(!intersects ||
                        (m.first.row >= range.first.row && m.last.row <= range.last.row &&
                            m.first.column >= range.first.column && m.last.column <= range.last.column),
                    "选区不能截断合并区域，请扩大导出范围。");
            }
            SpreadsheetModel model;
            model.setDocument(&document, sheet, calculation);
            const auto height_at = [&](unsigned row)
            {
                return model.rowVisible(row)
                    ? std::clamp(spreadsheet_dimension(document, sheet, false, row), 12.0, 409.0)
                    : 0.0;
            };
            std::vector<double> widths;
            double total_width = 0;
            for (auto column = range.first.column; column <= range.last.column; ++column)
            {
                const double width = features.hidden_columns.count(column)
                    ? 0
                    : std::clamp(spreadsheet_dimension(document, sheet, true, column) * 7 + 5, 20.0, 500.0);
                widths.push_back(width);
                total_width += width;
            }
            require(total_width > 0, "选区所有列均被隐藏。");
            const double scale = std::min(1.0, body.width() / total_width);
            require(scale >= .15, "列数过多，缩放后难以阅读，请选择较小区域。");
            auto row = range.first.row;
            while (row <= range.last.row)
            {
                output.page();
                auto& painter = output.painter;
                painter.save();
                QFont heading;
                heading.setPointSize(10);
                painter.setFont(heading);
                painter.setPen(Qt::black);
                painter.drawText(QRectF(32, 20, body.width(), 24), Qt::AlignLeft,
                    QString::fromStdString(document.sheets[sheet].name));
                painter.translate(32, 50);
                painter.scale(scale, scale);
                double y = 0;
                while (row <= range.last.row)
                {
                    auto group_end = row;
                    bool expanded = true;
                    while (expanded)
                    {
                        expanded = false;
                        for (auto m : features.merges)
                            if (m.first.row >= row && m.first.row <= group_end && m.last.row > group_end &&
                                m.first.column >= range.first.column && m.last.column <= range.last.column)
                            {
                                group_end = m.last.row;
                                expanded = true;
                            }
                    }
                    double group_height = 0;
                    for (auto i = row; i <= group_end; ++i)
                        group_height += height_at(i);
                    require(
                        group_height * scale <= body.height(), "合并区域高度超过一页，请缩小行高后导出。");
                    if (y > 0 && (y + group_height) * scale > body.height())
                        break;
                    while (row <= group_end)
                    {
                        const double height = height_at(row);
                        double x = 0;
                        for (auto column = range.first.column; column <= range.last.column; ++column)
                        {
                            const double width = widths[column - range.first.column];
                            bool covered = false;
                            double cell_width = width, cell_height = height;
                            for (auto m : features.merges)
                            {
                                if (row < m.first.row || row > m.last.row || column < m.first.column ||
                                    column > m.last.column)
                                    continue;
                                covered = row != m.first.row || column != m.first.column;
                                if (!covered)
                                {
                                    cell_width = 0;
                                    cell_height = 0;
                                    for (auto c = m.first.column; c <= m.last.column; ++c)
                                        cell_width += widths[c - range.first.column];
                                    for (auto r = m.first.row; r <= m.last.row; ++r)
                                        cell_height += height_at(r);
                                }
                                break;
                            }
                            if (!covered && cell_width > 0 && cell_height > 0)
                                sheet_cell(
                                    painter, QRectF(x, y, cell_width, cell_height), model, row, column);
                            x += width;
                        }
                        y += height;
                        ++row;
                    }
                    require(!progress.cancelled.load(), "导出已取消。");
                }
                painter.restore();
            }
        }
        require(output.count > 0, "没有可导出的工作表。");
        progress.total.store(output.count);
        return output.finish();
    }
}

namespace mirrorfly
{
    PdfBytesResult render_pdf_export(const PdfExportSource& source, const PdfExportOptions& options,
        const QVariantMap& theme, PdfExportProgress& progress)
    {
        try
        {
            if (std::holds_alternative<PdfTextSource>(source.content) ||
                std::holds_alternative<WordDocument>(source.content))
                return text_export(source, options, theme, progress);
            if (std::holds_alternative<SpreadsheetDocument>(source.content))
                return sheets_export(source, options, progress);
            if (std::holds_alternative<std::shared_ptr<const PresentationScene>>(source.content))
                return slides_export(source, options, theme, progress);
            if (std::holds_alternative<MindMapDocument>(source.content))
                return graph_export(source, options, theme, progress);
            if (const auto* pdf = std::get_if<PdfDocument>(&source.content))
            {
                auto document = *pdf;
                if (options.scope == "current")
                {
                    require(source.current < document.pages.size(), "导出页码无效。");
                    document.pages = {document.pages[source.current]};
                }
                if (options.compression != "structure")
                    return compress_pdf_document(document, options.compression == "print" ? 144 : 96,
                        options.compression == "print" ? 82 : 65, [&](int done, int total)
                    {
                        progress.completed.store(done);
                        progress.total.store(total);
                        return !progress.cancelled.load();
                    });
                return serialize_pdf_document(document);
            }
            return {PdfError::InvalidDocument, "当前没有可导出的文档。", {}};
        }
        catch (const std::exception& error)
        {
            return {PdfError::WriteFailed, error.what(), {}};
        }
        catch (...)
        {
            return {PdfError::WriteFailed, "PDF 导出失败，原文档和目标文件未改动。", {}};
        }
    }
}
