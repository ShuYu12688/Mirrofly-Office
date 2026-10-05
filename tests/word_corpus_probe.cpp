#include "word_bridge.hpp"
#include "word_document.hpp"
#include "word_format_properties.hpp"
#include "word_image_resources.hpp"
#include <mirrorfly/word_storage.hpp>

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextTable>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <iostream>
#include <set>

namespace
{
    int check_formatting(const mirrorfly::WordDocument& source, QJsonObject report)
    {
        auto document = mirrorfly::create_word_document(source, 960, true);
        document->documentLayout()->documentSize();
        QCoreApplication::processEvents();
        QJsonArray elapsed;
        QJsonArray decorations_elapsed;
        bool valid = true;
        for (int iteration = 0; iteration < 12; ++iteration)
        {
            QElapsedTimer timer;
            timer.start();
            valid = mirrorfly::format_word_document(*document, 0, 1, "bold", iteration % 2 == 0) && valid;
            elapsed.append(timer.nsecsElapsed() / 1000000.0);
            timer.restart();
            const auto decorations = mirrorfly::word_paragraph_decorations(*document);
            decorations_elapsed.append(timer.nsecsElapsed() / 1000000.0);
            report.insert("decorations", decorations.size());
        }
        report.insert("formatMs", elapsed);
        report.insert("decorationMs", decorations_elapsed);
        report.insert("success", valid);
        std::cout << QJsonDocument(report).toJson().constData();
        return valid ? 0 : 1;
    }

