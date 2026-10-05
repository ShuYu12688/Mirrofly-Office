#include "word_document.hpp"
#include "word_viewport.hpp"

#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextTable>

#include <iostream>
#include <memory>

namespace
{
    class Surface : public mirrorfly::WordViewport
    {
    public:
        QImage frame()
        {
            updatePolish();
            QImage result(600, 400, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::white);
            QPainter painter(&result);
            paint(&painter);
            return result;
        }

        QImage fullFrame()
        {
            const auto scale = renderScale();
            setRenderScale(scale + 0.001);
            setRenderScale(scale);
            return frame();
        }

        QImage reference(QTextDocument& document)
        {
            QImage pixels(QSize(int(600 * renderScale()), int(400 * renderScale())),
                QImage::Format_ARGB32_Premultiplied);
            pixels.fill(Qt::transparent);
            QPainter raster(&pixels);
            raster.setRenderHints(
                QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
            raster.scale(pixels.width() / 600.0, pixels.height() / 400.0);
            raster.translate(0, -documentTop());
            document.documentLayout()->draw(&raster, {});
            raster.end();
            QImage result(600, 400, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::white);
            QPainter painter(&result);
            painter.drawImage(QRectF(0, 0, 600, 400), pixels);
            return result;
        }
    };

    int failures = 0;
    int comparisons = 0;

    void check_pixels(const QImage& local, const QImage& full, const char* action, int rule, qreal scale,
        int allowed_delta = 0)
    {
        ++comparisons;
        if (local == full)
            return;
        int pixels = 0;
        int maximum_delta = 0;
        QRect bounds;
        for (int y = 0; y < local.height(); ++y)
            for (int x = 0; x < local.width(); ++x)
                if (local.pixel(x, y) != full.pixel(x, y))
                {
                    ++pixels;
                    for (const auto shift : {0, 8, 16, 24})
                        maximum_delta = std::max(maximum_delta,
                            std::abs(int((local.pixel(x, y) >> shift) & 255) -
                                int((full.pixel(x, y) >> shift) & 255)));
                    bounds = bounds.united(QRect(x, y, 1, 1));
                }
        if (maximum_delta <= allowed_delta)
            return;
        ++failures;
        std::cerr << action << " rule=" << rule << " scale=" << scale << " pixels=" << pixels
                  << " bounds=" << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ','
                  << bounds.height() << " maximumChannelDelta=" << maximum_delta << '\n';
    }

    void table_cases(QQmlEngine& engine, qreal scale)
    {
        mirrorfly::WordDocument source;
        source.paragraphs.resize(4);
        source.tables.resize(1);
        source.blocks = {{mirrorfly::WordBlock::Kind::Table, 0}};
        auto& table = source.tables[0];
        table.source_id = 1;
        table.rows = 2;
        table.column_widths = {140, 140};
        for (std::size_t index = 0; index < 4; ++index)
        {
            source.paragraphs[index].runs = {{"Border text 汉字"}};
            mirrorfly::WordTableCell cell;
            cell.row = index / 2;
            cell.column = index % 2;
            cell.blocks = {{mirrorfly::WordBlock::Kind::Paragraph, index}};
            cell.border_rows.resize(1);
            for (auto& edge : cell.border_rows[0])
                edge = {"single", "#224466", 1, true};
            table.cells.push_back(cell);
        }
        auto document = mirrorfly::create_word_document(source, 600);
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 600; textFormat: TextEdit.RichText; "
                          "wrapMode: TextEdit.Wrap }",
            QUrl{});
        std::unique_ptr<QObject> editor(component.create());
        if (!editor)
        {
            ++failures;
            return;
        }
        auto* wrapper = editor->property("textDocument").value<QQuickTextDocument*>();
        wrapper->setTextDocument(document.get());
        Surface surface;
        surface.setSize(QSizeF(600, 400));
        surface.setTextDocument(wrapper);
        surface.setRenderScale(scale);
        surface.frame();
        auto* grid = qobject_cast<QTextTable*>(document->rootFrame()->childFrames().front());
        const auto start = grid->cellAt(0, 0).firstPosition();
        for (const auto* edge : {"left", "top", "right", "bottom"})
            for (const double width : {0.25, 6.0, 12.0, 0.0})
            {
                if (!mirrorfly::format_word_document(*document, start, start, "cellBorder",
                        QVariantMap{{"edge", edge}, {"style", width ? "double" : "nil"}, {"color", "#aa3322"},
                            {"width", width}}))
                    ++failures;
                const auto edited = surface.frame();
                check_pixels(edited, surface.fullFrame(), "cell border edit", -1, scale);
                document->undo();
                const auto undone = surface.frame();
                check_pixels(undone, surface.fullFrame(), "cell border undo", -1, scale);
                document->redo();
                const auto redone = surface.frame();
                check_pixels(redone, surface.fullFrame(), "cell border redo", -1, scale);
            }
    }

