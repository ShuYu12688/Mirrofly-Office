#include "pdf_export_bridge.hpp"
#include "pdf_stamp.hpp"
#include "spreadsheet_border_renderer.hpp"
#include "spreadsheet_text_renderer.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <mirrorfly/pdf_storage.hpp>

namespace
{
    int failures = 0;
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }

    bool wait(mirrorfly::PdfExportBridge& exporter)
    {
        QElapsedTimer timer;
        timer.start();
        while (exporter.busy() && timer.elapsed() < 20000)
        {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        return !exporter.busy();
    }
}

int run_pdf_export_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    using namespace mirrorfly;
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(20);
        const QString text = QString::fromUtf8("á中👩‍💻文");
        const QVariantMap format{{"align", "distributed"}, {"wrap", "0"}, {"valign", "top"}};
        const auto narrow = layout_spreadsheet_text(text, font, 240, format);
        const auto wide = layout_spreadsheet_text(text, font, 480, format);
        const auto positions = [](const SpreadsheetTextLayout& layout)
        {
            QMap<qsizetype, QList<qreal>> result;
            for (const auto& run : layout.glyphs)
            {
                const auto indexes = run.stringIndexes();
                const auto points = run.positions();
                for (qsizetype index = 0; index < indexes.size(); ++index)
                    result[indexes[index]].push_back(points[index].x());
            }
            return result;
        };
        const auto a = positions(narrow), b = positions(wide);
        check(a.size() > 2 && a.keys() == b.keys(), "distributed layout retains source glyph mapping");
        if (!a.isEmpty())
        {
            check(std::abs(a.first().first() - b.first().first()) < .1 &&
                    std::abs(b.last().first() - a.last().first() - 240) < .1,
                "distributed text expands to the new width without shifting its first cluster");
            std::optional<qreal> emoji_shift;
            for (auto entry = a.cbegin(); entry != a.cend(); ++entry)
                if (entry.key() >= 3 && entry.key() < text.size() - 1)
                {
                    const auto shift = b.value(entry.key()).first() - entry.value().first();
                    check(!emoji_shift || std::abs(*emoji_shift - shift) < .1,
                        "joined emoji glyphs move as one grapheme cluster");
                    emoji_shift = shift;
                }
        }
        const auto multiline = layout_spreadsheet_text(
            QStringLiteral("甲乙丙丁\n甲乙"), font, 45, {{"align", "distributed"}, {"wrap", "1"}});
        check(multiline.height > 50 && !multiline.glyphs.empty(),
            "distributed text preserves wrapping and explicit line breaks");
        const auto rtl_narrow = positions(layout_spreadsheet_text(QStringLiteral("אבג"), font, 240, format));
        const auto rtl_wide = positions(layout_spreadsheet_text(QStringLiteral("אבג"), font, 480, format));
        check(rtl_narrow.size() == 3 && rtl_wide.size() == 3 &&
                std::abs(rtl_wide.last().first() - rtl_narrow.last().first()) < .1 &&
                std::abs(rtl_wide.first().first() - rtl_narrow.first().first() - 240) < .1,
            "distributed RTL follows visual order without reversing characters");
        const auto justified = layout_spreadsheet_text(
            "alpha beta gamma delta epsilon", font, 130, {{"align", "justify"}, {"wrap", "1"}});
        check(justified.height > 40, "justified text wraps through the shared layout engine");
        const QString long_text = QStringLiteral("长标题 Abc 123456789 数据核对");
        const auto full = layout_spreadsheet_text(long_text, font, 80, {{"wrap", "0"}});
        const auto fit = layout_spreadsheet_text(
            long_text, font, 80, {{"wrap", "0"}, {"shrinkToFit", "1"}, {"align", "right"}});
        check(fit.scale < 1 && fit.width <= 80 && fit.height < full.height &&
                fit.glyphs.size() == full.glyphs.size() && font.pixelSize() == 20,
            "shared GUI/PDF shrink preserves all shaped text and the original font");
        const auto spacious = layout_spreadsheet_text(long_text, font, 2000, {{"shrinkToFit", "1"}});
        check(spacious.scale == 1, "shrink-to-fit never enlarges short text");
        const auto wrapped =
            layout_spreadsheet_text(long_text, font, 80, {{"wrap", "1"}, {"shrinkToFit", "1"}});
        const auto explicit_lines = layout_spreadsheet_text(
            "long first line\nsecond", font, 20, {{"wrap", "0"}, {"shrinkToFit", "1"}});
        check(wrapped.scale == 1 && explicit_lines.scale == 1,
            "wrapped or explicit multiline cells do not shrink");
        QImage pixels(260, 60, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::white);
        QPainter painter(&pixels);
        painter.setFont(font);
        painter.setPen(Qt::black);
        paint_spreadsheet_aligned_text(painter, QRectF(10, 10, 240, 40), QStringLiteral("甲乙丙丁"), format);
        painter.end();
        bool right_ink = false;
        for (int y = 10; y < 50; ++y)
            for (int x = 228; x < 250; ++x)
                right_ink = right_ink || pixels.pixelColor(x, y) != QColor(Qt::white);
        check(right_ink, "distributed final glyph reaches right edge in headless painting");
    }
    {
        QImage image(80, 40, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        const QVariantMap edges{{"borderBottom", "thick"}, {"borderBottomColor", "#FF0000"},
            {"borderRight", "double"}, {"borderRightColor", "#0000FF"}};
        paint_spreadsheet_borders(painter, QRectF(0, 0, 80, 40), edges, 1);
        painter.end();
        check(image.pixelColor(30, 38) == QColor(Qt::red) && image.pixelColor(30, 35) == QColor(Qt::white),
            "spreadsheet thick bottom border paints inside its edge");
        check(image.pixelColor(0, 20) == QColor(Qt::white) && image.pixelColor(30, 0) == QColor(Qt::white),
            "spreadsheet border painter leaves unset edges untouched");
        check(image.pixelColor(79, 20) == QColor(Qt::blue) && image.pixelColor(77, 20) == QColor(Qt::blue) &&
                image.pixelColor(78, 20) == QColor(Qt::white),
            "spreadsheet double border has a visible gap");
    }
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory");
    const QVariantMap theme{{"fontFamily", "Microsoft YaHei"}, {"fontSize", 14}, {"textPrimary", "#20242A"},
        {"textSecondary", "#555555"}, {"surfaceColor", "#FFFFFF"}, {"accent", "#326976"},
        {"mindmapNodeBorder", "#326976"}, {"mindmapNodeFill", "#F2FAFB"}, {"mindmapEdge", "#507C85"},
        {"onAccent", "#FFFFFF"}};
    PdfExportSource source;
    source.title = "导出回归";
    source.content = PdfTextSource{"Hello export\n中文文档导出", false};
    PdfExportOptions options;
    const auto export_file = [&](const QString& name) -> PdfResult
    {
        PdfExportProgress progress;
        auto bytes = render_pdf_export(source, options, theme, progress);
        if (bytes.error != PdfError::None)
            std::cerr << bytes.message << '\n';
        check(bytes.error == PdfError::None && !bytes.bytes.empty(), "render PDF bytes");
        const auto path = directory.filePath(name).toStdString();
        check(save_pdf_bytes(path, bytes.bytes, "missing").error == PdfError::None, "atomic PDF write");
        auto loaded = load_pdf_file(path);
        check(loaded.error == PdfError::None && !loaded.document.pages.empty(),
            "PDFium independently reopens export");
        return loaded;
    };
    auto plain = export_file("text.pdf");
    check(plain.error == PdfError::None &&
            plain.document.pages[0].text.find("Hello export") != std::string::npos,
        "plain text remains searchable");
    check(plain.error == PdfError::None && plain.document.pages[0].text.find("中文") != std::string::npos,
        "CJK font embedding and extraction");
    source.content = PdfTextSource{"# Markdown 标题\n\n**重点**与正文。\n\n- 第一项\n- 第二项", true};
    export_file("markdown.pdf");
    WordDocument word;
    word.paragraphs[0].runs.push_back({"Word 中文正文"});
    for (int index = 0; index < 100; ++index)
        word.paragraphs.push_back(word.paragraphs[0]);
    source.content = word;
    auto word_pdf = export_file("word.pdf");
    check(word_pdf.document.pages.size() > 1, "Word paginates overflowing content");
    WordDocument lines;
    WordRun line_run{"                    "};
    line_run.font = line_run.east_asia_font = "Arial";
    line_run.size = 36;
    line_run.underline = line_run.double_underline = true;
    line_run.strike = line_run.double_strike = true;
    lines.paragraphs[0].runs = {line_run};
    source.content = lines;
    const auto line_pdf = export_file("double-lines.pdf");
    const auto line_pixels = render_pdf_page(line_pdf.document, line_pdf.document.pages[0].id, 1190, 1684);
    check(line_pixels.error == PdfError::None, "PDFium renders double-line export");
    if (line_pixels.error == PdfError::None)
    {
        int groups = 0;
        bool previous_ink = false;
        for (int y = 70; y < 200; ++y)
        {
            const auto offset = (y * line_pixels.width + 100) * 4;
            const bool ink = line_pixels.rgba[offset] < 160;
            if (ink && !previous_ink)
                ++groups;
            previous_ink = ink;
        }
        check(groups == 4,
            "independent PDF raster has two separate underline and strike strokes, no third line");
    }
    WordDocument phonetic;
    WordRun han;
    han.text = "中";
    han.ruby = "zhōng";
    han.size = 18;
    phonetic.paragraphs[0].runs.push_back(han);
    source.content = phonetic;
    auto phonetic_pdf = export_file("phonetic.pdf");
    check(phonetic_pdf.document.pages[0].text.find("中") != std::string::npos &&
            phonetic_pdf.document.pages[0].text.find("zhōng") != std::string::npos,
        "phonetic guide survives PDF clipping and independent text extraction");
    WordDocument distributed;
    distributed.paragraphs[0].alignment = 4;
    distributed.paragraphs[0].runs.push_back({"甲乙丙丁"});
    source.content = distributed;
    auto distributed_pdf = export_file("distributed.pdf");
    const auto distributed_text = distributed_pdf.document.pages[0].text;
    check(
        distributed_text.find("甲") != std::string::npos && distributed_text.find("丁") != std::string::npos,
        "distributed paragraph remains readable by independent PDF text extraction");
    auto sheets = make_spreadsheet();
    apply_spreadsheet_edit(sheets, {0, {0, 0}, {SpreadsheetValueKind::Text, "Sheet value"}});
    auto sheet_features = spreadsheet_features(sheets, 0);
    sheet_features.merges.push_back({{0, 0}, {0, 3}});
    sheet_features.hidden_rows.insert(1);
    apply_spreadsheet_edit(sheets, {0, {1, 0}, {SpreadsheetValueKind::Text, "hidden export row"}});
    check(apply_spreadsheet_features(sheets, 0, sheet_features).changed, "merged and hidden export source");
    const auto added = apply_spreadsheet_sheet_command(
        sheets, {SpreadsheetSheetAction::Add, sheets.sheets.size(), "第二页"});
    check(added.changed, "add second worksheet");
    source.content = sheets;
    auto sheets_pdf = export_file("sheets.pdf");
    check(sheets_pdf.document.pages[0].text.find("Sheet value") != std::string::npos &&
            sheets_pdf.document.pages[0].text.find("hidden export row") == std::string::npos,
        "PDF retains merged owner and omits hidden rows");
    check(sheets_pdf.document.pages.size() == 2, "one printed page per small worksheet");
    source.current = 1;
    options.scope = "current";
    auto selected_sheet = export_file("sheet.pdf");
    check(selected_sheet.document.pages.size() == 1, "current worksheet scope");
    options.scope = "all";
    source.current = 0;
    check(apply_spreadsheet_sheet_command(sheets, {SpreadsheetSheetAction::Hide, 1}).changed,
        "hidden worksheet export fixture");
    source.content = sheets;
    auto visible_sheets = export_file("visible-sheets.pdf");
    check(visible_sheets.document.pages.size() == 1, "whole-workbook PDF skips hidden worksheets");
    auto aligned = make_spreadsheet();
    apply_spreadsheet_edit(aligned, {0, {0, 0}, {SpreadsheetValueKind::Text, "甲乙丙丁"}});
    apply_spreadsheet_formats(aligned, {{0, {0, 0}, {{"align", "distributed"}, {"size", "20"}}}});
    apply_spreadsheet_dimensions(aligned, {{0, true, 0, 40}});
    source.content = aligned;
    const auto aligned_pdf = export_file("distributed-sheet.pdf");
    check(aligned_pdf.document.pages[0].text.find("甲") != std::string::npos &&
            aligned_pdf.document.pages[0].text.find("丁") != std::string::npos,
        "distributed spreadsheet PDF retains searchable first and last characters");
    auto fitted_sheet = make_spreadsheet();
    apply_spreadsheet_edit(
        fitted_sheet, {0, {0, 0}, {SpreadsheetValueKind::Text, "Shrink layout retains FINAL123"}});
    apply_spreadsheet_formats(
        fitted_sheet, {{0, {0, 0}, {{"size", "24"}, {"shrinkToFit", "1"}, {"align", "right"}}}});
    apply_spreadsheet_dimensions(fitted_sheet, {{0, true, 0, 12}});
    source.content = fitted_sheet;
    const auto fitted_pdf = export_file("shrink-sheet.pdf");
    check(fitted_pdf.document.pages[0].text.find("FINAL123") != std::string::npos &&
            spreadsheet_cell_format(fitted_sheet, 0, {0, 0}).at("size") == "24",
        "shrunk PDF keeps its final text searchable and leaves stored font size unchanged");
    const auto fitted_pixels =
        render_pdf_page(fitted_pdf.document, fitted_pdf.document.pages[0].id, 595, 842);
    check(fitted_pixels.error == PdfError::None, "independent PDFium can render scaled spreadsheet glyphs");
    for (const int rotation : {45, 90, 135, 180, 255})
    {
        auto oriented = make_spreadsheet();
        apply_spreadsheet_edit(oriented, {0, {0, 0}, {SpreadsheetValueKind::Text, "RotateX9"}});
        apply_spreadsheet_formats(
            oriented, {{0, {0, 0}, {{"textRotation", std::to_string(rotation)}, {"size", "12"}}}});
        apply_spreadsheet_dimensions(oriented, {{0, true, 0, 40}, {0, false, 0, 200}});
        source.content = oriented;
        const auto exported = export_file(QStringLiteral("rotation-%1.pdf").arg(rotation));
        auto extracted = exported.document.pages[0].text;
        extracted.erase(std::remove_if(extracted.begin(), extracted.end(),
                            [](unsigned char c)
        {
            return std::isspace(c);
        }),
            extracted.end());
        check(extracted.find("RotateX9") != std::string::npos,
            "rotated and stacked PDF remains searchable text with all characters");
        check(render_pdf_page(exported.document, exported.document.pages[0].id, 595, 842).error ==
                PdfError::None,
            "PDFium independently renders every supported text direction");
    }
    auto table_book = make_spreadsheet();
    apply_spreadsheet_edits(table_book,
        {{0, {0, 0}, {SpreadsheetValueKind::Text, "Key"}}, {0, {0, 1}, {SpreadsheetValueKind::Text, "Value"}},
            {0, {1, 0}, {SpreadsheetValueKind::Text, "Table row"}}});
    SpreadsheetTable table;
    table.name = "PdfTable";
    table.range = {{0, 0}, {5, 1}};
    table.style = {{"wholeTable", {{"fill", "#FFFFFF"}}},
        {"headerRow", {{"fill", "#345B89"}, {"text", "#FFFFFF"}, {"bold", "1"}}},
        {"firstRowStripe", {{"fill", "#ABCDEF"}}}};
    auto table_features = spreadsheet_features(table_book, 0);
    table_features.tables.push_back(table);
    check(apply_spreadsheet_features(table_book, 0, table_features).changed, "PDF table source commits");
    apply_spreadsheet_formats(table_book, {{0, {1, 1}, {{"fill", "#FF8877"}}}});
    source.content = table_book;
    const auto table_pdf = export_file("table.pdf");
    const auto table_pixels = render_pdf_page(table_pdf.document, table_pdf.document.pages[0].id, 595, 842);
    check(table_pixels.error == PdfError::None, "PDFium renders table styles independently");
    if (table_pixels.error == PdfError::None)
    {
        const QImage image(
            table_pixels.rgba.data(), table_pixels.width, table_pixels.height, QImage::Format_RGBA8888);
        check(image.pixelColor(200, 60) == QColor("#345B89"), "table header fill reaches PDF");
        check(image.pixelColor(200, 85) == QColor("#FF8877"), "manual cell fill wins in PDF");
        check(
            image.pixelColor(200, 185) == QColor("#ABCDEF"), "empty trailing table rows are included in PDF");
    }
    source.content = std::make_shared<const PresentationScene>(make_presentation());
    auto slides = export_file("slides.pdf");
    check(slides.document.pages.size() == 1, "slide PDF page count");
    auto graph = make_free_mindmap("思维导图");
    source.content = graph;
    export_file("mindmap.pdf");
    source.content = plain.document;
    export_file("structured.pdf");
    options.compression = "screen";
    auto raster = export_file("compressed.pdf");
    check(raster.document.pages.size() == plain.document.pages.size(), "compression keeps pages");
    check(raster.document.pages[0].text.empty(), "raster mode explicitly loses searchable text");
    const auto image = render_pdf_page(raster.document, raster.document.pages[0].id, 300, 424);
    check(image.error == PdfError::None && !image.rgba.empty(), "compressed page renders");
    options.compression = "structure";
    PdfExportProgress cancelled;
    cancelled.cancelled = true;
    source.content = PdfTextSource{"cancel", false};
    check(render_pdf_export(source, options, theme, cancelled).error != PdfError::None, "cancel rendering");
    check(compress_pdf_document(plain.document, 96, 65,
              [](int, int)
    {
        return false;
    }).error != PdfError::None,
        "cancel compression");
    auto readonly = plain.document;
    readonly.editable = false;
    check(compress_pdf_document(readonly, 96, 65).error == PdfError::ReadOnly, "compression honors readonly");
    auto stamp = make_pdf_stamp("label", QSizeF(220, 60), {{"text", "可见标注 Stamp"}, {"size", 18}});
    check(stamp.error == PdfError::None, "create CJK vector text stamp");
    auto annotated = plain.document;
    PdfCommand command;
    command.kind = PdfCommandKind::AddAnnotation;
    command.page_id = annotated.pages[0].id;
    command.annotation.kind = PdfAnnotationKind::Stamp;
    command.annotation.rect = {100, 100, 220, 60};
    command.annotation.contents = "可见标注 Stamp";
    command.annotation.stamp_bytes = std::make_shared<const std::vector<std::uint8_t>>(stamp.bytes);
    check(apply_pdf_command(annotated, command).changed, "stamp uses public core edit transaction");
    source.content = annotated;
    auto stamped = export_file("stamped.pdf");
    check(stamped.document.pages[0].text.find("Stamp") != std::string::npos,
        "saved stamp is searchable vector page content");
    check(stamped.document.pages[0].text.find("可见标注") != std::string::npos,
        "saved stamp preserves CJK font");
    const auto stamped_render = render_pdf_page(stamped.document, stamped.document.pages[0].id, 300, 424);
    check(stamped_render.error == PdfError::None, "render flattened stamp");
    check(make_pdf_stamp("watermark", QSizeF(400, 250), {{"text", "内部资料"}, {"opacity", .2}}).error ==
            PdfError::None,
        "create translucent watermark");
    QImage picture(180, 120, QImage::Format_RGB32);
    picture.fill(QColor("#DB523C"));
    const auto picture_path = directory.filePath("stamp.png");
    check(picture.save(picture_path), "image fixture");
    auto image_stamp = make_pdf_stamp(
        "image", QSizeF(200, 100), {{"file", QUrl::fromLocalFile(picture_path).toString()}}, 1);
    check(image_stamp.error == PdfError::None && !image_stamp.bytes.empty(),
        "image stamp handles rotated page");
    command.annotation.stamp_bytes = std::make_shared<const std::vector<std::uint8_t>>(image_stamp.bytes);
    check(apply_pdf_command(annotated, command).changed, "add image stamp transaction");
    source.content = annotated;
    const auto illustrated = export_file("illustrated.pdf");
    const auto illustration =
        render_pdf_page(illustrated.document, illustrated.document.pages[0].id, 300, 424);
    int red_pixels = 0;
    for (std::size_t index = 0; index + 3 < illustration.rgba.size(); index += 4)
        if (illustration.rgba[index] > 160 && illustration.rgba[index + 1] < 130)
            ++red_pixels;
    check(red_pixels > 100, "saved image is visible in PDFium render");
    PdfExportBridge exporter(theme);
    source.content = PdfTextSource{"Export service", false};
    exporter.registerSource("text", [&source]()
    {
        return source;
    });
    const auto target = QUrl::fromLocalFile(directory.filePath("service.pdf"));
    check(exporter.start("text", target, {}), "start async public export");
    check(wait(exporter) && exporter.snapshot().value("success").toBool(), "async job completion snapshot");
    check(!exporter.start("text", target, {}), "overwrite is explicit");
    check(!exporter.start("text", target, {{"scope", "selection"}}), "module options validated");
    source.source_path = target.toLocalFile().toStdString();
    check(!exporter.start("text", target, {{"overwrite", true}}), "never overwrite active source");
    source.source_path = directory.filePath("working-copy.pdf").toStdString();
    source.protected_source_path = target.toLocalFile().toStdString();
    check(!exporter.start("text", target, {{"overwrite", true}}),
        "protect imported source after saving another copy");
    const auto inspected = inspect_pdf_destination(target.toLocalFile().toStdString());
    QFile conflict(target.toLocalFile());
    check(conflict.open(QIODevice::WriteOnly) && conflict.write("external") == 8,
        "external modification fixture");
    conflict.close();
    auto data = serialize_pdf_document(plain.document);
    check(save_pdf_bytes(inspected.path, data.bytes, inspected.revision).error == PdfError::ChangedOnDisk,
        "export detects changes after target inspection");
    std::cout << "PDF export failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_pdf_export_tests(argc, argv);
}