    int check_async_images(const mirrorfly::WordDocument& document, QJsonObject report)
    {
        mirrorfly::WordImageResources resources(document.images);
        QElapsedTimer elapsed, request_timer;
        elapsed.start();
        qint64 last_tick = 0, largest_gap = 0, longest_request_ns = 0;
        int ticks = 0, decoded = 0;
        std::set<qulonglong> ready;
        QJsonArray failed;
        QObject::connect(&resources, &mirrorfly::WordImageResources::imageReady, &resources,
            [&](const QUrl& name)
        {
            ready.insert(name.path().mid(1).toULongLong());
        });
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&]()
        {
            const auto now = elapsed.elapsed();
            largest_gap = std::max(largest_gap, now - last_tick);
            last_tick = now;
            ++ticks;
        });
        heartbeat.start();
        // Two visible images per viewport; revisit every image without exceeding the pixel cache.
        for (std::size_t first = 0; first < document.images.size(); first += 2)
        {
            const auto end = std::min(document.images.size(), first + 2);
            for (std::size_t index = first; index < end; ++index)
            {
                const auto& source = document.images[index];
                const QUrl name(QStringLiteral("mirrorfly-word-image:/%1").arg(source.id));
                request_timer.start();
                const auto image = resources.request(name);
                longest_request_ns = std::max(longest_request_ns, request_timer.nsecsElapsed());
                if (!image.isNull() && image.size() != QSize(1, 1))
                    ready.insert(source.id);
            }
            bool complete = false;
            while (!complete && elapsed.elapsed() < 60000)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                complete = true;
                for (std::size_t index = first; index < end; ++index)
                    complete = complete && ready.count(document.images[index].id) != 0;
                QThread::msleep(1);
            }
            for (std::size_t index = first; index < end; ++index)
            {
                const auto& source = document.images[index];
                const QUrl name(QStringLiteral("mirrorfly-word-image:/%1").arg(source.id));
                if (complete && !resources.request(name).isNull())
                    ++decoded;
                else
                    failed.append(QString::fromStdString(source.path));
            }
        }
        heartbeat.stop();
        const auto image_wall_ms = elapsed.elapsed();
        // Compare pixels separately so synchronous reference work cannot distort the heartbeat benchmark.
        mirrorfly::WordImageResources reference(document.images);
        int matching = 0;
        for (const auto& source : document.images)
        {
            const QUrl name(QStringLiteral("mirrorfly-word-image:/%1").arg(source.id));
            const auto expected = reference.image(name);
            resources.request(name);
            QElapsedTimer timeout;
            timeout.start();
            while (resources.pending() && timeout.elapsed() < 10000)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(1);
            }
            if (!resources.pending() && !expected.isNull() && resources.request(name) == expected)
                ++matching;
            else
                failed.append(QString::fromStdString(source.path) + QStringLiteral(": pixel mismatch"));
        }
        report.insert("success", failed.isEmpty());
        report.insert("workerPixelsMatchingSynchronous", matching);
        report.insert("decodedImages", decoded);
        report.insert("failedImages", failed);
        report.insert("asyncImageWallMs", image_wall_ms);
        report.insert("longestResourceRequestMs", longest_request_ns / 1000000.0);
        report.insert("largestHeartbeatGapMs", largest_gap);
        report.insert("heartbeatTicks", ticks);
        report.insert("viewportImages", 2);
        std::cout << QJsonDocument(report).toJson().constData();
        return failed.isEmpty() ? 0 : 1;
    }

    int check_async_load(const QString& path, qint64 font_initialization_ms, int font_families)
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 960; height: 720; textFormat: "
                          "TextEdit.RichText; wrapMode: TextEdit.Wrap }",
            QUrl{});
        std::unique_ptr<QObject> item(component.create());
        if (!item)
            return 2;
        auto* wrapper = item->property("textDocument").value<QQuickTextDocument*>();
        mirrorfly::WordBridge bridge;
        bridge.loadEditor(wrapper);
        qreal previous_progress = 0;
        int progress_events = 0;
        bool monotonic = true, ready = false;
        QObject::connect(&bridge, &mirrorfly::WordBridge::loadingChanged, &bridge, [&]()
        {
            monotonic = monotonic && bridge.loadingProgress() >= previous_progress;
            previous_progress = bridge.loadingProgress();
            ++progress_events;
        });
        QObject::connect(&bridge, &mirrorfly::WordBridge::loadReady, &bridge, [&]()
        {
            ready = true;
        });
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 last_tick = 0, largest_gap = 0;
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&]()
        {
            const auto now = elapsed.elapsed();
            largest_gap = std::max(largest_gap, now - last_tick);
            last_tick = now;
            ++ticks;
        });
        heartbeat.start();
        const bool requested = bridge.requestOpen(QUrl::fromLocalFile(path));
        while (bridge.locked() && elapsed.elapsed() < 60000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(1);
        }
        heartbeat.stop();
        const auto preparation = elapsed.elapsed();
        const bool loaded = requested && !bridge.locked() && bridge.active();
        const bool gated_before_attach = !ready && bridge.loadingProgress() < 1;
        elapsed.restart();
        if (loaded)
        {
            bridge.loadEditor(wrapper);
            QCoreApplication::processEvents();
        }
        const auto attach_ms = elapsed.nsecsElapsed() / 1000000.0;
        auto* resources = wrapper->textDocument()->findChild<mirrorfly::WordImageResources*>();
        std::set<QString> names;
        int missing_previews = 0;
        for (auto block = wrapper->textDocument()->begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto format = it.fragment().charFormat();
                if (!format.isImageFormat())
                    continue;
                const auto name = format.toImageFormat().name();
                if (names.insert(name).second && (!resources || resources->request(QUrl(name)).isNull()))
                    ++missing_previews;
            }
        const bool success = loaded && gated_before_attach && ready && monotonic && missing_previews == 0 &&
            resources && !resources->pending() && bridge.loadingProgress() == 1;
        const QJsonObject report{{"success", success}, {"prepareMs", preparation}, {"attachMs", attach_ms},
            {"heartbeatTicks", ticks}, {"progressEvents", progress_events}, {"monotonicProgress", monotonic},
            {"gatedBeforeAttachment", gated_before_attach},
            {"preparedImages", static_cast<int>(names.size())}, {"missingPreviews", missing_previews},
            {"preparedBytes", resources ? resources->preparedBytes() : 0},
            {"largestHeartbeatGapMs", largest_gap}, {"message", bridge.message()},
            {"platform", QGuiApplication::platformName()}, {"fontFamilies", font_families},
            {"fontInitializationMs", font_initialization_ms},
            {"sameThread", loaded && wrapper->textDocument()->thread() == QThread::currentThread()}};
        std::cout << QJsonDocument(report).toJson().constData();
        QThreadPool::globalInstance()->waitForDone();
        return success ? 0 : 1;
    }
}

