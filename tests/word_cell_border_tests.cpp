#include "word_cell_borders.hpp"
#include "word_document.hpp"

#include <QAbstractTextDocumentLayout>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTextCursor>

#include <algorithm>
#include <iostream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }

    std::vector<OfficePart> fixture()
    {
        auto parts = serialize_word(WordDocument{}).parts;
        const auto main = std::find_if(parts.begin(), parts.end(), [](const auto& part)
        {
            return part.path == "word/document.xml";
        });
        main->bytes =
            R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
<w:tbl><w:tblPr><w:tblBorders>
<w:top w:val="single" w:sz="16" w:color="0000FF"/>
<w:left w:val="single" w:sz="16" w:color="FF0000"/>
<w:right w:val="single" w:sz="16" w:color="00FF00"/>
<w:bottom w:val="single" w:sz="16" w:color="FFFF00"/>
</w:tblBorders></w:tblPr><w:tblGrid><w:gridCol w:w="2000"/><w:gridCol w:w="2000"/></w:tblGrid>
<w:tr><w:tc><w:tcPr><w:tcBorders><w:right w:val="dashed" w:sz="48" w:color="FF00FF"/></w:tcBorders></w:tcPr><w:p/></w:tc>
<w:tc><w:tcPr><w:tcBorders><w:left w:val="single" w:sz="8" w:color="00FFFF"/></w:tcBorders></w:tcPr><w:p/></w:tc></w:tr>
</w:tbl></w:body></w:document>)xml";
        return parts;
    }

    QTextTable* table(QTextDocument& document)
    {
        for (auto* frame : document.rootFrame()->childFrames())
            if (auto* result = qobject_cast<QTextTable*>(frame))
                return result;
        return nullptr;
    }

    void read_edit_save()
    {
        const auto parts = fixture();
        const auto original = parse_word(parts);
        auto document = create_word_document(original.document, 420);
        auto* grid = table(*document);
        check(grid != nullptr, "table exists");
        if (!grid)
            return;
        auto left = grid->cellAt(0, 0);
        const auto right = grid->cellAt(0, 1);
        check(left.format().toTableCellFormat().rightBorderBrush().color() == QColor("#00FFFF") &&
                right.format().toTableCellFormat().leftBorderBrush().color() == QColor("#00FFFF") &&
                left.format().toTableCellFormat().rightBorderStyle() == QTextFrameFormat::BorderStyle_Solid,
            "both paint sides use Word winner instead of wider Qt dashed edge");
        auto read = inspect_word_document(*document, left.firstPosition()).value("cellBorders").toMap();
        check(read.value("source").toMap().value("right").toMap().value("style").toString() == "dashed" &&
                read.value("display").toMap().value("exact").toBool(),
            "inspection separates source border from shared display result");
        auto extracted = extract_word_document(*document);
        auto saved = serialize_word(extracted.document);
        check(extracted.success && saved.success && saved.parts.size() == parts.size(),
            "border source extracts");
        for (std::size_t i = 0; saved.success && i < parts.size(); ++i)
            check(
                saved.parts[i].bytes == parts[i].bytes, "display conflict resolution never flattens source");
        const auto start = left.firstPosition();
        const QVariantMap change{{"edge", "right"}, {"style", "nil"}, {"color", "#112233"}, {"width", 0}};
        check(format_word_document(*document, start, start, "cellBorder", change),
            "public format edits border");
        check(left.format().toTableCellFormat().rightBorder() == 0 &&
                right.format().toTableCellFormat().leftBorder() == 0,
            "nil removes both rendered halves of the shared edge");
        extracted = extract_word_document(*document);
        saved = serialize_word(extracted.document);
        const auto reopened = parse_word(saved.parts);
        check(reopened.success && reopened.document.tables[0].cells[0].border_rows[0][2].style == "nil" &&
                reopened.document.tables[0].cells[1].border_rows ==
                    original.document.tables[0].cells[1].border_rows,
            "save changes only selected source edge, never the opposing source edge");
        document->undo();
        check(left.format().toTableCellFormat().rightBorderBrush().color() == QColor("#00FFFF"),
            "one undo restores source and display formats");
        extracted = extract_word_document(*document);
        saved = serialize_word(extracted.document);
        for (std::size_t i = 0; saved.success && i < parts.size(); ++i)
            check(saved.parts[i].bytes == parts[i].bytes, "undo restores original package bytes");
        document->redo();
        check(right.format().toTableCellFormat().leftBorder() == 0, "redo restores suppressed display");
        std::unique_ptr<QTextDocument> clone(document->clone());
        extracted = extract_word_document(*clone, document.get());
        check(extracted.success && extracted.document.tables[0].cells[0].border_rows[0][2].style == "nil",
            "preflight clone retains source border metadata");
        const auto prior = left.format();
        auto invalid = change;
        invalid.insert("width", "1");
        check(!format_word_document(*document, start, start, "cellBorder", invalid) && left.format() == prior,
            "typed invalid input leaves cell unchanged");
        invalid = change;
        invalid.insert("extra", true);
        check(!format_word_document(*document, start, start, "cellBorder", invalid), "unknown field rejects");
        check(!format_word_document(*document, start, right.lastPosition(), "cellBorder", change),
            "border transaction cannot span multiple cells");
    }

    void large_table()
    {
        auto parts = fixture();
        const auto main = std::find_if(parts.begin(), parts.end(), [](const auto& part)
        {
            return part.path == "word/document.xml";
        });
        std::string grid, rows;
        for (int column = 0; column < 8; ++column)
            grid += "<w:gridCol w:w='600'/>";
        for (int row = 0; row < 50; ++row)
        {
            rows += "<w:tr>";
            for (int column = 0; column < 8; ++column)
                rows += "<w:tc><w:p><w:r><w:t>Cell</w:t></w:r></w:p></w:tc>";
            rows += "</w:tr>";
        }
        main->bytes = "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
                      "<w:body><w:tbl><w:tblGrid>" +
            grid + "</w:tblGrid>" + rows + "</w:tbl></w:body></w:document>";
        QElapsedTimer timer;
        timer.start();
        const auto parsed = parse_word(parts);
        const auto parse_ms = timer.elapsed();
        check(parsed.success, "large table fixture parses");
        if (!parsed.success)
            return;
        auto document = create_word_document(parsed.document, 600);
        document->documentLayout()->documentSize();
        const auto layout_ms = timer.elapsed() - parse_ms;
        auto* grid_table = table(*document);
        const auto position = grid_table->cellAt(25, 3).firstPosition();
        timer.restart();
        check(format_word_document(*document, position, position, "cellBorder",
                  QVariantMap{{"edge", "right"}, {"style", "double"}, {"width", 1.25}, {"color", "#112233"}}),
            "large table border editing succeeds");
        document->documentLayout()->documentSize();
        const auto edit_ms = timer.elapsed();
        std::cout << "400 cells parse_ms=" << parse_ms << " layout_ms=" << layout_ms
                  << " edit_layout_ms=" << edit_ms << '\n';
        auto extracted = extract_word_document(*document);
        check(extracted.success &&
                extracted.document.tables[0].cells[25 * 8 + 3].border_rows[0][2].width == 1.25,
            "large table edit targets exactly the requested cell");
    }

    void mixed_and_bounded_read()
    {
        auto parts = fixture();
        auto main = std::find_if(parts.begin(), parts.end(), [](const auto& part)
        {
            return part.path == "word/document.xml";
        });
        std::string rows;
        for (int row = 0; row < 12; ++row)
        {
            rows += "<w:tr><w:tc><w:tcPr><w:vMerge";
            rows += row == 0 ? " w:val='restart'/>" : "/>";
            rows += "<w:tcBorders><w:left w:val='single' w:sz='8' w:color='";
            rows += row == 0 ? "112233" : "AABBCC";
            rows += "'/></w:tcBorders></w:tcPr><w:p/></w:tc><w:tc><w:p/></w:tc></w:tr>";
        }
        main->bytes = "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
                      "<w:body><w:tbl><w:tblGrid><w:gridCol w:w='2000'/><w:gridCol w:w='2000'/>"
                      "</w:tblGrid>" +
            rows + "</w:tbl></w:body></w:document>";
        const auto source = parse_word(parts);
        auto document = create_word_document(source.document, 420);
        const auto start = table(*document)->cellAt(0, 0).firstPosition();
        const auto state = inspect_word_document(*document, start).value("cellBorders").toMap();
        const auto display = state.value("display").toMap();
        const auto left = display.value("edges").toMap().value("left").toMap();
        check(state.value("source").toMap().value("left").toMap().value("mixed").toBool() &&
                !display.value("exact").toBool() && left.value("segmentCount").toInt() == 12 &&
                left.value("segments").toList().size() == 8 && !left.value("segmentsComplete").toBool(),
            "mixed merged borders are explicit and AI preview is bounded without claiming completeness");
        check(format_word_document(*document, start, start, "cellBorder",
                  QVariantMap{{"edge", "left"}, {"style", "single"}, {"color", "#123456"}, {"width", 1}}),
            "whole-edge editing deliberately unifies merged segments");
        const auto uniform = inspect_word_document(*document, start).value("cellBorders").toMap();
        check(uniform.value("display").toMap().value("exact").toBool(),
            "uniform segments restore exact native edge support");
        document->undo();
        check(!inspect_word_document(*document, start)
                  .value("cellBorders")
                  .toMap()
                  .value("display")
                  .toMap()
                  .value("exact")
                  .toBool(),
            "undo restores heterogeneous source and its display limitation");
        auto spaced = source.document;
        spaced.tables[0].border_layout_supported = false;
        auto unsupported = create_word_document(spaced, 420);
        check(!inspect_word_document(*unsupported, table(*unsupported)->cellAt(0, 1).firstPosition())
                  .value("cellBorders")
                  .toMap()
                  .value("display")
                  .toMap()
                  .value("exact")
                  .toBool(),
            "source layout limitations propagate to border inspection");
    }

    void raster_edges()
    {
        auto document = create_word_document(parse_word(fixture()).document, 420);
        const auto size = document->documentLayout()->documentSize();
        QImage image(QSize(440, qCeil(size.height()) + 20), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        document->documentLayout()->draw(&painter, {});
        painter.end();
        const QRgb colors[] = {qRgb(255, 0, 0), qRgb(0, 0, 255), qRgb(0, 255, 0), qRgb(255, 255, 0),
            qRgb(0, 255, 255), qRgb(255, 0, 255)};
        std::array<QRect, 6> bounds;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                for (std::size_t c = 0; c < bounds.size(); ++c)
                    if (image.pixel(x, y) == colors[c])
                        bounds[c] = bounds[c].united(QRect(x, y, 1, 1));
        check(!bounds[0].isEmpty() && !bounds[1].isEmpty() && !bounds[2].isEmpty() && !bounds[3].isEmpty(),
            "native raster contains four independently colored exterior borders");
        check(bounds[0].width() < 6 && bounds[2].width() < 6 && bounds[1].height() < 6 &&
                bounds[3].height() < 6,
            "top border color is not incorrectly painted around the whole table");
        check(!bounds[4].isEmpty() && bounds[5].isEmpty(),
            "raster shared border uses winning cyan, never magenta");
    }
}

int run_word_cell_border_tests(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    read_edit_save();
    raster_edges();
    large_table();
    mixed_and_bounded_read();
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    return run_word_cell_border_tests(argc, argv);
}