    void cases(QQmlEngine& engine, int rule, qreal scale, bool background)
    {
        mirrorfly::WordDocument source;
        source.paragraphs.resize(12);
        for (auto& paragraph : source.paragraphs)
        {
            paragraph.runs = {{"编辑测试 Agjy 汉字笔画"}};
            paragraph.runs[0].size = 36;
            paragraph.runs[0].italic = true;
            paragraph.runs[0].underline = background;
            paragraph.runs[0].background = background ? "#ffffff" : "";
            paragraph.background = background ? "#f3f6ff" : "";
            paragraph.line_spacing_rule = rule;
            paragraph.line_spacing_points = 18;
            paragraph.line_spacing = 1.0;
            paragraph.space_after = 0;
        }
        auto document = mirrorfly::create_word_document(source, 600);
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 600; textFormat: TextEdit.RichText; "
                          "wrapMode: TextEdit.Wrap }",
            QUrl{});
        std::unique_ptr<QObject> editor(component.create());
        if (!editor)
        {
            ++failures;
            return;
        }
        auto* wrapper = editor->property("textDocument").value<QQuickTextDocument*>();
        wrapper->setTextDocument(document.get());
        Surface surface;
        surface.setSize(QSizeF(600, 400));
        surface.setTextDocument(wrapper);
        surface.setRenderScale(scale);
        surface.frame();
        for (int step = 0; step < 25; ++step)
        {
            emit document->documentLayout()->update(QRectF(0, 60.13 + step * 0.73, 600, 0.63));
            const auto local = surface.frame();
            check_pixels(local, surface.fullFrame(), "narrow strip", rule, scale);
        }
        const auto block = document->findBlockByNumber(2);
        const int start = block.position(), end = start + block.length() - 1;
        for (const auto size : {72.0, 12.0, 36.0})
        {
            if (!mirrorfly::format_word_document(*document, start, end, "size", size))
                ++failures;
            const auto local = surface.frame();
            check_pixels(local, surface.fullFrame(), "font size", rule, scale);
            document->undo();
            const auto undone = surface.frame();
            check_pixels(undone, surface.fullFrame(), "undo", rule, scale);
            document->redo();
            const auto redone = surface.frame();
            check_pixels(redone, surface.fullFrame(), "redo", rule, scale);
        }
        for (const auto top : {80.0, 0.0, 0.37, 80.37})
        {
            surface.setDocumentTop(top);
            const auto local = surface.frame();
            // Integral raster translations can round one 8-bit antialiasing channel at the edge.
            check_pixels(local, surface.fullFrame(), "scroll", rule, scale, 1);
            check_pixels(surface.frame(), surface.reference(*document), "unculled reference", rule, scale);
        }
        surface.setSelection({{"start", start}, {"end", end}, {"background", QColor("#4466dd")},
            {"foreground", QColor(Qt::white)}});
        const auto selected = surface.frame();
        check_pixels(selected, surface.fullFrame(), "selection", rule, scale);
        surface.setSelection({});
        const auto cleared = surface.frame();
        check_pixels(cleared, surface.fullFrame(), "clear selection", rule, scale);
        QTextCursor erase(document.get());
        erase.setPosition(start);
        erase.setPosition(end + 1, QTextCursor::KeepAnchor);
        erase.removeSelectedText();
        const auto erased = surface.frame();
        check_pixels(erased, surface.fullFrame(), "delete paragraph", rule, scale);
        document->undo();
        const auto restored = surface.frame();
        check_pixels(restored, surface.fullFrame(), "restore paragraph", rule, scale);
        for (const auto spacing : {1.5, 1.0})
        {
            if (!mirrorfly::format_word_document(*document, start, end, "spacing", spacing))
                ++failures;
            const auto local = surface.frame();
            check_pixels(local, surface.fullFrame(), "line rule change", rule, scale);
        }
        surface.setDocumentTop(0);
        surface.frame();
        mirrorfly::format_word_document(*document, start, end, "color", "#bb2233");
        const auto recolored = surface.frame();
        if (surface.lastPaintedDocumentArea().height() >= surface.height())
        {
            ++failures;
            std::cerr << "ordinary paragraph must retain local repaint after leaving fixed line spacing\n";
        }
        check_pixels(recolored, surface.fullFrame(), "ordinary local recolor", rule, scale);
        for (const auto* action : {"underlineStyle", "strikeStyle"})
        {
            if (!mirrorfly::format_word_document(*document, start, end, action, "double"))
                ++failures;
            const auto decorated = surface.frame();
            check_pixels(decorated, surface.fullFrame(), "double line local repaint", rule, scale);
            document->undo();
            const auto undone = surface.frame();
            check_pixels(undone, surface.fullFrame(), "double line undo repaint", rule, scale);
            document->redo();
            const auto redone = surface.frame();
            check_pixels(redone, surface.fullFrame(), "double line redo repaint", rule, scale);
        }
    }
}

int run_word_viewport_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QQmlEngine engine;
    QList<qreal> scales{0.75, 1.0, 1.25, 1.5, 2.0};
    if (argc > 1)
    {
        bool valid = false;
        const auto scale = QString::fromUtf8(argv[1]).toDouble(&valid);
        if (argc != 2 || !valid || !scales.contains(scale))
        {
            std::cerr << "Expected one supported viewport scale\n";
            return 1;
        }
        scales = {scale};
    }
    for (const auto scale : scales)
        table_cases(engine, scale);
    for (int rule = 0; rule < 3; ++rule)
        for (const auto scale : scales)
            for (const auto background : {false, true})
                cases(engine, rule, scale, background);
    std::cout << "comparisons=" << comparisons << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_viewport_tests(argc, argv);
}