int run_word_corpus_probe(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    const auto arguments = application.arguments();
    QElapsedTimer font_timer;
    font_timer.start();
    const auto font_families = QFontDatabase::families().size();
    const auto font_initialization_ms = font_timer.elapsed();
    if (arguments.size() == 3 && font_families == 0)
    {
        std::cerr << "The selected Qt platform exposes no system fonts; use QT_QPA_PLATFORM=windows "
                     "for windowless corpus rendering and timing on Windows.\n";
        return 2;
    }
    if (arguments.size() == 3 && arguments[2] == "--async")
        return check_async_load(arguments[1], font_initialization_ms, static_cast<int>(font_families));
    if (arguments.size() < 2 || arguments.size() > 3)
    {
        std::cerr << "Usage: word_corpus_probe input.docx "
                     "[render-directory|--async|--async-images|--geometry|--format]\n";
        return 2;
    }
    QElapsedTimer timer;
    timer.start();
    auto result = mirrorfly::load_word_file(arguments[1].toStdString());
    const auto load_ms = timer.elapsed();
    QJsonObject report{{"success", result.success}, {"error", QString::fromStdString(result.error)},
        {"platform", QGuiApplication::platformName()}, {"fontFamilies", font_families},
        {"fontInitializationMs", font_initialization_ms}, {"loadMs", load_ms},
        {"paragraphs", static_cast<int>(result.document.paragraphs.size())},
        {"tables", static_cast<int>(result.document.tables.size())},
        {"images", static_cast<int>(result.document.images.size())},
        {"sections", static_cast<int>(result.document.sections.size())}};
    if (result.success)
    {
        if (arguments.size() == 3 && arguments[2] == "--format")
            return check_formatting(result.document, report);
        if (arguments.size() == 3 && arguments[2] == "--geometry")
        {
            QJsonArray images;
            for (const auto& image : result.document.images)
                images.append(QJsonObject{{"id", static_cast<qint64>(image.id)},
                    {"path", QString::fromStdString(image.path)}, {"width", image.width},
                    {"height", image.height}});
            report.insert("imageGeometry", images);
            std::cout << QJsonDocument(report).toJson().constData();
            return 0;
        }
        if (arguments.size() == 3 && arguments[2] == "--async-images")
            return check_async_images(result.document, report);
        timer.restart();
        const auto serialized = mirrorfly::serialize_word(result.document);
        report.insert("serializeMs", timer.elapsed());
        report.insert("serializeSuccess", serialized.success);
        report.insert("serializeError", QString::fromStdString(serialized.error));
        report.insert("preservedParts", static_cast<int>(serialized.parts.size()));
        result.success = serialized.success;
        if (arguments.size() == 3)
        {
            timer.restart();
            auto document = mirrorfly::create_word_document(result.document, 960);
            report.insert("createEditorMs", timer.elapsed());
            timer.restart();
            document->setTextWidth(960);
            const auto size = document->documentLayout()->documentSize();
            report.insert("layoutMs", timer.elapsed());
            report.insert("height", size.height());
            const QDir output(arguments[2]);
            QDir().mkpath(output.absolutePath());
            const auto roundtrip =
                mirrorfly::save_word_file(output.filePath("roundtrip.docx").toStdString(), result.document);
            report.insert("roundtripSaved", roundtrip.success);
            report.insert("roundtripError", QString::fromStdString(roundtrip.error));
            result.success = result.success && roundtrip.success;
            QJsonArray paragraphs;
            for (const auto& paragraph : result.document.paragraphs)
            {
                QString text;
                for (const auto& run : paragraph.runs)
                    text += QString::fromStdString(run.text);
                paragraphs.append(text);
            }
            report.insert("paragraphText", paragraphs);
            QImage page(960, 1357, QImage::Format_ARGB32_Premultiplied);
            page.fill(Qt::white);
            timer.restart();
            QPainter painter(&page);
            QAbstractTextDocumentLayout::PaintContext context;
            context.clip = page.rect();
            document->documentLayout()->draw(&painter, context);
            painter.end();
            report.insert("firstPaintMs", timer.elapsed());
            report.insert("pngSaved", page.save(output.filePath("first-page.png")));
            QJsonArray style_samples;
            bool body_sample = false, table_sample = false;
            for (auto block = document->begin(); block.isValid(); block = block.next())
            {
                const auto id =
                    block.blockFormat().property(mirrorfly::word_source_paragraph_property).toULongLong();
                if (!id || id > result.document.paragraphs.size())
                    continue;
                const bool in_table = QTextCursor(block).currentTable() != nullptr;
                if ((in_table ? table_sample : body_sample) ||
                    block.text().trimmed().size() < (in_table ? 2 : 60) ||
                    block.text().contains(QChar::ObjectReplacementCharacter))
                    continue;
                if (in_table)
                    table_sample = true;
                else
                    body_sample = true;
                const auto& source = result.document.paragraphs[id - 1];
                const auto top =
                    std::max(0.0, document->documentLayout()->blockBoundingRect(block).top() - 20);
                page.fill(Qt::white);
                QPainter sample_painter(&page);
                sample_painter.translate(0, -top);
                context.clip = QRectF(0, top, page.width(), page.height());
                document->documentLayout()->draw(&sample_painter, context);
                sample_painter.end();
                const auto file = in_table ? "table-region.png" : "body-region.png";
                QJsonObject sample{{"sourceParagraph", static_cast<qint64>(id)}, {"tableCell", in_table},
                    {"pngSaved", page.save(output.filePath(file))}, {"alignment", source.alignment},
                    {"lineSpacing", source.line_spacing}, {"viewportTop", top}};
                if (!source.runs.empty())
                {
                    sample.insert("font", QString::fromStdString(source.runs.front().font));
                    sample.insert("eastAsiaFont", QString::fromStdString(source.runs.front().east_asia_font));
                    sample.insert("fontSize", source.runs.front().size);
                }
                style_samples.append(sample);
                if (body_sample && table_sample)
                    break;
            }
            report.insert("styleSamples", style_samples);
            const auto extracted = mirrorfly::extract_word_document(*document);
            report.insert("extractSuccess", extracted.success);
            report.insert("extractError", QString::fromStdString(extracted.error));
            if (extracted.success)
            {
                std::set<std::size_t> owned;
                const auto collect = [&owned](const std::vector<mirrorfly::WordBlock>& blocks)
                {
                    for (const auto& block : blocks)
                        if (block.kind == mirrorfly::WordBlock::Kind::Paragraph)
                            owned.insert(block.index);
                };
                collect(extracted.document.blocks);
                for (const auto& table : extracted.document.tables)
                    for (const auto& cell : table.cells)
                        collect(cell.blocks);
                QJsonArray unowned;
                for (std::size_t index = 0; index < extracted.document.paragraphs.size(); ++index)
                    if (!owned.count(index))
                    {
                        const auto& paragraph = extracted.document.paragraphs[index];
                        unowned.append(QJsonObject{{"index", static_cast<qint64>(index)},
                            {"sourceId", static_cast<qint64>(paragraph.source_id)},
                            {"runs", static_cast<qint64>(paragraph.runs.size())}});
                    }
                report.insert("unownedExtractedParagraphs", unowned);
                QJsonArray synthetic;
                for (auto block = document->begin(); block.isValid(); block = block.next())
                {
                    if (block.blockFormat().property(mirrorfly::word_source_paragraph_property).toULongLong())
                        continue;
                    const auto* table = QTextCursor(block).currentTable();
                    const auto cell = table ? table->cellAt(block.position()) : QTextTableCell{};
                    synthetic.append(QJsonObject{{"position", block.position()}, {"length", block.length()},
                        {"table",
                            table ? table->format().property(mirrorfly::word_source_table_property).toInt()
                                  : 0},
                        {"row", cell.isValid() ? cell.row() : -1},
                        {"column", cell.isValid() ? cell.column() : -1}});
                }
                report.insert("syntheticBlocks", synthetic);
                const auto editor_saved = mirrorfly::serialize_word(extracted.document);
                report.insert("editorSerializeSuccess", editor_saved.success);
                report.insert("editorSerializeError", QString::fromStdString(editor_saved.error));
                result.success = result.success && editor_saved.success;
                bool identical = editor_saved.parts.size() == serialized.parts.size();
                for (std::size_t index = 0; identical && index < serialized.parts.size(); ++index)
                    identical = editor_saved.parts[index].path == serialized.parts[index].path &&
                        editor_saved.parts[index].bytes == serialized.parts[index].bytes;
                report.insert("editorUnchangedPartsIdentical", identical);
                result.success = result.success && identical;
                if (!identical)
                {
                    auto before = result.document;
                    auto after = extracted.document;
                    before.source_package.reset();
                    after.source_package.reset();
                    const auto original = mirrorfly::serialize_word(before);
                    const auto converted = mirrorfly::serialize_word(after);
                    if (original.success && converted.success)
                    {
                        const auto a = QString::fromStdString(original.parts.back().bytes).split("</w:p>");
                        const auto b = QString::fromStdString(converted.parts.back().bytes).split("</w:p>");
                        QJsonArray differences;
                        for (qsizetype index = 0;
                            index < a.size() && index < b.size() && differences.size() < 3; ++index)
                            if (a[index] != b[index])
                                differences.append(QJsonObject{{"paragraph", static_cast<int>(index + 1)},
                                    {"before", a[index].left(1600)}, {"after", b[index].left(1600)}});
                        report.insert("normalizationDifferences", differences);
                    }
                }
            }
            else
                result.success = false;
            mirrorfly::WordImageResources resources(result.document.images);
            QJsonArray failed_images;
            timer.restart();
            int decoded = 0;
            for (const auto& source : result.document.images)
            {
                const auto image =
                    resources.image(QUrl(QStringLiteral("mirrorfly-word-image:/%1").arg(source.id)));
                if (image.isNull())
                    failed_images.append(QString::fromStdString(source.path));
                else
                {
                    ++decoded;
                    if (source.mime_type == "image/x-emf" && decoded <= 5)
                        image.save(output.filePath(QStringLiteral("image-%1.png").arg(source.id)));
                }
            }
            report.insert("decodedImages", decoded);
            report.insert("imageDecodeMs", timer.elapsed());
            report.insert("failedImages", failed_images);
            result.success = result.success && failed_images.isEmpty();
            QJsonArray unmapped;
            for (auto block = document->begin(); block.isValid() && unmapped.size() < 6; block = block.next())
                if (!block.blockFormat().property(mirrorfly::word_source_paragraph_property).toULongLong() &&
                    !block.text().isEmpty())
                    unmapped.append(
                        QJsonObject{{"position", block.position()}, {"text", block.text().left(80)}});
            report.insert("unmapped", unmapped);
            timer.restart();
            const auto decorations = mirrorfly::word_paragraph_decorations(*document);
            report.insert("decorationMs", timer.elapsed());
            report.insert("decorations", decorations.size());
            for (auto block = document->begin(); block.isValid(); block = block.next())
            {
                if (block.text().trimmed().size() < 10 ||
                    block.text().contains(QChar::ObjectReplacementCharacter))
                    continue;
                QTextCursor edit(document.get());
                edit.setPosition(block.position() + block.length() - 1);
                edit.insertText(QStringLiteral(" [Mirrorfly verification]"));
                const auto candidate = mirrorfly::extract_word_document(*document);
                const auto saved = candidate.success
                    ? mirrorfly::save_word_file(
                          output.filePath("edited-check.docx").toStdString(), candidate.document)
                    : candidate;
                report.insert("editedSaveSuccess", saved.success);
                report.insert("editedSaveError", QString::fromStdString(saved.error));
                result.success = result.success && saved.success;
                if (saved.success)
                {
                    const auto reopened =
                        mirrorfly::load_word_file(output.filePath("edited-check.docx").toStdString());
                    const bool stable = reopened.success &&
                        reopened.document.images.size() == result.document.images.size() &&
                        reopened.document.tables.size() == result.document.tables.size() &&
                        reopened.document.sections.size() == result.document.sections.size();
                    report.insert("editedReopenStructure", stable);
                    result.success = result.success && stable;
                }
                break;
            }
            document->undo();
            QJsonArray structural_checks;
            for (const bool table_cell : {false, true})
            {
                bool checked = false;
                for (auto block = document->begin(); block.isValid(); block = block.next())
                {
                    const auto id =
                        block.blockFormat().property(mirrorfly::word_source_paragraph_property).toULongLong();
                    QTextCursor edit(block);
                    if (bool(edit.currentTable()) != table_cell || block.text().size() < 6 || !id ||
                        !mirrorfly::word_paragraph_capabilities(result.document, id - 1).split)
                        continue;
                    const auto name = table_cell ? QStringLiteral("cell-split-check.docx")
                                                 : QStringLiteral("body-split-check.docx");
                    edit.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 3);
                    timer.restart();
                    edit.insertBlock();
                    const auto candidate = mirrorfly::extract_word_document(*document);
                    const auto prepare_ms = timer.elapsed();
                    const auto saved = candidate.success
                        ? mirrorfly::save_word_file(output.filePath(name).toStdString(), candidate.document)
                        : candidate;
                    auto reopened = saved.success
                        ? mirrorfly::load_word_file(output.filePath(name).toStdString())
                        : saved;
                    const auto body_text = [](const auto& source)
                    {
                        std::string value;
                        for (const auto& paragraph : source.paragraphs)
                            for (const auto& run : paragraph.runs)
                                value += run.text;
                        return value;
                    };
                    const bool valid = reopened.success &&
                        reopened.document.paragraphs.size() == result.document.paragraphs.size() + 1 &&
                        reopened.document.images.size() == result.document.images.size() &&
                        reopened.document.tables.size() == result.document.tables.size() &&
                        reopened.document.sections.size() == result.document.sections.size() &&
                        body_text(reopened.document) == body_text(result.document);
                    structural_checks.append(QJsonObject{{"tableCell", table_cell},
                        {"sourceParagraph", static_cast<int>(id)}, {"prepareMs", prepare_ms},
                        {"success", valid}, {"error", QString::fromStdString(saved.error)}});
                    result.success = result.success && valid;
                    document->undo();
                    checked = true;
                    break;
                }
                if (!checked)
                    structural_checks.append(
                        QJsonObject{{"tableCell", table_cell}, {"skipped", "no eligible paragraph"}});
            }
            report.insert("structuralChecks", structural_checks);
        }
    }
    report.insert("success", result.success);
    std::cout << QJsonDocument(report).toJson().constData();
    return result.success ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_word_corpus_probe(argc, argv);
}
