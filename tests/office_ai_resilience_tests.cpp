#include "automation_bridge.hpp"
#include "canvas_bridge.hpp"
#include "document_read.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_page_layout.hpp"
#include "office_ai_stream.hpp"
#include "office_ai_structured.hpp"
#include "office_ai_style.hpp"
#include "office_ai_test_transport.hpp"
#include "office_ai_toolbox.hpp"
#include "presentation_bridge.hpp"
#include "presentation_image_export.hpp"

#include <mirrorfly/mindmap_storage.hpp>
#include <mirrorfly/presentation_storage.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

namespace
{
    using namespace mirrorfly;
    using namespace office_ai_test;
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }

    bool wait_for(const std::function<bool()>& done, int timeout = 20000)
    {
        QElapsedTimer clock;
        clock.start();
        while (!done() && clock.elapsed() < timeout)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return done();
    }

    class Root final : public QObject
    {
        Q_OBJECT
    public:
        QString module;
        Q_INVOKABLE bool automationReady() const
        {
            return true;
        }
        Q_INVOKABLE QVariantMap automationState() const
        {
            return {{"module", module}, {"pendingInput", false}};
        }
    };

    QJsonObject execute(OfficeAiToolbox& box, const QString& name, const QJsonObject& args)
    {
        box.beginResponse(runtime().value("revision").toString(), 1);
        bool finished = false;
        QJsonObject result;
        box.execute(name, args, [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        check(wait_for(
                  [&]()
        {
            return finished;
        }),
            "tool completes without an application window");
        if (!result.value("ok").toBool())
            std::cout << "Tool result: " << json(result).toStdString() << '\n';
        return result;
    }

    QJsonObject pages(const QString& id, int first = 0)
    {
        QJsonArray result;
        for (int index = first; index < first + 4; ++index)
            result.append(QJsonObject{{"title", QStringLiteral("Page %1").arg(index + 1)}, {"layout", "grid"},
                {"blocks",
                    QJsonArray{
                        QJsonObject{{"heading", "Learn"}, {"text", "A concise complete explanation."}}}}});
        return {{"batchId", id}, {"targetPages", 12}, {"pages", result}};
    }

    QJsonArray add_node(const QString& id);

    void test_pages(const QString& directory)
    {
        PresentationBridge slides;
        slides.requestNew();
        Root root;
        root.module = "slides";
        AutomationBridge automation;
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        OfficeAiToolbox box(directory);
        box.reset();
        box.query("office_load_group", {{"group", "slides"}});
        box.focusWorkspace();
        check(box.groups().contains("compose"), "new blank presentation loads bulk creation automatically");
        const auto index = box.query("office_schema", {{"module", "slides"}, {"name", ""}});
        check(!index.contains("metadata") && index.value("topics").toArray().contains("help:speakerNotes") &&
                !index.value("names").toArray().contains("scope"),
            "schema index separates short discovery names from detailed explanatory topics");
        check(box.query("office_schema", {{"module", "slides"}, {"name", "help:speakerNotes"}})
                    .value("description")
                    .isString() &&
                box.query("office_schema", {{"module", "slides"}, {"name", "speakerNotes"}})
                    .value("parameters")
                    .isArray(),
            "help topics remain readable even when their names collide with public methods");
        const auto format_schema = box.query("office_schema", {{"module", "slides"}, {"name", "formatText"}});
        const auto format_invocation = format_schema.value("invocation").toObject();
        check(format_invocation.value("args").toObject().value("action") == "formatText" &&
                format_invocation.value("args").toObject().contains("options"),
            "exact edit schema uses public named action/options instead of ambiguous flattened fields");
        check(format_schema.value("scope").toString().contains("wholeObject") &&
                format_schema.value("selection").toString().contains("slides.selectObject") &&
                !format_schema.value("options").toObject().contains("range"),
            "exact slide edit schema includes selection and scope without loading the full catalog");
        const auto original = slides.editGeneration();
        const auto catalog = box.query("office_style", {});
        check(catalog.value("ok").toBool() && catalog.value("styles").toArray().size() == 4 &&
                box.query("office_style", {{"styleId", "unknown"}}).value("error") == "unknown_style" &&
                execute(box, "office_image_fetch", {{"id", "invented"}}).value("error") == "unknown_image",
            "style and image tools expose bounded discoverable contracts");
        check(!slides.applyEdit("addText", {{"text", "must not exist"}, {"unexpected", 1}}) &&
                !slides.applyEdit("addText", {{"text", "must not exist"}, {"width", "wide"}}) &&
                slides.editGeneration() == original,
            "unknown options and wrong numeric types cannot silently apply partial defaults");
        auto oversized = pages("oversized");
        auto too_many = oversized.value("pages").toArray();
        while (too_many.size() < 9)
            too_many.append(too_many.first());
        oversized.insert("pages", too_many);
        const auto batch_error = execute(box, "office_compose_slides", oversized);
        check(batch_error.value("error") == "invalid_page_batch_size" && batch_error.value("maximum") == 8 &&
                slides.editGeneration() == original,
            "oversized call reports batch size rather than misleading document precondition");
        auto no_target = pages("four");
        no_target.remove("targetPages");
        const auto missing_target = execute(box, "office_compose_slides", no_target);
        check(missing_target.value("error") == "deck_pages_required" && slides.editGeneration() == original,
            "model must declare the full intended deck length before the first page batch");
        auto short_target = pages("four");
        short_target.insert("targetPages", 2);
        const auto invalid_target = execute(box, "office_compose_slides", short_target);
        check(
            invalid_target.value("error") == "deck_page_plan_mismatch" && slides.editGeneration() == original,
            "model cannot declare fewer total pages than the first proposed segment");
        auto invalid_theme = pages("four");
        invalid_theme.insert("theme", QJsonObject{{"ink", QStringLiteral("#2E1F₁")}});
        const auto rejected_theme = execute(box, "office_compose_slides", invalid_theme);
        check(rejected_theme.value("error") == "invalid_theme" &&
                rejected_theme.value("field") == "theme.ink" && slides.editGeneration() == original,
            "model's non-ASCII theme digit gets a targeted error before editing");
        auto invalid_style = pages("four");
        invalid_style.insert("styleId", "made-up");
        check(execute(box, "office_compose_slides", invalid_style).value("error") == "invalid_style" &&
                slides.editGeneration() == original,
            "unknown visual styles are rejected before editing any page");
        auto invalid = pages("four");
        auto input = invalid.value("pages").toArray();
        auto last = input.last().toObject();
        last.insert("layout", "comparison");
        input[3] = last;
        invalid.insert("pages", input);
        const auto wrong_count = execute(box, "office_compose_slides", invalid);
        check(wrong_count.value("error") == "invalid_page" && wrong_count.value("field") == "blocks" &&
                wrong_count.value("pageIndex") == 3 && slides.editGeneration() == original,
            "a model page with the wrong block count identifies its page and changes nothing");
        last.insert("layout", "grid");
        last.insert("blocks", QJsonArray{QJsonObject{{"text", QString(601, 'x')}}});
        input[3] = last;
        invalid.insert("pages", input);
        const auto wrong_text = execute(box, "office_compose_slides", invalid);
        check(wrong_text.value("error") == "invalid_block" && wrong_text.value("blockIndex") == 0 &&
                wrong_text.value("pageIndex") == 3 && slides.editGeneration() == original,
            "model retry identifies an invalid content block without applying preceding pages");
        const auto result = execute(box, "office_compose_slides", pages("four"));
        check(result.value("complete").toBool() && result.value("pages").toArray().size() == 4 &&
                slides.slideCount() == 4 &&
                result.value("deckProgress").toObject().value("verifiedPages") == 4 &&
                !result.value("deckProgress").toObject().value("complete").toBool(),
            "one structured tool creates four verified pages");
        const auto timing = result.value("timing").toObject();
        check(timing.value("snapshots").toInt() >= result.value("executed").toInt() * 2 &&
                timing.contains("executeMs") && timing.contains("snapshotMs") && timing.contains("totalMs") &&
                timing.value("fallbackWakes").toInt() == 0,
            "page receipts retain execution and snapshot costs without polling synchronous edits");
        auto changed_style = pages("new-style", 4);
        changed_style.insert("styleId", "natural");
        const auto style_error = execute(box, "office_compose_slides", changed_style);
        check(style_error.value("error") == "invalid_style" &&
                style_error.value("expectedStyleId") == "modern" && slides.slideCount() == 4,
            "later batches cannot silently switch the deck's visual style");
        auto revised_target = pages("changed-plan", 4);
        revised_target.insert("targetPages", 8);
        const auto rejected_plan = execute(box, "office_compose_slides", revised_target);
        check(rejected_plan.value("error") == "deck_page_plan_mismatch" && slides.slideCount() == 4,
            "later model segment cannot silently reduce the original deck target");
        const auto overview = box.query("office_read", {{"module", "slides"}, {"view", "overview"}});
        check(overview.value("items").toArray().first().toObject().value("title") == "Page 1",
            "overview exposes the real title instead of the generic slide label");
        const auto repeated = execute(box, "office_compose_slides", pages("four"));
        check(repeated.value("alreadyApplied").toBool() && slides.slideCount() == 4,
            "same batch ID is idempotent after a lost acknowledgement");
        check(repeated.value("timing") == result.value("timing"),
            "replaying a completed receipt does not count its timing twice");
        int edits = 0;
        bool interrupted = false;
        const auto connection = QObject::connect(&box, &OfficeAiToolbox::actionFinished, &box,
            [&](const QString&, const QString&, const QJsonArray&, const QJsonObject&, qint64)
        {
            if (++edits == 12)
                QTimer::singleShot(0, &box, [&]()
                {
                    box.cancel();
                    interrupted = true;
                });
        });
        box.beginResponse(runtime().value("revision").toString(), 2);
        box.execute("office_compose_slides", pages("interrupted", 4), [](const QJsonObject&)
        {
        });
        check(wait_for(
                  [&]()
        {
            return interrupted;
        }),
            "page batch yields between edits for cancellation");
        QObject::disconnect(connection);
        const auto resumed = execute(box, "office_continue_slides", {{"batchId", "interrupted"}});
        check(resumed.value("complete").toBool() && slides.slideCount() == 8,
            "cancelled page batch resumes its exact remaining steps without duplicate pages");
        check(resumed.value("deckProgress").toObject().value("verifiedPages") == 8 &&
                !resumed.value("deckProgress").toObject().value("complete").toBool(),
            "resumed second segment still reports unfinished full-deck coverage");
        for (int page = 0; page < 8; ++page)
            check(slides.semanticPage(page, 0, 1).value("totalObjects").toInt() == 8,
                "all eight pages retain complete layout after resume");
        check(slides.applyEdit("addText", {{"text", "External edit"}}), "simulate user edit");
        const auto changed = execute(box, "office_compose_slides", pages("four"));
        check(changed.value("error") == "completed_batch_changed" && !changed.value("complete").toBool() &&
                changed.value("historicalReceipt").toBool(),
            "old receipts cannot certify externally changed content");
        const auto saved = execute(box, "office_save", {{"title", "Four-page resume"}});
        const auto file = saved.value("file").toObject().value("path").toString();
        const auto reopened = load_presentation_file(file.toStdString());
        check(reopened.error == PresentationError::None && reopened.scene.slides.size() == 8 &&
                find_presentation_text(reopened.scene, "Page 8").size() == 1,
            "resumed pages independently survive save and native PPTX readback");
        for (int page = 0; page < 8; ++page)
            check(
                find_presentation_text(reopened.scene, QStringLiteral("Page %1").arg(page + 1).toStdString())
                        .size() == 1,
                "each intended slide title survives native readback");
        QFile source_file(file);
        check(source_file.open(QIODevice::ReadOnly), "saved source can be read before conversion");
        const auto source_hash = QCryptographicHash::hash(source_file.readAll(), QCryptographicHash::Sha256);
        source_file.close();
        check(slides.requestOpen(QUrl::fromLocalFile(file)) &&
                wait_for(
                    [&]()
        {
            return !slides.busy();
        }) && !slides.editable(),
            "native PPT reopens read-only");
        box.reset();
        box.query("office_load_group", {{"group", "slides"}});
        for (const auto& arguments :
            std::vector<QJsonObject>{{{"destination", file}}, {{"destination", "relative.pptx"}},
                {{"destination", true}}, {{"destination", QDir(directory).filePath("wrong.docx")}},
                {{"destination", QDir(directory).filePath("copy.pptx")}, {"title", "Ambiguous"}}})
            check(execute(box, "office_editable_copy", arguments).value("error") ==
                        "invalid_copy_destination" &&
                    !slides.editable(),
                "copy rejects existing, relative, wrong-type, wrong-extension or ambiguous destination");
        const auto copied = execute(box, "office_editable_copy", {{"title", "Recovered editable deck"}});
        const auto copy_path = copied.value("file").toObject().value("path").toString();
        check(copied.value("ok").toBool() && QFileInfo::exists(copy_path) && slides.editable() &&
                slides.documentPath() == copy_path,
            "AI public workflow creates an editable PPT copy without a dialog");
        const auto object = slides.readContent({{"view", "content"}, {"index", 0}, {"limit", 1}})
                                .value("items")
                                .toList()
                                .value(0)
                                .toMap();
        const auto generation = slides.editGeneration();
        const auto selection = slides.selection();
        const auto format = box.query("office_read",
            {{"module", "slides"}, {"view", "format"}, {"index", 0}, {"id", object.value("id").toString()}});
        check(format.value("ok").toBool() && format.value("format").toObject().contains("fontSize") &&
                format.value("scope").toString().contains("first text run") &&
                slides.editGeneration() == generation && slides.selection() == selection,
            "targeted slide format read uses shared semantic style without changing selection or generation");
        check(slides.readContent({{"view", "format"}, {"id", "unknown-object"}}).value("error") ==
                "object_not_found",
            "format read refuses unknown object IDs instead of falling back to the selected object");
        QFile unchanged(file);
        check(unchanged.open(QIODevice::ReadOnly) &&
                QCryptographicHash::hash(unchanged.readAll(), QCryptographicHash::Sha256) == source_hash,
            "AI conversion leaves the original PPT byte for byte intact");
        check(slides.requestOpen(QUrl::fromLocalFile(file)) &&
                wait_for(
                    [&]()
        {
            return !slides.busy();
        }),
            "reopen source to exercise an explicit editable copy destination");
        box.reset();
        box.query("office_load_group", {{"group", "slides"}});
        box.focusWorkspace();
        check(!box.groups().contains("compose"), "opening an existing presentation omits bulk recipes");
        check(box.query("office_load_group", {{"group", "compose"}}).value("ok").toBool() &&
                box.groups().contains("compose"),
            "model can explicitly load creation for appending to a file");
        const QString explicit_path = QDir(directory).filePath("explicit-copy.pptx");
        const auto explicit_copy = execute(box, "office_editable_copy", {{"destination", explicit_path}});
        check(explicit_copy.value("ok").toBool() && slides.documentPath() == explicit_path &&
                explicit_copy.value("file").toObject().value("path").toString() == explicit_path,
            "explicit copy creates and activates exactly the requested output path");
        const auto appended = execute(box, "office_compose_slides", pages("after-copy", 8));
        if (!appended.value("complete").toBool())
            std::cerr << "Append: " << json(appended).toStdString() << '\n';
        check(appended.value("complete").toBool() && slides.slideCount() == 12,
            "AI appends another four-page segment to a reopened editable deck");
        check(appended.value("deckProgress").toObject().value("complete").toBool() &&
                appended.value("deckProgress").toObject().value("verifiedPages") == 12 &&
                appended.value("next").toString().contains("Do not append pages"),
            "full target becomes complete only after all twelve pages are verified");
        auto extra_pages = pages("already-complete", 12);
        auto extra_input = extra_pages.value("pages").toArray();
        extra_input = QJsonArray{extra_input.first()};
        extra_pages.insert("pages", extra_input);
        const auto rejected_extra = execute(box, "office_compose_slides", extra_pages);
        check(rejected_extra.value("error") == "deck_already_complete" &&
                rejected_extra.value("targetPages") == 12 && rejected_extra.value("currentPages") == 12 &&
                rejected_extra.value("remainingPages") == 0 && slides.slideCount() == 12,
            "model gets a precise save-or-edit response after the planned deck is complete");
        const auto emoji = QString::fromUtf8("A\xF0\x9F\x98\x80Z");
        check(read_text(emoji, 1, 1).value("text").toString().size() == 2 &&
                !read_text(emoji, 2, 1).value("ok").toBool(),
            "UTF-16 paging never splits a surrogate pair");
    }

    void test_human_page_updates(const QString& directory)
    {
        PresentationBridge slides;
        slides.requestNew();
        Root root;
        root.module = "slides";
        AutomationBridge automation;
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        OfficeAiToolbox box(directory);
        box.reset();
        box.setRequestedSlidePages(3);
        box.query("office_load_group", {{"group", "slides"}});
        auto first = pages("human-first");
        auto first_pages = first.value("pages").toArray();
        first_pages.removeLast();
        first.insert("pages", first_pages);
        first.insert("targetPages", 3);
        check(execute(box, "office_compose_slides", first).value("complete").toBool() &&
                slides.slideCount() == 3,
            "initial explicit human goal creates exactly three actual pages");
        const auto generation = slides.editGeneration();
        box.setRequestedSlidePages(5);
        const auto historical = execute(box, "office_compose_slides", first);
        check(historical.value("alreadyApplied").toBool() && historical.value("historicalReceipt").toBool() &&
                !historical.value("complete").toBool() && historical.value("humanTargetPages") == 5 &&
                slides.slideCount() == 3 && slides.editGeneration() == generation,
            "3-to-5 human expansion retains batch idempotency without claiming the old goal complete");
        auto extension = pages("human-extension", 3);
        auto extension_pages = extension.value("pages").toArray();
        extension_pages.removeLast();
        extension_pages.removeLast();
        extension.insert("pages", extension_pages);
        extension.insert("targetPages", 5);
        bool interrupted = false;
        int edits = 0;
        const auto connection = QObject::connect(&box, &OfficeAiToolbox::actionFinished, &box,
            [&](const QString&, const QString&, const QJsonArray&, const QJsonObject&, qint64)
        {
            if (++edits == 12)
                QTimer::singleShot(0, &box, [&]()
                {
                    box.cancel();
                    interrupted = true;
                });
        });
        box.beginResponse(runtime().value("revision").toString(), 1);
        box.execute("office_compose_slides", extension, [](const QJsonObject&)
        {
        });
        check(wait_for(
                  [&]()
        {
            return interrupted;
        }),
            "human expansion can pause a partial page batch");
        QObject::disconnect(connection);
        const auto resumed = execute(box, "office_continue_slides", {{"batchId", "human-extension"}});
        check(resumed.value("complete").toBool() && slides.slideCount() == 5 &&
                resumed.value("deckProgress").toObject().value("targetPages") == 5,
            "expansion resumes only remaining batch steps and keeps the existing three pages");
        const auto before_shrink = slides.editGeneration();
        box.setRequestedSlidePages(3);
        check(slides.slideCount() == 5 && slides.editGeneration() == before_shrink,
            "5-to-3 human goal change does not delete pages in the constraint setter");
        for (int page = 4; page >= 3; --page)
        {
            slides.setSlide(page);
            check(slides.applyEdit("deleteSlide", {}), "explicit edit removes only the excess current page");
            check(wait_for(
                      [&]()
            {
                return !slides.syncing();
            }),
                "page removal settles before next edit");
        }
        const auto replayed = execute(box, "office_compose_slides", extension);
        check(slides.slideCount() == 3 && replayed.value("alreadyApplied").toBool() &&
                replayed.value("historicalReceipt").toBool(),
            "completed expansion batch cannot replay extra pages after the human reduces the target");
        const auto session = runtime()
                                 .value("modules")
                                 .toObject()
                                 .value("slides")
                                 .toObject()
                                 .value("documentSession")
                                 .toString();
        const auto saved = directory + "/human-target.pptx";
        check(slides.saveTo(QUrl::fromLocalFile(saved)) &&
                wait_for(
                    [&]()
        {
            return !slides.locked();
        }) && !slides.modified(),
            "revised three-page goal saves real bytes before reopening");
        check(slides.requestOpen(QUrl::fromLocalFile(saved)) &&
                wait_for(
                    [&]()
        {
            return !slides.locked();
        }),
            "reopen saved revised goal through the real document engine");
        const auto reopened_session = runtime()
                                          .value("modules")
                                          .toObject()
                                          .value("slides")
                                          .toObject()
                                          .value("documentSession")
                                          .toString();
        const auto reopened_batch = execute(box, "office_continue_slides", {{"batchId", "human-extension"}});
        check(reopened_session != session && slides.slideCount() == 3 &&
                !reopened_batch.value("ok").toBool() && reopened_batch.value("executed").toInt() == 0 &&
                (reopened_batch.value("error") == "workspace_changed" ||
                    reopened_batch.value("error") == "batch_id_reused"),
            "saved file reopened as another session cannot reuse prior session batch execution");
    }

    void test_large_segment(const QString& directory)
    {
        PresentationBridge slides;
        slides.requestNew();
        Root root;
        root.module = "slides";
        AutomationBridge automation;
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        OfficeAiToolbox box(directory);
        box.reset();
        QJsonArray batch;
        for (int index = 0; index < 8; ++index)
        {
            QJsonArray blocks;
            for (int block = 0; block < 4; ++block)
                blocks.append(QJsonObject{{"heading", QStringLiteral("Topic %1").arg(block)},
                    {"text", "A concrete supporting fact for this topic."}});
            batch.append(QJsonObject{{"title", QStringLiteral("Rich page %1").arg(index + 1)},
                {"layout", "grid"}, {"blocks", blocks}});
        }
        const QJsonObject request{{"batchId", "rich-eight"}, {"targetPages", 8}, {"pages", batch}};
        const auto result = execute(box, "office_compose_slides", request);
        check(result.value("ok").toBool() && result.value("complete").toBool() &&
                result.value("executed").toInt() > 256 && slides.slideCount() == 8,
            "more than 256 expanded edits finish in local chunks without another model request");
        const auto generation = slides.editGeneration();
        const auto duplicate = execute(box, "office_compose_slides", request);
        check(duplicate.value("alreadyApplied").toBool() && slides.editGeneration() == generation,
            "chunked segment replay remains idempotent");
        check(!execute(box, "office_save", {{"current", true}}).value("ok").toBool(),
            "saving an unnamed document never opens a save dialog");
        check(execute(box, "office_save", {{"current", "true"}}).value("error") == "invalid_save_mode" &&
                execute(box, "office_save", {{"current", true}, {"title", "Ambiguous"}}).value("error") ==
                    "invalid_save_mode",
            "current-file saving has an unambiguous typed contract");
        for (const auto& invalid :
            {QJsonObject{{"destination", 12}}, QJsonObject{{"destination", "relative.pptx"}},
                QJsonObject{{"destination", QDir(directory).filePath("wrong.docx")}},
                QJsonObject{{"destination", QDir(directory).filePath("missing/final.pptx")}},
                QJsonObject{{"destination", QDir(directory).filePath("final.pptx")}, {"title", "conflict"}},
                QJsonObject{{"destination", QDir(directory).filePath("final.pptx")}, {"current", true}}})
            check(!execute(box, "office_save", invalid).value("ok").toBool(),
                "explicit save destination rejects ambiguous modes and invalid file locations");
        const QString exact_path = QDir(directory).filePath("Eight rich pages.pptx");
        const auto saved =
            execute(box, "office_save", {{"title", QDir(directory).filePath("Eight rich pages")}});
        check(saved.value("ok").toBool() && saved.value("file").toObject().value("path") == exact_path,
            "absolute title path is not silently sanitized into a different output filename");
        const auto reopened =
            load_presentation_file(saved.value("file").toObject().value("path").toString().toStdString());
        check(reopened.error == PresentationError::None && reopened.scene.slides.size() == 8 &&
                find_presentation_text(reopened.scene, "Rich page 8").size() == 1,
            "last page survives chunk boundary and independent file readback");
        const auto schema = box.query("office_schema", {{"module", "slides"}, {"name", "addText"}});
        check(schema.value("invocation").toObject().value("op") == "slides.applyEdit",
            "edit schema shows its public wrapper instead of implying a nonexistent method");
        const auto alias = execute(box, "office_action",
            {{"op", "slides.addText"}, {"args", QJsonObject{{"text", "Known edit alias"}}}});
        check(alias.value("ok").toBool() && slides.editGeneration() != generation,
            "unambiguous edit shorthand routes through the permitted public wrapper");
        const auto updated = execute(box, "office_save", {{"destination", exact_path}});
        const auto path = saved.value("file").toObject().value("path").toString();
        const auto current = load_presentation_file(path.toStdString());
        check(updated.value("ok").toBool() && !slides.modified() &&
                updated.value("file").toObject().value("path").toString() == path &&
                current.error == PresentationError::None &&
                find_presentation_text(current.scene, "Known edit alias").size() == 1,
            "save current persists edits to the same file with verified completion");
        auto invocation = schema.value("invocation").toObject();
        auto invocation_args = invocation.value("args").toObject();
        invocation_args.insert("options", QJsonObject{{"text", "Named schema edit"}});
        invocation.insert("args", invocation_args);
        invocation.remove("tool");
        check(execute(box, "office_action", invocation).value("ok").toBool(),
            "named invocation returned by exact schema executes through the public bridge");
        const auto same_file = execute(
            box, "office_action", {{"op", "slides.saveTo"}, {"args", QJsonObject{{"destination", path}}}});
        const auto saved_again = load_presentation_file(path.toStdString());
        check(same_file.value("ok").toBool() && !slides.modified() &&
                same_file.value("resolvedAction") == "save" && saved_again.error == PresentationError::None &&
                find_presentation_text(saved_again.scene, "Named schema edit").size() == 1,
            "saveTo current path uses public save and waits for actual edited file bytes");
        check(execute(box, "office_action",
                  {{"op", "slides.saveTo"}, {"args", QJsonArray{QUrl::fromLocalFile(path).toString()}}})
                  .value("ok")
                  .toBool(),
            "current save also recognizes file URLs");
        const QString other = QDir(directory).filePath("do-not-overwrite.pptx");
        QFile other_file(other);
        check(other_file.open(QIODevice::WriteOnly) && other_file.write("preserve") == 8,
            "unrelated existing file fixture");
        other_file.close();
        check(!execute(box, "office_action", {{"op", "slides.saveTo"}, {"args", QJsonArray{other}}})
                    .value("ok")
                    .toBool() &&
                other_file.open(QIODevice::ReadOnly) && other_file.readAll() == "preserve",
            "current-path compatibility never overwrites a different existing file");
        other_file.close();
        check(!execute(box, "office_save", {{"destination", other}}).value("ok").toBool(),
            "explicit save helper cannot overwrite another existing destination");
        const QString second_path = QDir(directory).filePath("explicit-destination.pptx");
        const auto second =
            execute(box, "office_save", {{"destination", QUrl::fromLocalFile(second_path).toString()}});
        check(second.value("ok").toBool() && second.value("file").toObject().value("path") == second_path &&
                load_presentation_file(second_path.toStdString()).error == PresentationError::None,
            "explicit destination and local file URL produce the requested readable artifact");
        const auto rejected =
            execute(box, "office_action", {{"op", "slides.imaginedEdit"}, {"args", QJsonObject{}}});
        check(rejected.value("error") == "action_not_available", "unknown edit shorthand is never guessed");
    }

    void test_cover_image(const QString& directory)
    {
        const QString path = QDir(directory).filePath("cover-source.png");
        QImage image(640, 360, QImage::Format_RGB32);
        image.fill(QColor("#315F88"));
        check(image.save(path), "local cover fixture is available");
        PresentationImageExportOptions options;
        options.long_edge = 1280;
        PresentationImageExportProgress progress;
        {
            PresentationBridge slides;
            slides.requestNew();
            Root root;
            root.module = "slides";
            AutomationBridge automation;
            automation.registerModule("slides", &slides);
            automation.setUiRoot(&root);
            OfficeAiToolbox box(directory);
            box.reset();
            box.query("office_load_group", {{"group", "slides"}});
            const QJsonObject cover{{"title", "Image cover"}, {"layout", "cover"}, {"imagePath", path},
                {"coverWord", "Nature"},
                {"blocks", QJsonArray{QJsonObject{{"text", "A concrete opening summary."}}}}};
            QJsonObject missing = cover;
            missing.insert("imagePath", QDir(directory).filePath("missing.png"));
            const auto rejected = execute(box, "office_compose_slides",
                {{"batchId", "bad-cover"}, {"targetPages", 2}, {"pages", QJsonArray{cover, missing}}});
            check(rejected.value("error") == "invalid_page_image" && rejected.value("pageIndex") == 1 &&
                    rejected.value("field") == "imagePath" && slides.editGeneration() == "0",
                "missing image in a later page rejects the whole segment before edits");
            const auto composed = execute(box, "office_compose_slides",
                {{"batchId", "image-cover"}, {"targetPages", 1}, {"pages", QJsonArray{cover}}});
            check(composed.value("ok").toBool() && composed.value("complete").toBool() &&
                    composed.value("pages").toArray().size() == 1,
                "cover image completes through the public asynchronous image action");
            const auto rendered = render_presentation_images(slides.document().value<RenderPresentationPtr>(),
                0, {}, directory, "image-cover.pptx", options, progress);
            const QImage preview(QDir(rendered.path).filePath("slide-001.png"));
            check(rendered.success && rendered.pages == 1 && !preview.isNull() &&
                    preview.size() == QSize(1280, 720) && preview.pixelColor(1015, 372) == QColor("#315F88"),
                "headless cover export visibly draws the supplied image in its planned region");
            const auto saved = execute(box, "office_save", {{"title", "Image cover"}});
            const auto file = saved.value("file").toObject().value("path").toString();
            const auto reopened = load_presentation_file(file.toUtf8().toStdString());
            bool placed = false;
            if (reopened.error == PresentationError::None && reopened.scene.slides.size() == 1)
                for (const auto& shape : reopened.scene.slides.front().shapes)
                    placed = placed ||
                        (!shape.image_path.empty() && shape.transform[4] >= 620 &&
                            shape.transform[4] + shape.width <= 904 && shape.transform[5] >= 130 &&
                            shape.transform[5] + shape.height <= 430);
            check(reopened.error == PresentationError::None && reopened.scene.images.size() == 1 && placed,
                "cover image and fitted geometry survive native PPTX save and readback");
        }

        PresentationBridge visual_slides;
        visual_slides.requestNew();
        Root visual_root;
        visual_root.module = "slides";
        AutomationBridge visual_automation;
        visual_automation.registerModule("slides", &visual_slides);
        visual_automation.setUiRoot(&visual_root);
        OfficeAiToolbox visual_box(directory);
        visual_box.reset();
        visual_box.query("office_load_group", {{"group", "slides"}});
        const QJsonObject opening{{"title", "Image lesson"}, {"layout", "cover"}};
        const QJsonObject visual{{"title", "How coffee is brewed"}, {"imagePath", path},
            {"blocks",
                QJsonArray{QJsonObject{{"heading", "Pour over"},
                    {"text", "Water passes through ground coffee and a paper filter."}}}}};
        QJsonObject missing_visual = visual;
        missing_visual.insert("imagePath", QDir(directory).filePath("missing-visual.png"));
        const auto rejected_visual = execute(visual_box, "office_compose_slides",
            {{"batchId", "bad-visual"}, {"targetPages", 2}, {"pages", QJsonArray{opening, missing_visual}}});
        check(rejected_visual.value("error") == "invalid_page_image" &&
                rejected_visual.value("pageIndex") == 1 && visual_slides.editGeneration() == "0",
            "missing content image rejects the whole segment before editing");
        const auto composed_visual = execute(visual_box, "office_compose_slides",
            {{"batchId", "image-visual"}, {"targetPages", 2}, {"pages", QJsonArray{opening, visual}}});
        const auto inferred_visual = office_ai_page_recipe(visual,
            {{"ink", "#10233F"}, {"paper", "#F3F6FC"}, {"accent", "#246BFD"}}, 960, 540, 2, image.size());
        QJsonObject without_image = visual;
        without_image.remove("imagePath");
        without_image.insert("layout", "visual");
        const auto invalid_visual = office_ai_page_recipe(
            without_image, {{"ink", "#10233F"}, {"paper", "#F3F6FC"}, {"accent", "#246BFD"}}, 960, 540, 2);
        check(composed_visual.value("ok").toBool() && composed_visual.value("complete").toBool() &&
                composed_visual.value("pages").toArray().size() == 2 &&
                inferred_visual.value("ok").toBool() && invalid_visual.value("field") == "imagePath",
            "local image automatically selects the content visual layout");
        const auto visual_rendered =
            render_presentation_images(visual_slides.document().value<RenderPresentationPtr>(), 0, {},
                directory, "image-visual.pptx", options, progress);
        const QImage visual_preview(QDir(visual_rendered.path).filePath("slide-002.png"));
        check(visual_rendered.success && visual_rendered.pages == 2 && !visual_preview.isNull() &&
                visual_preview.pixelColor(410, 430) == QColor("#315F88"),
            "headless content export draws the supplied image beside the explanation");
        const auto visual_saved = execute(visual_box, "office_save", {{"title", "Image lesson"}});
        const auto visual_file = visual_saved.value("file").toObject().value("path").toString();
        const auto visual_reopened = load_presentation_file(visual_file.toUtf8().toStdString());
        bool content_image_placed = false;
        if (visual_reopened.error == PresentationError::None && visual_reopened.scene.slides.size() == 2)
            for (const auto& shape : visual_reopened.scene.slides.at(1).shapes)
                content_image_placed = content_image_placed ||
                    (!shape.image_path.empty() && shape.transform[4] >= 60 &&
                        shape.transform[4] + shape.width <= 560);
        check(visual_reopened.error == PresentationError::None && visual_reopened.scene.images.size() == 1 &&
                content_image_placed,
            "content image survives native PPTX save and readback");
    }

    void test_page_recipes()
    {
        const QJsonObject theme{{"ink", "#10233F"}, {"paper", "#F3F6FC"}, {"accent", "#246BFD"}};
        const auto editorial = office_ai_page_recipe(
            {{"title", "A story"}, {"layout", "cover"}}, office_ai_style_theme("editorial"), 960, 540, 1);
        const auto research = office_ai_page_recipe(
            {{"title", "A story"}, {"layout", "cover"}}, office_ai_style_theme("research"), 960, 540, 1);
        check(editorial.value("ok").toBool() && research.value("ok").toBool() &&
                editorial.value("background") != research.value("background") &&
                editorial.value("elements") != research.value("elements"),
            "selected styles produce different palettes and cover geometry");
        const QJsonArray two_blocks{QJsonObject{{"heading", "One"}, {"text", "First concrete point"}},
            QJsonObject{{"heading", "Two"}, {"text", "Second concrete point"}}};
        const auto auto_cover = office_ai_page_recipe(
            {{"title", "Opening"}, {"blocks", QJsonArray{QJsonObject{{"text", "Specific summary"}}}}}, theme,
            960, 540, 1);
        const auto empty_cover =
            office_ai_page_recipe({{"title", "Opening"}, {"layout", "cover"}}, theme, 960, 540, 1);
        const auto missing_content =
            office_ai_page_recipe({{"title", "Content"}, {"layout", "grid"}}, theme, 960, 540, 2);
        check(empty_cover.value("ok").toBool() && missing_content.value("field") == "blocks",
            "a model may omit empty cover blocks but content pages still need blocks");
        const auto auto_columns =
            office_ai_page_recipe({{"title", "Evidence"}, {"blocks", two_blocks}}, theme, 960, 540, 2);
        const auto auto_grid =
            office_ai_page_recipe({{"title", "Actions"}, {"blocks", two_blocks}}, theme, 960, 540, 3);
        const auto explicit_grid = office_ai_page_recipe(
            {{"title", "Explicit"}, {"layout", "grid"}, {"blocks", two_blocks}}, theme, 960, 540, 1);
        const auto filled_panels = [](const QJsonObject& recipe)
        {
            int count = 0;
            for (const auto& value : recipe.value("elements").toArray())
            {
                const auto element = value.toObject();
                if (element.value("type") == "shape" && element.value("width").toDouble() > 200 &&
                    element.value("height").toDouble() > 70)
                    ++count;
            }
            return count;
        };
        check(auto_cover.value("ok").toBool() &&
                auto_cover.value("background")
                        .toString()
                        .compare(theme.value("ink").toString(), Qt::CaseInsensitive) == 0 &&
                auto_columns.value("ok").toBool() && filled_panels(auto_columns) >= 2 &&
                auto_grid.value("ok").toBool() && filled_panels(auto_grid) == 0 &&
                explicit_grid.value("ok").toBool() && explicit_grid.value("background") != theme.value("ink"),
            "omitted layouts rotate cover, parallel and editorial pages while explicit layout wins");
        const auto illustrated_cover = office_ai_page_recipe(
            {{"title", QStringLiteral("识毒防毒 从拒绝第一次开始")}, {"layout", "cover"},
                {"blocks", QJsonArray{QJsonObject{{"text", "A concrete introduction"}}}}},
            theme, 960, 540, 1);
        bool has_monogram = false;
        bool has_visual_panel = false;
        bool cover_title_fits = false;
        for (const auto& value : illustrated_cover.value("elements").toArray())
        {
            const auto element = value.toObject();
            has_monogram = has_monogram ||
                (element.value("type") == "text" && element.value("text") == QStringLiteral("识毒") &&
                    element.value("x").toDouble() > 700);
            has_visual_panel = has_visual_panel ||
                (element.value("type") == "shape" && element.value("x").toDouble() > 700 &&
                    element.value("width").toDouble() > 200 && element.value("height").toDouble() > 500);
            if (element.value("text") == QStringLiteral("识毒防毒 从拒绝第一次开始"))
            {
                QFont font(QStringLiteral("Microsoft YaHei"));
                font.setBold(true);
                font.setPixelSize(qRound(element.value("style").toObject().value("fontSize").toDouble()));
                cover_title_fits = QFontMetricsF(font).horizontalAdvance(element.value("text").toString()) <=
                    element.value("width").toDouble() - 12;
            }
        }
        check(illustrated_cover.value("ok").toBool() && has_monogram && has_visual_panel && cover_title_fits,
            "a text-only cover uses its topic and keeps a short headline on one line");
        const auto technical_cover =
            office_ai_page_recipe({{"title", "从第一行代码开始学 C++"}, {"layout", "cover"},
                                      {"blocks", QJsonArray{QJsonObject{{"text", "从小程序开始练习。"}}}}},
                theme, 960, 540, 1);
        bool has_cpp_monogram = false;
        for (const auto& value : technical_cover.value("elements").toArray())
        {
            const auto element = value.toObject();
            has_cpp_monogram = has_cpp_monogram ||
                (element.value("type") == "text" && element.value("text") == "C++" &&
                    element.value("x").toDouble() > 700);
        }
        check(technical_cover.value("ok").toBool() && has_cpp_monogram,
            "a technical cover keeps C++ together instead of combining C with a Chinese title character");
        const auto semantic_cover = office_ai_page_recipe(
            {{"title", QStringLiteral("一杯咖啡的风味从哪里来")}, {"layout", "cover"},
                {"coverWord", QStringLiteral("咖啡风味")},
                {"blocks", QJsonArray{QJsonObject{{"text", QStringLiteral("认识影响风味的因素。")}}}}},
            theme, 960, 540, 1);
        bool has_semantic_word = false;
        bool has_visual_circle = false;
        for (const auto& value : semantic_cover.value("elements").toArray())
        {
            const auto element = value.toObject();
            has_semantic_word = has_semantic_word ||
                (element.value("type") == "text" && element.value("text") == QStringLiteral("咖啡风味") &&
                    element.value("x").toDouble() > 700);
            has_visual_circle = has_visual_circle ||
                (element.value("type") == "shape" && element.value("geometry") == "ellipse" &&
                    element.value("width").toDouble() > 200 && element.value("x").toDouble() > 700);
        }
        check(semantic_cover.value("ok").toBool() && has_semantic_word && has_visual_circle,
            "model-supplied cover word gives the visual a meaningful topic");
        const QString real_summary = QStringLiteral("从豆种到杯中：四个环节决定你喝到的味道（零基础入门）");
        const auto long_cover =
            office_ai_page_recipe({{"title", QStringLiteral("一杯咖啡的风味从哪里来")}, {"layout", "cover"},
                                      {"coverWord", QStringLiteral("咖啡")}, {"subtitle", real_summary}},
                theme, 960, 540, 1);
        bool summary_fits = false;
        for (const auto& value : long_cover.value("elements").toArray())
        {
            const auto element = value.toObject();
            if (element.value("text") != real_summary)
                continue;
            QFont font(QStringLiteral("Microsoft YaHei"));
            font.setPixelSize(qRound(element.value("style").toObject().value("fontSize").toDouble()));
            summary_fits =
                QFontMetricsF(font).horizontalAdvance(real_summary) <= element.value("width").toDouble() - 12;
        }
        check(long_cover.value("ok").toBool() && summary_fits,
            "real model cover summary fits on one line without an orphan fragment");
        for (const auto& value : semantic_cover.value("elements").toArray())
        {
            const auto element = value.toObject();
            if (element.value("text") != QStringLiteral("咖啡风味"))
                continue;
            QFont font(QStringLiteral("Microsoft YaHei"));
            font.setBold(true);
            font.setPixelSize(qRound(element.value("style").toObject().value("fontSize").toDouble()));
            check(QFontMetricsF(font).horizontalAdvance(QStringLiteral("咖啡风味")) <=
                    element.value("width").toDouble() - 12,
                "four-character model cover word fits on a single line");
        }
        const auto invalid_cover_word =
            office_ai_page_recipe({{"title", "Cover"}, {"layout", "cover"},
                                      {"coverWord", "An excessive topic label"}, {"blocks", QJsonArray{}}},
                theme, 960, 540, 1);
        check(invalid_cover_word.value("error") == "invalid_page" &&
                invalid_cover_word.value("field") == "coverWord",
            "overlong cover word is rejected before editing a page");
        const auto related_pair = office_ai_page_recipe(
            {{"title", QStringLiteral("两个常见误区")}, {"layout", "comparison"}, {"blocks", two_blocks}},
            theme, 960, 540, 2);
        bool false_opposition = false;
        for (const auto& value : related_pair.value("elements").toArray())
            false_opposition = false_opposition || value.toObject().value("text") == QStringLiteral("对");
        check(related_pair.value("ok").toBool() && !false_opposition && filled_panels(related_pair) >= 2,
            "two related points remain readable if the model requests comparison by mistake");
        const auto inferred = [&](const QString& title, const QString& subtitle, const QJsonArray& blocks,
                                  const QString& expected)
        {
            QJsonObject page{{"title", title}, {"subtitle", subtitle}, {"blocks", blocks}};
            const auto automatic = office_ai_page_recipe(page, theme, 960, 540, 2);
            page.insert("layout", expected);
            const auto explicit_recipe = office_ai_page_recipe(page, theme, 960, 540, 2);
            check(automatic.value("ok").toBool() && automatic == explicit_recipe,
                "an unambiguous title selects the matching native composition without model layout syntax");
        };
        inferred(QStringLiteral("两种处理方式对比"), {}, two_blocks, "comparison");
        inferred(QStringLiteral("实施步骤"), {}, two_blocks, "steps");
        inferred(QStringLiteral("要素关系"), QStringLiteral("共同目标"), two_blocks, "hub");
        inferred(QStringLiteral("核心观点"), {},
            QJsonArray{QJsonObject{{"text", QStringLiteral("先核实，再行动。")}}}, "statement");
        const QJsonObject override_page{
            {"title", QStringLiteral("两种处理方式对比")}, {"layout", "grid"}, {"blocks", two_blocks}};
        const auto overridden = office_ai_page_recipe(override_page, theme, 960, 540, 2);
        check(overridden.value("ok").toBool() &&
                overridden !=
                    office_ai_page_recipe(
                        {{"title", QStringLiteral("两种处理方式对比")}, {"blocks", two_blocks}}, theme, 960,
                        540, 2),
            "an explicit editorial choice overrides title-based composition");
        const QJsonArray long_steps{QJsonObject{{"text", QString(80, QChar(0x6B65))}},
            QJsonObject{{"text", QString(80, QChar(0x6B65))}},
            QJsonObject{{"text", QString(80, QChar(0x6B65))}}};
        const QJsonObject long_process{{"title", QStringLiteral("实施步骤")}, {"blocks", long_steps}};
        auto long_columns = long_process;
        long_columns.insert("layout", "columns");
        check(office_ai_page_recipe(long_process, theme, 960, 540, 2) ==
                office_ai_page_recipe(long_columns, theme, 960, 540, 2),
            "dense text does not trigger an inferred process layout that cannot fit it");
        const QJsonArray three_blocks{QJsonObject{{"heading", "Lead"}, {"text", "A concrete lead"}},
            QJsonObject{{"heading", "Support"}, {"text", "A supporting point"}},
            QJsonObject{{"heading", "Action"}, {"text", "A practical action"}}};
        const QJsonObject mirror_page{
            {"title", "Three concrete points"}, {"layout", "grid"}, {"blocks", three_blocks}};
        const auto left_lead = office_ai_page_recipe(mirror_page, theme, 960, 540, 3);
        const auto right_lead = office_ai_page_recipe(mirror_page, theme, 960, 540, 4);
        const auto text_x = [](const QJsonObject& recipe, const QString& text)
        {
            for (const auto& value : recipe.value("elements").toArray())
                if (value.toObject().value("text") == text)
                    return value.toObject().value("x").toDouble();
            return -1.0;
        };
        check(left_lead.value("ok").toBool() && right_lead.value("ok").toBool() &&
                text_x(left_lead, "Lead") < text_x(left_lead, "Support") &&
                text_x(right_lead, "Lead") > text_x(right_lead, "Support"),
            "three-point editorial pages alternate lead and supporting columns without dropping text");
        const QStringList layouts{"cover", "grid", "columns", "steps", "comparison", "statement", "hub"};
        QString grid_panels;
        QString column_panels;
        QString step_panels;
        for (const auto& layout : layouts)
        {
            QJsonArray blocks;
            const int count = layout == "cover" || layout == "statement" ? 1 : layout == "comparison" ? 2 : 3;
            for (int index = 0; index < count; ++index)
                blocks.append(QJsonObject{{"heading", QStringLiteral("Heading %1").arg(index)},
                    {"text", QStringLiteral("Body %1").arg(index)}});
            const QJsonObject page{{"title", "Distinct title"}, {"subtitle", "Useful subtitle"},
                {"eyebrow", "Section"}, {"layout", layout}, {"blocks", blocks}};
            const auto recipe = office_ai_page_recipe(page, theme, 960, 540, 1);
            if (!recipe.value("ok").toBool())
                std::cerr << "Layout " << layout.toStdString() << ": " << json(recipe).toStdString() << '\n';
            check(recipe.value("ok").toBool(), "every supported layout plans a complete page");
            QStringList rendered;
            for (const auto& value : recipe.value("elements").toArray())
                if (value.toObject().value("type") == "text")
                    rendered.append(value.toObject().value("text").toString());
            const auto all_text = rendered.join(' ');
            check(all_text.contains("Distinct title") && all_text.contains("Useful subtitle") &&
                    all_text.contains("Section"),
                "page metadata survives planning");
            if (layout != "cover")
            {
                QJsonObject title_box;
                QJsonObject section_box;
                for (const auto& value : recipe.value("elements").toArray())
                {
                    const auto element = value.toObject();
                    if (element.value("text") == "Distinct title")
                        title_box = element;
                    else if (element.value("text") == "Section")
                        section_box = element;
                }
                check(!title_box.isEmpty() && !section_box.isEmpty() &&
                        title_box.value("x").toDouble() + title_box.value("width").toDouble() <=
                            section_box.value("x").toDouble(),
                    "title and section marker keep separate text regions");
            }
            for (int index = 0; index < count; ++index)
                check(all_text.contains(QStringLiteral("Heading %1").arg(index)) &&
                        all_text.contains(QStringLiteral("Body %1").arg(index)),
                    "every supplied block survives planning");
            if (layout == "grid" || layout == "columns" || layout == "steps")
            {
                QStringList panels;
                for (const auto& value : recipe.value("elements").toArray())
                {
                    const auto element = value.toObject();
                    if (element.value("type") == "shape" && element.value("width").toDouble() > 200 &&
                        element.value("height").toDouble() > 70)
                        panels.append(QStringLiteral("%1/%2/%3/%4")
                                .arg(element.value("x").toDouble())
                                .arg(element.value("y").toDouble())
                                .arg(element.value("width").toDouble())
                                .arg(element.value("height").toDouble()));
                }
                if (layout == "grid")
                    grid_panels = panels.join(';');
                else if (layout == "columns")
                    column_panels = panels.join(';');
                else
                    step_panels = panels.join(';');
            }
        }
        check(grid_panels.isEmpty() && !column_panels.isEmpty() && !step_panels.isEmpty() &&
                column_panels != step_panels,
            "editorial grid has no filled cards while columns and steps retain distinct panels");
        for (const int count : {1, 2, 4})
        {
            QJsonArray blocks;
            for (int index = 0; index < count; ++index)
                blocks.append(QJsonObject{{"heading", QStringLiteral("Point %1").arg(index + 1)},
                    {"text", QString(48, QChar(0x8BF4))}});
            const auto recipe = office_ai_page_recipe(
                {{"title", "Editorial hierarchy"}, {"layout", "grid"}, {"blocks", blocks}}, theme, 960, 540,
                count);
            check(recipe.value("ok").toBool(),
                "one, two and four point editorial pages fit useful text without truncation");
            int bodies = 0;
            for (const auto& value : recipe.value("elements").toArray())
                if (value.toObject().value("text") == QString(48, QChar(0x8BF4)))
                    ++bodies;
            check(bodies == count, "editorial page preserves every supplied body");
        }
        const auto invalid = office_ai_page_recipe(
            {{"title", "Empty"}, {"layout", "grid"}, {"blocks", QJsonArray{}}}, theme, 960, 540, 1);
        check(invalid.value("error") == "invalid_page" && invalid.value("field") == "blocks" &&
                invalid.value("layout") == "grid" && invalid.value("blockCount") == 0,
            "empty content pages identify the block count before edits");
        const auto invalid_comparison =
            office_ai_page_recipe({{"title", "Compare"}, {"layout", "comparison"},
                                      {"blocks", QJsonArray{QJsonObject{{"text", "Only one"}}}}},
                theme, 960, 540, 1);
        check(invalid_comparison.value("error") == "invalid_page" &&
                invalid_comparison.value("field") == "blocks" && invalid_comparison.value("blockCount") == 1,
            "comparison returns its actual block count for model repair");
        const auto missing_hub_focus = office_ai_page_recipe(
            {{"title", "Relations"}, {"layout", "hub"}, {"blocks", two_blocks}}, theme, 960, 540, 3);
        check(missing_hub_focus.value("error") == "invalid_page" &&
                missing_hub_focus.value("field") == "subtitle",
            "relationship page requires a short central idea before applying any edit");
        const auto hub = office_ai_page_recipe({{"title", "Relations"}, {"subtitle", "Shared purpose"},
                                                   {"layout", "hub"}, {"blocks", two_blocks}},
            theme, 960, 540, 3);
        QString hub_text;
        for (const auto& value : hub.value("elements").toArray())
            if (value.toObject().value("type") == "text")
                hub_text += value.toObject().value("text").toString();
        check(hub.value("ok").toBool() && hub_text.contains("Shared purpose") &&
                hub_text.contains("First concrete point") && hub_text.contains("Second concrete point"),
            "relationship page retains its central concept and both supporting points");
        QJsonArray four_facets;
        for (int index = 0; index < 4; ++index)
            four_facets.append(QJsonObject{{"heading", QStringLiteral("Aspect %1").arg(index + 1)},
                {"text", QString(25, QChar(0x5185))}});
        const auto dense_hub = office_ai_page_recipe({{"title", "Relations"}, {"subtitle", "Shared purpose"},
                                                         {"layout", "hub"}, {"blocks", four_facets}},
            theme, 960, 540, 4);
        int left_facets = 0;
        int right_facets = 0;
        int visible_bodies = 0;
        for (const auto& value : dense_hub.value("elements").toArray())
        {
            const auto element = value.toObject();
            const auto content = element.value("text").toString();
            if (content.startsWith("Aspect "))
            {
                left_facets += element.value("x").toDouble() < 400 ? 1 : 0;
                right_facets += element.value("x").toDouble() > 600 ? 1 : 0;
            }
            visible_bodies += content == QString(25, QChar(0x5185)) ? 1 : 0;
        }
        check(dense_hub.value("ok").toBool() && left_facets == 2 && right_facets == 2 && visible_bodies == 4,
            "four relationship facets surround the center without dropping their content");
        const QJsonArray coffee_facets{
            QJsonObject{{"heading", "豆种"},
                {"text", "不同品种与产地带来酸、甜、果香等基础底味，是风味的原料底色。"}},
            QJsonObject{
                {"heading", "烘焙"}, {"text", "烘焙深浅决定酸度与苦味的走向，控制香气是明亮还是焦香。"}},
            QJsonObject{{"heading", "研磨"}, {"text", "颗粒粗细影响水穿过咖啡粉的速度，粗则淡、细则浓。"}},
            QJsonObject{
                {"heading", "萃取"}, {"text", "水温、时间与比例决定风味释放多少，过度则苦，不足则酸。"}}};
        const auto coffee_hub =
            office_ai_page_recipe({{"title", "风味是四个环节共同作用的结果"}, {"subtitle", "咖啡风味"},
                                      {"layout", "hub"}, {"blocks", coffee_facets}},
                theme, 960, 540, 2);
        check(coffee_hub.value("ok").toBool(),
            "real model four-facet explanations fit the radial relationship layout");
        const auto invalid_block = office_ai_page_recipe(
            {{"title", "Content"}, {"layout", "columns"},
                {"blocks", QJsonArray{QJsonObject{{"text", "Valid"}}, QJsonObject{{"heading", "Missing"}}}}},
            theme, 960, 540, 1);
        check(invalid_block.value("error") == "invalid_block" && invalid_block.value("blockIndex") == 1,
            "malformed model content points to the affected block");
        const auto blank_block = office_ai_page_recipe(
            {{"title", "Content"}, {"layout", "grid"},
                {"blocks", QJsonArray{QJsonObject{{"heading", "Empty"}, {"text", "   "}}}}},
            theme, 960, 540, 1);
        check(blank_block.value("error") == "invalid_block" && blank_block.value("blockIndex") == 0,
            "whitespace-only model blocks are rejected before editing");
        const auto long_text = QString(600, QChar(0x957F));
        const auto overflow =
            office_ai_page_recipe({{"title", "Feedback"}, {"layout", "grid"},
                                      {"blocks",
                                          QJsonArray{QJsonObject{{"heading", "One"}, {"text", "Brief"}},
                                              QJsonObject{{"heading", "Two"}, {"text", long_text}},
                                              QJsonObject{{"heading", "Three"}, {"text", "Brief"}}}}},
                theme, 960, 540, 2);
        check(overflow.value("error") == "page_text_overflow" && overflow.value("blockIndex") == 1 &&
                overflow.value("textLength") == 600,
            "overflow identifies the exact model block to split");
        const auto title_overflow =
            office_ai_page_recipe({{"title", QString(100, QChar(0x957F))}, {"layout", "grid"},
                                      {"blocks", QJsonArray{QJsonObject{{"text", "Brief"}}}}},
                theme, 960, 540, 1);
        check(title_overflow.value("error") == "page_text_overflow" &&
                title_overflow.value("field") == "title" && title_overflow.value("textLength") == 100,
            "title overflow identifies the exact field for model repair");
        const auto statement =
            office_ai_page_recipe({{"title", "Conclusion"}, {"subtitle", "One clear takeaway"},
                                      {"layout", "statement"}, {"blocks", QJsonArray{}}},
                theme, 960, 540, 1);
        int statement_copies = 0;
        for (const auto& value : statement.value("elements").toArray())
            if (value.toObject().value("text") == "One clear takeaway")
                ++statement_copies;
        check(statement.value("ok").toBool() && statement_copies == 1,
            "subtitle-only statement renders its message once");
        const auto short_card =
            office_ai_page_recipe({{"title", "Typography"}, {"layout", "columns"},
                                      {"blocks", QJsonArray{QJsonObject{{"text", "Short signal"}}}}},
                theme, 960, 540, 1);
        const auto long_card = office_ai_page_recipe(
            {{"title", "Typography"}, {"layout", "columns"},
                {"blocks", QJsonArray{QJsonObject{{"text", QString(200, QChar(0x8BF4))}}}}},
            theme, 960, 540, 1);
        const auto body_size = [](const QJsonObject& recipe)
        {
            for (const auto& value : recipe.value("elements").toArray())
            {
                const auto element = value.toObject();
                if (element.value("text") == "Short signal" ||
                    element.value("text").toString() == QString(200, QChar(0x8BF4)))
                    return element.value("style").toObject().value("fontSize").toDouble();
            }
            return 0.0;
        };
        check(short_card.value("ok").toBool() && long_card.value("ok").toBool() &&
                body_size(short_card) > body_size(long_card),
            "sparse model text receives stronger visual hierarchy without deleting content");
        const QStringList sampled_bodies{
            QString(43, QChar(0x5B66)), QString(48, QChar(0x5199)), QString(49, QChar(0x8C03))};
        QJsonArray sampled_blocks;
        for (int index = 0; index < sampled_bodies.size(); ++index)
            sampled_blocks.append(QJsonObject{
                {"heading", QStringLiteral("要点 %1").arg(index + 1)}, {"text", sampled_bodies.at(index)}});
        const auto sampled_columns = office_ai_page_recipe(
            {{"title", QStringLiteral("三条学习建议")}, {"layout", "columns"}, {"blocks", sampled_blocks}},
            theme, 960, 540, 2);
        QList<double> sampled_sizes;
        int numbered_footers = 0;
        for (const auto& value : sampled_columns.value("elements").toArray())
        {
            const auto element = value.toObject();
            if (sampled_bodies.contains(element.value("text").toString()))
                sampled_sizes.append(element.value("style").toObject().value("fontSize").toDouble());
            if (QStringList{"01", "02", "03"}.contains(element.value("text").toString()))
                ++numbered_footers;
        }
        check(sampled_columns.value("ok").toBool() && sampled_sizes.size() == 3 &&
                sampled_sizes.at(0) == sampled_sizes.at(1) && sampled_sizes.at(1) == sampled_sizes.at(2) &&
                sampled_sizes.at(0) <= 23 && numbered_footers == 3,
            "real model three-column lengths keep consistent readable type and visible card indices");
        const QStringList examples{QStringLiteral("输出 Hello World"),
            QStringLiteral("error: expected ';' before '}'"), QStringLiteral("先查看出错行号")};
        QJsonArray evidence_blocks;
        for (int index = 0; index < examples.size(); ++index)
            evidence_blocks.append(QJsonObject{{"heading", QStringLiteral("建议 %1").arg(index + 1)},
                {"text", QString(26, QChar(0x5B66))}, {"example", examples.at(index)}});
        const auto evidence_cards = office_ai_page_recipe(
            {{"title", QStringLiteral("三条学习建议")}, {"layout", "columns"}, {"blocks", evidence_blocks}},
            theme, 960, 540, 2);
        int visible_examples = 0;
        int example_labels = 0;
        for (const auto& value : evidence_cards.value("elements").toArray())
        {
            const auto card_text = value.toObject().value("text").toString();
            visible_examples += examples.contains(card_text) ? 1 : 0;
            example_labels += card_text.startsWith(QStringLiteral("例 / ")) ? 1 : 0;
        }
        check(evidence_cards.value("ok").toBool() && visible_examples == 3 && example_labels == 3,
            "model examples become separate visible card evidence without replacing the body");
        const auto editorial_example = office_ai_page_recipe(
            {{"title", "Editorial"}, {"layout", "grid"},
                {"blocks", QJsonArray{QJsonObject{{"text", "Main point"}, {"example", "Concrete case"}}}}},
            theme, 960, 540, 3);
        bool preserved_example = false;
        for (const auto& value : editorial_example.value("elements").toArray())
            preserved_example = preserved_example ||
                value.toObject().value("text").toString().contains(QStringLiteral("例：Concrete case"));
        const auto invalid_example = office_ai_page_recipe(
            {{"title", "Invalid"}, {"layout", "columns"},
                {"blocks",
                    QJsonArray{QJsonObject{{"text", "Main point"}, {"example", QString(65, QChar('x'))}}}}},
            theme, 960, 540, 2);
        check(editorial_example.value("ok").toBool() && preserved_example &&
                invalid_example.value("error") == "invalid_block" && invalid_example.value("blockIndex") == 0,
            "other layouts preserve supplied examples and oversized examples fail before editing");
        QJsonArray four_steps;
        for (int index = 0; index < 4; ++index)
            four_steps.append(QJsonObject{
                {"heading", QStringLiteral("阶段 %1").arg(index + 1)}, {"text", QString(48, QChar(0x8BF4))}});
        const auto process = office_ai_page_recipe(
            {{"title", "Four steps"}, {"layout", "steps"}, {"blocks", four_steps}}, theme, 960, 540, 3);
        check(process.value("ok").toBool(), "four moderately detailed steps fit without truncation");
        int process_arrows = 0;
        for (const auto& value : process.value("elements").toArray())
        {
            const auto geometry = value.toObject().value("geometry").toString();
            if (geometry == "rightArrow" || geometry == "leftArrow")
                ++process_arrows;
        }
        check(process_arrows == 2, "four-step page exposes a connected reading order");
        const QJsonArray flow_blocks{
            QJsonObject{{"heading", "写源码"}, {"text", "把逻辑写入 main.cpp。"}, {"example", "cout << 1;"}},
            QJsonObject{{"heading", "编译"}, {"text", "编译器检查语法并生成程序。"}},
            QJsonObject{{"heading", "运行"}, {"text", "执行程序并查看输出结果。"}}};
        const auto flow = office_ai_page_recipe(
            {{"title", "源码到运行的流程"}, {"layout", "flow"}, {"blocks", flow_blocks}}, theme, 960, 540, 2);
        int flow_arrows = 0;
        int visible_flow_text = 0;
        for (const auto& value : flow.value("elements").toArray())
        {
            const auto element = value.toObject();
            flow_arrows += element.value("geometry") == "rightArrow" ? 1 : 0;
            for (const auto& block : flow_blocks)
            {
                const auto object = block.toObject();
                visible_flow_text += element.value("text") == object.value("text") ? 1 : 0;
            }
        }
        check(flow.value("ok").toBool() && flow_arrows == 2 && visible_flow_text == 3,
            "native flow links three model stages and keeps every explanation visible");
        const auto inferred_flow = office_ai_page_recipe(
            {{"title", "源码到运行的流程"}, {"blocks", flow_blocks}}, theme, 960, 540, 2);
        int inferred_arrows = 0;
        for (const auto& value : inferred_flow.value("elements").toArray())
            inferred_arrows += value.toObject().value("geometry") == "rightArrow" ? 1 : 0;
        check(inferred_flow.value("ok").toBool() && inferred_arrows == 2,
            "short three-stage process uses the connected layout when the model omits layout");
        auto long_flow_blocks = flow_blocks;
        auto long_block = long_flow_blocks.at(1).toObject();
        long_block.insert("text", QString(240, QChar(0x5B66)));
        long_flow_blocks[1] = long_block;
        const auto overflowing_flow = office_ai_page_recipe(
            {{"title", "流程"}, {"layout", "flow"}, {"blocks", long_flow_blocks}}, theme, 960, 540, 2);
        check(overflowing_flow.value("error") == "page_text_overflow" &&
                overflowing_flow.value("blockIndex") == 1,
            "flow overflow points to the offending model block before any edit");
        const QJsonArray claims{
            QJsonObject{{"heading", "只试一次没关系"}, {"text", "后果无法预判，明确拒绝并离开现场。"},
                {"example", "向家人说明情况"}},
            QJsonObject{{"heading", "包装新奇就安全"}, {"text", "不能凭外观判断来源，拒绝不明物品。"}},
            QJsonObject{{"heading", "自己不会受影响"}, {"text", "误判风险会耽误求助，及时告诉可信成年人。"}}};
        const auto factcheck =
            office_ai_page_recipe({{"title", "常见误区与隐藏风险"}, {"blocks", claims}}, theme, 960, 540, 2);
        int fact_arrows = 0;
        int corrections = 0;
        int preserved_claims = 0;
        for (const auto& value : factcheck.value("elements").toArray())
        {
            const auto element = value.toObject();
            fact_arrows += element.value("geometry") == "rightArrow" ? 1 : 0;
            corrections += element.value("text") == QStringLiteral("实际情况") ? 1 : 0;
            for (const auto& claim : claims)
            {
                const auto block = claim.toObject();
                preserved_claims += element.value("text") == block.value("heading") ? 1 : 0;
                preserved_claims += element.value("text") == block.value("text") ? 1 : 0;
            }
        }
        check(factcheck.value("ok").toBool() && fact_arrows == 3 && corrections == 3 && preserved_claims == 6,
            "three claims render as claim-to-correction rows without dropping model text");
        auto invalid_claims = claims;
        invalid_claims[1] = QJsonObject{{"text", "缺少说法"}};
        const auto missing_claim = office_ai_page_recipe(
            {{"title", "误区"}, {"layout", "factcheck"}, {"blocks", invalid_claims}}, theme, 960, 540, 2);
        auto long_claims = claims;
        long_claims[0] = QJsonObject{{"heading", "只试一次没关系"}, {"text", QString(130, QChar(0x5B66))}};
        const auto overflowing_fact = office_ai_page_recipe(
            {{"title", "误区"}, {"layout", "factcheck"}, {"blocks", long_claims}}, theme, 960, 540, 2);
        check(missing_claim.value("error") == "invalid_block" && missing_claim.value("blockIndex") == 1 &&
                overflowing_fact.value("error") == "page_text_overflow" &&
                overflowing_fact.value("blockIndex") == 0,
            "factcheck requires a claim and rejects oversized corrections before any edit");
        const QString complete_code = QStringLiteral(
            "#include <iostream>\nint main()\n{\n    std::cout << \"Hello, C++!\" << std::endl;\n"
            "    return 0;\n}");
        const auto code_page = office_ai_page_recipe(
            {{"title", "完整可执行示例"}, {"code", complete_code},
                {"blocks", QJsonArray{QJsonObject{{"heading", "验证"}, {"text", "运行后看到 Hello, C++!"}}}}},
            theme, 960, 540, 3);
        bool source_preserved = false;
        for (const auto& value : code_page.value("elements").toArray())
        {
            const auto element = value.toObject();
            source_preserved = source_preserved ||
                (element.value("text") == complete_code &&
                    element.value("style").toObject().value("fontFamily") == "Consolas");
        }
        check(code_page.value("ok").toBool() && source_preserved,
            "code field selects a full-width source layout and preserves the complete program");
        const auto missing_code = office_ai_page_recipe(
            {{"title", "Incomplete"}, {"layout", "code"}, {"blocks", QJsonArray{}}}, theme, 960, 540, 3);
        const auto overflowing_code = office_ai_page_recipe(
            {{"title", "Too many lines"}, {"layout", "code"},
                {"code", QStringLiteral("int main() {}\n").repeated(25)}, {"blocks", QJsonArray{}}},
            theme, 960, 540, 3);
        const auto wide_code =
            office_ai_page_recipe({{"title", "Too wide"}, {"layout", "code"},
                                      {"code", QString(190, QChar('W'))}, {"blocks", QJsonArray{}}},
                theme, 960, 540, 3);
        check(missing_code.value("field") == "code" &&
                overflowing_code.value("error") == "page_text_overflow" &&
                overflowing_code.value("field") == "code" &&
                wide_code.value("error") == "page_text_overflow" && wide_code.value("field") == "code",
            "missing, tall or unwrappable source is rejected before an editable slide exists");
    }

    void test_structured_recipes()
    {
        const QVariantMap theme{{"wordAccent", "#4269B2"}, {"textSecondary", "#746A61"},
            {"sheetsSurface", "#EAF2EC"}, {"sheetsHeaderFill", "#35644C"}, {"sheetsStrongLine", "#7C9687"},
            {"sheetsTableBodyFill", "#FFFFFF"}, {"sheetsBandFill", "#EDF4EF"},
            {"sheetsHeaderText", "#FFFFFF"}, {"sheetsTableBodyText", "#24352B"}};
        const QJsonObject report{{"title", QStringLiteral("咖啡风味观察")},
            {"subtitle", QStringLiteral("从研磨到萃取")},
            {"sections",
                QJsonArray{QJsonObject{{"heading", QStringLiteral("研磨")},
                               {"paragraphs", QJsonArray{QStringLiteral("粗细影响接触面积和溶出速度。")}},
                               {"bullets", QJsonArray{QStringLiteral("按冲煮方法调整粗细")}}},
                    QJsonObject{{"heading", QStringLiteral("萃取")},
                        {"paragraphs", QJsonArray{QStringLiteral("留意时间和粉水比。")}}}}}};
        const auto word = office_ai_word_recipe(report, theme);
        check(word.value("ok").toBool() && word.value("paragraphCount") == 7 &&
                word.value("steps").toArray().first().toObject().value("action") == "insertText" &&
                word.value("expectedText").toString().contains(QStringLiteral("留意时间和粉水比")),
            "Word recipe plans complete text and styles before public edits");
        QJsonObject invalid_report = report;
        invalid_report.insert("title", "Line one\nLine two");
        check(office_ai_word_recipe(invalid_report, theme).value("field") == "title",
            "Word recipe rejects structural title input before editing");
        const QJsonObject table{{"title", QStringLiteral("咖啡记录")},
            {"columns", QJsonArray{QStringLiteral("项目"), QStringLiteral("数量")}},
            {"rows",
                QJsonArray{QJsonArray{QStringLiteral("豆量"), 18},
                    QJsonArray{QStringLiteral("合计"), QStringLiteral("=SUM(B4:B4)")}}},
            {"totalRow", true}};
        const auto sheet = office_ai_table_recipe(table, theme);
        check(sheet.value("ok").toBool() && sheet.value("rowCount") == 3 && sheet.value("columnCount") == 2 &&
                sheet.value("steps").toArray().size() == 9,
            "table recipe preflights formulas and plans one paste plus named styles");
        QJsonObject invalid_table = table;
        invalid_table.insert("rows", QJsonArray{QJsonValue(QJsonArray{QStringLiteral("豆量")})});
        check(office_ai_table_recipe(invalid_table, theme).value("field") == "rows[0]",
            "table recipe rejects ragged rows before editing");
        invalid_table.insert(
            "rows", QJsonArray{QJsonValue(QJsonArray{QStringLiteral("豆量"), QStringLiteral("=SUM(")})});
        check(office_ai_table_recipe(invalid_table, theme).value("field") == "rows[0][1]",
            "table recipe rejects unsupported formulas before editing");
    }

    void test_page_images(const QString& directory)
    {
        PresentationBridge slides;
        slides.requestNew();
        Root root;
        root.module = "slides";
        AutomationBridge automation;
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        OfficeAiToolbox box(directory);
        box.reset();
        box.query("office_load_group", {{"group", "slides"}});
        const auto block = [](const QString& heading, const QString& text)
        {
            return QJsonObject{{"heading", heading}, {"text", text}};
        };
        QJsonArray content;
        content.append(QJsonObject{{"title", "让校园远离毒品"},
            {"subtitle", "认识风险 · 学会拒绝 · 主动求助"}, {"blocks", QJsonArray{}}});
        content.append(QJsonObject{{"title", "风险不是遥远的故事"}, {"subtitle", "从身边常见的误解开始"},
            {"layout", "grid"},
            {"blocks",
                QJsonArray{block("诱导",
                               "有人会用免费体验、减压或提神等说法接近你。保持警惕，不接受来源不明的物品。"),
                    block("识别", "不因包装新奇就认为安全，先离开现场，再向可信赖的成年人核实。"),
                    block("求助", "若感到被迫或已接触可疑物品，及时联系老师、家人或警方。")}}});
        content.append(QJsonObject{{"title", "面对邀请，选择会改变结果"}, {"layout", "comparison"},
            {"blocks",
                QJsonArray{block("冒险尝试", "轻信所谓的第一次没事，可能让健康和安全承受无法预估的后果。"),
                    block("明确拒绝", "直接说不，离开现场，并向可信赖的人说明发生了什么。")}}});
        content.append(QJsonObject{{"title", "拒绝，从一句明确的话开始"}, {"layout", "statement"},
            {"blocks", QJsonArray{block({}, "我不接受。现在离开，并告诉可信赖的人。")}}});
        const auto composed = execute(box, "office_compose_slides",
            {{"batchId", "visual-probe"}, {"targetPages", 7}, {"pages", content}});
        check(composed.value("complete").toBool() && slides.slideCount() == 4,
            "representative model pages pass the public compose tool");
        const QJsonArray process{QJsonObject{{"title", "需要帮助时，按顺序行动"},
            {"subtitle", "从离开现场到获得支持"}, {"layout", "steps"},
            {"blocks",
                QJsonArray{block("识别", "发现来源不明的物品或可疑邀请时，先判断环境是否安全。"),
                    block("拒绝", "明确说不，不接受、不携带，也不替他人保管。"),
                    block("离开", "远离现场，尽量与可信赖的同伴保持联系。"),
                    block("求助", "尽快告诉老师、家人或警方，并说明时间和地点。")}}}};
        const auto continued =
            execute(box, "office_compose_slides", {{"batchId", "visual-probe-process"}, {"pages", process}});
        check(continued.value("complete").toBool() && slides.slideCount() == 5,
            "a later model page appends a four-step flow without repeating the first segment");
        const QJsonArray editorial{
            QJsonObject{{"title", "识别风险，也要找到支持"}, {"subtitle", "识别与求助"}, {"layout", "hub"},
                {"blocks",
                    QJsonArray{block("先离开", "如果邀请让你感到不安，先到安全的地方。"),
                        block("再求助", "把经历告诉老师或家人，不必独自处理。")}}},
            QJsonObject{{"title", "把拒绝落实为四个动作"}, {"layout", "grid"},
                {"blocks",
                    QJsonArray{block("看清", "辨认来源不明的物品和异常要求。"),
                        block("说不", "用简短明确的话拒绝邀请。"), block("离开", "尽快与现场拉开距离。"),
                        block("报告", "告诉可信赖的人事情的经过。")}}}};
        const auto last_segment = execute(
            box, "office_compose_slides", {{"batchId", "visual-probe-editorial"}, {"pages", editorial}});
        check(last_segment.value("complete").toBool() &&
                last_segment.value("deckProgress").toObject().value("complete").toBool() &&
                slides.slideCount() == 7,
            "relationship and editorial pages complete the seven-page deck");
        const QString saved_path = directory + "/visual-flow.pptx";
        check(slides.saveTo(QUrl::fromLocalFile(saved_path)) &&
                wait_for(
                    [&]()
        {
            return !slides.busy() && QFileInfo::exists(saved_path);
        }),
            "generated seven-page presentation saves through the public bridge");
        const auto reopened = load_presentation_file(saved_path.toStdString());
        int saved_arrows = 0;
        if (reopened.error == PresentationError::None && reopened.scene.slides.size() == 7)
            for (const auto& shape : reopened.scene.slides[4].shapes)
                if (shape.geometry == "rightArrow" || shape.geometry == "leftArrow")
                    ++saved_arrows;
        check(reopened.error == PresentationError::None && reopened.scene.slides.size() == 7 &&
                saved_arrows == 2 && find_presentation_text(reopened.scene, "识别与求助").size() == 1,
            "saved PPTX reopens with relationship concept and four-step reading direction intact");
        const auto document = slides.document().value<RenderPresentationPtr>();
        const QString probe_directory = qEnvironmentVariable("MIRRORFLY_AI_VISUAL_PROBE_DIR");
        const QString parent = probe_directory.isEmpty() ? directory : probe_directory;
        PresentationImageExportOptions options;
        options.long_edge = 1280;
        PresentationImageExportProgress progress;
        const auto result =
            render_presentation_images(document, 0, {}, parent, "AI layout probe.pptx", options, progress);
        check(result.success && result.pages == 7 && progress.completed == 7,
            "seven AI pages render through the headless image export path");
        const QDir images(result.path);
        const QImage first(images.filePath("slide-001.png"));
        const QImage second(images.filePath("slide-002.png"));
        const QImage third(images.filePath("slide-003.png"));
        const QImage fourth(images.filePath("slide-004.png"));
        const QImage fifth(images.filePath("slide-005.png"));
        const QImage sixth(images.filePath("slide-006.png"));
        const QImage seventh(images.filePath("slide-007.png"));
        check(!first.isNull() && first.size() == QSize(1280, 720) && !second.isNull() && !third.isNull() &&
                !fourth.isNull() && !fifth.isNull() && !sixth.isNull() && !seventh.isNull() &&
                first != second && second != third && third != fourth && fourth != fifth && fifth != sixth &&
                sixth != seventh,
            "rendered AI layouts are complete images with distinct compositions");
        if (!probe_directory.isEmpty() && result.success)
        {
            const auto environment = presentation_render_environment();
            const QFont requested(QStringLiteral("Microsoft YaHei"));
            std::cout << "Font probe: installed=" << environment->installed_fonts.size()
                      << ", cjk=" << environment->cjk_fallback.toStdString()
                      << ", resolved=" << QFontInfo(requested).family().toStdString()
                      << ", chinese=" << QFontMetricsF(requested).inFontUcs4(0x4E2D) << '\n';
            std::cout << "Visual probe: " << result.path.toStdString() << '\n';
        }
    }

    QJsonArray add_node(const QString& id)
    {
        return {call("office_action",
            {{"op", "mindmap.execute"},
                {"args",
                    QJsonArray{
                        "createNode", QJsonObject{{"newId", id}, {"text", id}, {"x", 400}, {"y", 100}}}}})};
    }

    void test_stream_frames()
    {
        const auto chunk = [](const QJsonObject& delta, const QJsonValue& reason = QJsonValue())
        {
            return QByteArray("data: ") +
                json(QJsonObject{{"choices",
                    QJsonArray{QJsonObject{{"index", 0}, {"delta", delta}, {"finish_reason", reason}}}}}) +
                "\n\n";
        };
        OfficeAiStream complete;
        complete.append(chunk({{"tool_calls",
            QJsonArray{QJsonObject{{"index", 0}, {"id", "call_a"}, {"type", "function"},
                {"function", QJsonObject{{"name", "office_state"}, {"arguments", "{"}}}}}}}));
        complete.append(chunk({{"tool_calls",
                                  QJsonArray{QJsonObject{{"index", 0},
                                      {"function", QJsonObject{{"arguments", "\"module\":\"slides\"}"}}}}}}},
            "tool_calls"));
        complete.append("data: [DONE]\n\n");
        const auto valid_call = complete.response()
                                    .value("choices")
                                    .toArray()
                                    .first()
                                    .toObject()
                                    .value("message")
                                    .toObject()
                                    .value("tool_calls")
                                    .toArray()
                                    .first()
                                    .toObject();
        check(complete.complete() && valid_call.value("type") == "function" &&
                valid_call.value("function").toObject().value("arguments") == "{\"module\":\"slides\"}",
            "split real-model tool arguments assemble with their original function type");

        OfficeAiStream wrong_type;
        wrong_type.append(chunk({{"tool_calls",
            QJsonArray{QJsonObject{{"index", 0}, {"id", "call_a"}, {"type", "unknown"},
                {"function", QJsonObject{{"name", "office_state"}, {"arguments", "{}"}}}}}}}));
        check(wrong_type.failed() && !wrong_type.complete(),
            "unsupported streamed tool type cannot be relabeled as an executable function");

        OfficeAiStream changed_id;
        changed_id.append(chunk({{"tool_calls",
            QJsonArray{QJsonObject{{"index", 0}, {"id", "call_a"}, {"type", "function"},
                {"function", QJsonObject{{"name", "office_state"}, {"arguments", "{"}}}}}}}));
        changed_id.append(
            chunk({{"tool_calls",
                      QJsonArray{QJsonObject{{"index", 0}, {"id", "call_b"},
                          {"function", QJsonObject{{"arguments", "\"module\":\"slides\"}"}}}}}}},
                "tool_calls"));
        check(changed_id.failed() && !changed_id.complete(),
            "conflicting streamed tool IDs cannot silently replace an earlier call");
    }

    void test_network(const QString& directory)
    {
        CanvasBridge map(false);
        map.requestNew();
        Root root;
        root.module = "mindmap";
        AutomationBridge automation;
        automation.registerModule("mindmap", &map);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "mindmap"}})};
            if (turn == 1 || turn == 2)
            {
                check(map.snapshot().value("nodes").toList().size() == 1,
                    "partial streamed tool response has no document effects");
                return add_node("one");
            }
            return {};
        };
        network.fault = [](int turn)
        {
            Fault value;
            value.incomplete = turn == 1;
            return value;
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Original goal: create one node, then preserve it.");
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && !agent.resumable() &&
                map.snapshot().value("nodes").toList().size() == 2 && network.requests.size() == 4,
            "incomplete HTTP 200 retries only the model request and executes exactly once");

        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "mindmap"}})};
            if (turn == 1)
                return add_node("two");
            if (turn >= 6)
                check(json(body).contains("Keep two nodes; add a third after reconnect."),
                    "resume retains the full original goal and prior tool exchange");
            if (turn == 6)
                check(json(body).contains(QStringLiteral("保存了，请你继续").toUtf8()),
                    "resume passes the user's latest update to the model");
            if (turn == 6)
                return add_node("three");
            return {};
        };
        network.fault = [](int turn)
        {
            Fault value;
            if (turn >= 2 && turn < 6)
            {
                value.status = 503;
                value.error = QNetworkReply::ServiceUnavailableError;
            }
            return value;
        };
        agent.start("Keep two nodes; add a third after reconnect.");
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && agent.resumable() &&
                network.requests.size() == 6,
            "three bounded retries pause the task without discarding completed edits");
        check(map.snapshot().value("nodes").toList().size() == 3, "successful tools are not retried offline");
        agent.start(QStringLiteral("保存了，请你继续"));
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && !agent.resumable() &&
                map.snapshot().value("nodes").toList().size() == 4 && network.requests.size() == 8,
            "continue command resumes the existing objective without repeating earlier nodes");

        network.requests.clear();
        network.respond = [&](int, const QJsonObject& body) -> QJsonArray
        {
            check(!json(body).contains("Keep two nodes; add a third after reconnect.") &&
                    json(body).contains(QStringLiteral("多做一些，效果美观一点").toUtf8()),
                "new task preserves the document but does not inject previous conversation");
            return {};
        };
        network.fault = [](int)
        {
            return Fault{};
        };
        agent.start(QStringLiteral("多做一些，效果美观一点"));
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && network.requests.size() == 1,
            "refinement request completes in the same document context");

        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "mindmap"}})};
            if (turn == 2 || turn == 4)
                return add_node("after-user-edit");
            if (turn == 3)
            {
                check(json(body).contains("resume_observation_required") &&
                        map.snapshot().value("nodes").toList().size() == 4,
                    "external edits invalidate planned writes until current content is read");
                return {call("office_read", {{"module", "mindmap"}, {"view", "content"}})};
            }
            return {};
        };
        network.fault = [](int turn)
        {
            Fault value;
            if (turn == 1)
            {
                value.status = 401;
                value.error = QNetworkReply::AuthenticationRequiredError;
            }
            return value;
        };
        agent.start("Preserve user changes and add the remaining node.");
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && agent.resumable(),
            "authentication failure retains task");
        check(map.execute("rename", {{"text", "User changed this node"}}), "change document while paused");
        check(agent.configure("https://api.deepseek.com", "offline-model", "new-offline-key", "none"),
            "reconfiguring credentials preserves the paused task");
        agent.resume();
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && !agent.resumable() &&
                map.snapshot().value("nodes").toList().size() == 5,
            "re-observation unlocks continuation without losing user changes");

        for (int scenario = 0; scenario < 6; ++scenario)
        {
            network.requests.clear();
            const int initial = map.snapshot().value("nodes").toList().size();
            network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
            {
                if (turn == 0)
                    return {call("office_load_group", {{"group", "mindmap"}})};
                if (turn == 1 && scenario == 5)
                {
                    check(map.snapshot().value("nodes").toList().size() == initial,
                        "blank model completion cannot change the document");
                    return {};
                }
                if (turn == 1 || turn == 2)
                {
                    check(map.snapshot().value("nodes").toList().size() == initial,
                        "failed model response executes no speculative tools");
                    return add_node(QStringLiteral("recovered-%1").arg(scenario));
                }
                return {};
            };
            network.fault = [scenario](int turn)
            {
                Fault value;
                if (turn != 1)
                    return value;
                if (scenario == 0)
                {
                    value.status = 0;
                    value.error = QNetworkReply::RemoteHostClosedError;
                }
                else if (scenario == 1)
                {
                    value.status = 429;
                    value.error = QNetworkReply::UnknownContentError;
                }
                else if (scenario == 2)
                    value.truncated = true;
                else if (scenario == 3)
                    value.finish_reason = "insufficient_system_resource";
                else if (scenario == 4)
                    value.finish_reason = "aborted";
                else
                    value.empty_content = true;
                return value;
            };
            agent.start("Recover one unexecuted request and create exactly one additional node.");
            check(wait_for(
                      [&]()
            {
                return !agent.busy();
            }) && !agent.resumable() &&
                    map.snapshot().value("nodes").toList().size() == initial + 1,
                "disconnect, rate-limit, truncation and interrupted or blank model output retry without "
                "duplicate effects");
        }

        for (const QString& reason : {QStringLiteral("content_filter"), QStringLiteral("stop")})
        {
            network.requests.clear();
            const int initial = map.snapshot().value("nodes").toList().size();
            network.respond = [](int turn, const QJsonObject&) -> QJsonArray
            {
                if (turn == 0)
                    return {call("office_load_group", {{"group", "mindmap"}})};
                return add_node("not-applied");
            };
            network.fault = [reason](int turn)
            {
                Fault value;
                if (turn == 1 || (reason == "stop" && turn > 1))
                    value.finish_reason = reason;
                return value;
            };
            agent.start("Preserve the document when the model response cannot authorize a tool call.");
            const int expected_requests = reason == "content_filter" ? 2 : 4;
            check(wait_for(
                      [&]()
            {
                return !agent.busy();
            }) && agent.resumable() &&
                    network.requests.size() == expected_requests &&
                    map.snapshot().value("nodes").toList().size() == initial,
                "filtered output stops and repeated stop-with-tools exhausts repair without editing");
        }

        network.requests.clear();
        const int before_cancel = map.snapshot().value("nodes").toList().size();
        network.respond = [](int, const QJsonObject&)
        {
            return add_node("cancelled-reply");
        };
        network.fault = [](int)
        {
            Fault value;
            value.delay_ms = 150;
            return value;
        };
        agent.start("Keep this task after cancellation; do not run late replies.");
        check(!agent.configure("https://api.deepseek.com", "changed", "changed", "none"),
            "connection configuration cannot change under an active request");
        QTimer::singleShot(10, &agent, [&]()
        {
            agent.cancel();
        });
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && agent.resumable(),
            "cancel preserves a resumable original goal");
        bool drained = false;
        QTimer::singleShot(200, &agent, [&]()
        {
            drained = true;
        });
        wait_for([&]()
        {
            return drained;
        });
        check(map.snapshot().value("nodes").toList().size() == before_cancel,
            "late network completion after cancel cannot execute tools");
        network.fault = {};
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            check(json(body).contains("Keep this task after cancellation"), "cancel/resume retains intent");
            if (turn == 1)
                return {call("office_load_group", {{"group", "mindmap"}})};
            if (turn == 2)
                return add_node("after-cancel");
            return {};
        };
        agent.resume();
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && !agent.resumable() &&
                map.snapshot().value("nodes").toList().size() == before_cancel + 1,
            "resume after cancellation starts only the remaining complete exchange");

        network.requests.clear();
        const int before_malformed = map.snapshot().value("nodes").toList().size();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "mindmap"}})};
            if (turn == 2)
                check(json(body).contains("invalid tool-call envelope") &&
                        map.snapshot().value("nodes").toList().size() == before_malformed,
                    "malformed tool response gets a repair hint before any edit");
            if (turn == 1 || turn == 2)
                return add_node("after-json-repair");
            return {};
        };
        network.fault = [](int turn)
        {
            Fault value;
            value.malformed = turn == 1;
            return value;
        };
        agent.start("Recover one malformed tool response without repeating earlier edits.");
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && !agent.resumable() &&
                network.requests.size() == 4 &&
                map.snapshot().value("nodes").toList().size() == before_malformed + 1,
            "corrected model tool JSON executes exactly once after guided retry");

        network.requests.clear();
        network.respond = [](int, const QJsonObject&)
        {
            return add_node("must-not-run");
        };
        network.fault = [](int)
        {
            Fault value;
            value.malformed = true;
            return value;
        };
        agent.start("Malformed tool JSON must not edit anything.");
        check(wait_for(
                  [&]()
        {
            return !agent.busy();
        }) && agent.resumable() &&
                network.requests.size() == 3 &&
                map.snapshot().value("nodes").toList().size() == before_malformed + 1,
            "repeated malformed tool envelopes pause after two guided retries without executing any prefix");
        const int requests = static_cast<int>(network.requests.size());
        map.requestNew();
        map.resolveUnsaved("discard");
        agent.resume();
        check(!agent.busy() && agent.resumable() && network.requests.size() == requests,
            "resuming against another document identity is refused before sending a request");
    }
}

int run_office_ai_resilience_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    check(directory.isValid(), "isolated storage");
    test_page_recipes();
    test_structured_recipes();
    test_stream_frames();
    test_page_images(directory.path());
    test_cover_image(directory.path());
    test_pages(directory.path());
    test_human_page_updates(directory.path());
    test_large_segment(directory.path());
    test_network(directory.path());
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_office_ai_resilience_tests(argc, argv);
}

#include "office_ai_resilience_tests.moc"
