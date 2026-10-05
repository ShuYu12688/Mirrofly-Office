#include "ai_island_host.hpp"
#include "automation_bridge.hpp"
#include "bridge.hpp"
#include "canvas_bridge.hpp"
#include "document_routing.hpp"
#include "editor_tools.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_qml_scenario.hpp"
#include "pdf_export_bridge.hpp"
#include "presentation_bridge.hpp"
#include "presentation_image_export_bridge.hpp"
#include "spreadsheet_bridge.hpp"
#include "text_bridge.hpp"
#include "word_bridge.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <memory>
#include <mirrorfly/automation.hpp>
#include <mirrorfly/office_ai.hpp>

void qml_register_types_Mirrorfly_Native();

namespace
{
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
    bool wait_for(const std::function<bool()>& predicate)
    {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < 8000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QCoreApplication::processEvents();
        return predicate();
    }
    void run_sessions(const QString& directory)
    {
        using namespace mirrorfly;
        CanvasBridge map(false);
        int prompts = 0;
        QObject::connect(&map, &CanvasBridge::confirmUnsavedRequested, &map, [&]()
        {
            ++prompts;
        });
        map.requestNew();
        const auto root = map.viewData()["selectedId"].toString();
        check(map.active() && !map.modified(), "new map starts clean");
        check(map.execute("addChild", {{"text", QStringLiteral("研究问题")}}), "child command");
        const auto child = map.viewData()["selectedId"].toString();
        check(child != root && map.modified() && map.viewData()["nodeCount"].toInt() == 2,
            "child visible and dirty");
        check(!map.execute("rename", {}) && !map.execute("rename", {{"text", 42}}),
            "missing and wrong-type arguments rejected");
        map.requestHome();
        check(prompts == 1 && map.active() && map.locked(), "unsaved map home protected");
        map.resolveUnsaved("cancel");
        check(map.active() && !map.locked(), "cancel retains map");
        const auto file = QDir(directory).filePath("research.mfg");
        map.saveAs();
        map.selectSaveFile(QUrl::fromLocalFile(file));
        check(wait_for(
                  [&]()
        {
            return !map.locked();
        }) && !map.modified(),
            "map save completes");
        check(load_mindmap_file(file.toStdString()).document.nodes.size() == 2,
            "saved map is independently readable");
        map.undo();
        check(map.modified() && map.viewData()["nodeCount"].toInt() == 1, "undo remains dirty after save");
        map.redo();
        check(!map.modified() && map.viewData()["nodeCount"].toInt() == 2, "redo returns to saved identity");
        map.requestHome();
        check(!map.active(), "clean map returns home");
        map.requestOpen(QUrl::fromLocalFile(file));
        check(wait_for(
                  [&]()
        {
            return !map.locked();
        }) && map.active(),
            "map opens through session");
        map.selectNode(root);
        check(!map.execute("collapse", {}), "free graph refuses ambiguous tree-only collapse");
        check(map.viewData()["nodes"].toList().size() == 2, "graph preserves independent nodes");
        check(map.outline().contains(QStringLiteral("研究问题")), "graph outline includes nodes");
        check(map.execute("connect", {{"id", child}, {"toId", root}}), "graph permits a return connection");
        check(map.execute("moveNode", {{"id", child}, {"x", 420}, {"y", 230}}), "public node movement");
        map.undo();
        map.undo();
        check(map.setZoom(1.5) && map.zoom() == 1.5 && !map.modified(),
            "canvas zoom does not modify the saved document");
        const auto legacy_path = QDir(directory).filePath("legacy.mm");
        check(save_mindmap_file(legacy_path.toStdString(), make_mindmap(), {}).error ==
                MindMapError::UnsupportedType,
            "legacy save format is rejected");
        check(!map.requestOpen(QUrl::fromLocalFile(legacy_path)) && map.active(),
            "legacy open leaves current map intact");
        check(map.beginConnection("once", child) && map.connectNode(root) && map.connectionMode() == "off",
            "context connection automatically exits after one success");
        map.undo();
        check(map.beginConnection("continuous", child) && map.connectNode(root) &&
                map.connectionMode() == "continuous" && map.connectionFrom().isEmpty(),
            "toolbar connection remains active after success");
        check(map.beginConnection("once", root) && !map.connectNode(child) && map.connectionMode() == "once",
            "failed duplicate connection does not consume the pending operation");
        check(map.beginConnection("off", {}) && map.connectionFrom().isEmpty(),
            "connection cancellation clears its source");
        map.undo();

        check(map.beginConnection("once", child) && map.execute("deleteNode", {{"id", child}}) &&
                map.connectionMode() == "off",
            "deleting the source cancels a one-shot connection");
        map.undo();

        const auto source = QDir(directory).filePath("source.pdf");
        {
            QPdfWriter writer(source);
            writer.setResolution(72);
            QPainter painter(&writer);
            painter.drawText(30, 50, "Page one searchable text");
            writer.newPage();
            painter.drawText(30, 50, "Page two searchable text");
        }
        CanvasBridge pdf(true);
        check(pdf.requestOpen(QUrl::fromLocalFile(source)), "PDF open accepted");
        check(wait_for(
                  [&]()
        {
            return !pdf.locked() && pdf.previewReady();
        }) && pdf.active(),
            "PDF loads and renders without an application window");
        check(pdf.viewData()["pageCount"].toInt() == 2 && !pdf.image().isNull(), "two-page preview");
        check(pdf.execute("note", {{"text", QStringLiteral("需要核对来源")}, {"x", 0.1}, {"y", 0.2}}),
            "native note command");
        check(pdf.viewData()["annotations"].toList().size() == 1, "annotation snapshot updates");
        pdf.saveAs();
        pdf.selectSaveFile(QUrl::fromLocalFile(source));
        check(!pdf.locked() && pdf.modified() && !pdf.message().isEmpty(), "source overwrite rejected");
        const auto output = QDir(directory).filePath("annotated.pdf");
        pdf.saveAs();
        pdf.selectSaveFile(QUrl::fromLocalFile(output));
        check(wait_for(
                  [&]()
        {
            return !pdf.locked();
        }) && !pdf.modified(),
            "PDF copy saved");
        const auto reopened = load_pdf_file(output.toStdString());
        check(reopened.error == PdfError::None && reopened.document.pages[0].original_annotations.size() == 1,
            "PDF note roundtrip");
        pdf.selectPage(1);
        check(pdf.execute("rotate", {{"turns", 1}}), "rotate selected page");
        pdf.undo();
        check(!pdf.modified(), "PDF undo restores saved state");
        pdf.requestHome();
        check(!pdf.active(), "PDF returns home and releases scene");
    }
    void run_composition(const QDir& source, bool ai_scenario, bool ai_multiformat)
    {
        using namespace mirrorfly;
        QFile file(source.filePath("config/theme.json"));
        check(file.open(QIODevice::ReadOnly), "theme opens");
        const auto theme = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        InterfaceBridge app(theme);
        TextEditorBridge text;
        SpreadsheetBridge sheets;
        PresentationBridge slides;
        WordBridge word;
        CanvasBridge pdf(true), mindmap(false);
        EditorTools tools;
        PdfExportBridge exporter(theme);
        PresentationImageExportBridge image_exporter(theme);
        image_exporter.registerSource([&slides]()
        {
            return PresentationImageExportSource{slides.document().value<RenderPresentationPtr>(),
                slides.currentSlide(), slides.documentName(), {}};
        });
        exporter.registerSource("text", [&text]()
        {
            return text.pdfSource();
        });
        connect_document_routes(app, text, slides, sheets, word, &pdf, &mindmap);
        QTemporaryDir ai_directory;
        office_ai_test::Transport network(check);
        OfficeAiAgent ai_agent(
            nullptr, &network, ai_directory.filePath("diagnostics.jsonl"), ai_directory.path());
        AiIslandHost ai_island(ai_agent);
        QQmlEngine engine;
        auto* context = engine.rootContext();
        context->setContextProperty("appBridge", &app);
        context->setContextProperty("textEditor", &text);
        context->setContextProperty("spreadsheet", &sheets);
        context->setContextProperty("presentation", &slides);
        context->setContextProperty("wordEditor", &word);
        context->setContextProperty("editorTools", &tools);
        context->setContextProperty("pdfEditor", &pdf);
        context->setContextProperty("mindmapEditor", &mindmap);
        context->setContextProperty("pdfExporter", &exporter);
        context->setContextProperty("imageExporter", &image_exporter);
        context->setContextProperty("aiAgent", &ai_agent);
        context->setContextProperty("aiIsland", &ai_island);
        QStringList warnings;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors)
        {
            for (const auto& error : errors)
                warnings.push_back(error.toString());
        });
        QQmlComponent component(&engine, QUrl::fromLocalFile(source.filePath("ui/Main.qml")));
        std::unique_ptr<QObject> root(component.createWithInitialProperties({{"visible", false}}));
        if (!root)
        {
            std::cerr << component.errorString().toStdString();
            check(false, "main QML composition");
            return;
        }
        check(!root->property("visible").toBool(), "composition stays invisible");
        auto* timeline = static_cast<OfficeAiTimeline*>(ai_agent.trace());
        int timeline_resets = 0;
        QObject::connect(timeline, &QAbstractItemModel::modelReset, root.get(), [&]()
        {
            ++timeline_resets;
        });
        QVariantMap chat_row{{"kind", "user"}, {"title", "You"}, {"detail", "Continue this document"},
            {"state", "done"}, {"elapsedMs", 0}, {"time", "12:00:00"}};
        timeline->append(chat_row);
        chat_row.insert("kind", "tool");
        chat_row.insert("title", "office_workspace");
        chat_row.insert("state", "running");
        timeline->append(chat_row);
        QCoreApplication::processEvents();
        auto* chat = root->findChild<QObject*>("aiConversationList");
        auto* ball = root->findChild<QObject*>("aiFloatingBall");
        check(!chat && !ball && timeline->rowCount() == 2,
            "AI timeline remains in the agent without an in-window floating chat");
        chat_row.insert("state", "done");
        timeline->update(1, chat_row);
        QCoreApplication::processEvents();
        check(timeline_resets == 0, "tool status updates do not reset the conversation model");
        {
            QQmlComponent row_component(
                &engine, QUrl::fromLocalFile(source.filePath("ui/components/AiTraceRow.qml")));
            chat_row.insert("detail", QString(32000, 'x'));
            std::unique_ptr<QObject> row(row_component.createWithInitialProperties(
                {{"theme", theme}, {"entry", chat_row}, {"width", 420}}));
            check(row != nullptr, "AI trace row constructs independently without a visible window");
            if (row)
            {
                auto* loader = row->findChild<QObject*>("aiTraceDetailLoader");
                check(loader && !loader->property("item").value<QObject*>(),
                    "collapsed tool detail creates no large text layout");
                row->setProperty("expanded", true);
                QCoreApplication::processEvents();
                check(loader && loader->property("item").value<QObject*>(),
                    "explicit expansion loads the tool detail");
                row->setProperty("expanded", false);
                QCoreApplication::processEvents();
                check(loader && !loader->property("item").value<QObject*>(),
                    "collapsing tool detail releases its text control");
            }
        }
        root->setProperty("aiDebugOpen", false);
        AutomationBridge automation;
        automation.registerModule("app", &app);
        automation.registerModule("text", &text);
        automation.registerModule("word", &word);
        automation.registerModule("sheets", &sheets);
        automation.registerModule("slides", &slides);
        automation.registerModule("pdf", &pdf);
        automation.registerModule("mindmap", &mindmap);
        automation.registerModule("export", &exporter);
        automation.registerModule("images", &image_exporter);
        automation.setUiRoot(root.get());
        const auto api = [](const QString& module, const QString& action, const QJsonArray& args)
        {
            const auto state = QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot())).object();
            const auto request = QJsonDocument(QJsonObject{{"module", module}, {"action", action},
                                                   {"args", args}, {"expectedRevision", state["revision"]}})
                                     .toJson(QJsonDocument::Compact);
            return QJsonDocument::fromJson(QByteArray::fromStdString(office_execute(request.toStdString())))
                .object();
        };
        const auto check_zoom = [&](const QString& module, const QString& page_name, DocumentView& bridge)
        {
            check(wait_for(
                      [&]()
            {
                return QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot()))
                    .object()["ui"]
                    .toObject()["ready"]
                    .toBool();
            }),
                "zoom waits for settled input");
            const auto before =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot())).object();
            const bool dirty = bridge.property("modified").toBool();
            check(api(module, "setZoom", {2.0})["ok"].toBool(), "zoom reachable through public catalog");
            auto* page = root->findChild<QObject*>(page_name);
            check(page && page->property("zoom").toDouble() == 2.0, "API zoom updates composed QML page");
            const auto after = QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot())).object();
            check(before["revision"] != after["revision"] &&
                    after["modules"].toObject()[module].toObject()["zoom"].toDouble() == 2.0,
                "zoom participates in observable automation revision");
            check(!api(module, "setZoom", {0.0})["ok"].toBool() && bridge.zoom() == 2.0,
                "invalid zoom cannot alter view state");
            if (page)
                check(QMetaObject::invokeMethod(page, "zoomRequested", Q_ARG(double, 1.75)) &&
                        bridge.zoom() == 1.75,
                    "UI zoom uses the same bridge state");
            check(bridge.property("modified").toBool() == dirty, "zoom never dirties document");
            bridge.setZoom(1);
        };
        app.initialize();
        if (ai_scenario)
        {
            if (ai_multiformat)
                run_office_ai_multiformat_scenario(ai_agent, network, ai_directory.path(), check);
            else
                run_office_ai_qml_scenario(ai_agent, network, ai_directory.path(), check,
                    QCoreApplication::arguments().contains("--ai-batched"));
            check(!root->property("visible").toBool(), "AI scenario never opens an application window");
            for (const auto& warning : warnings)
                std::cerr << warning.toStdString() << '\n';
            check(warnings.isEmpty(), "complex AI task has no QML warnings");
            return;
        }
        const auto contract =
            QJsonDocument::fromJson(QByteArray::fromStdString(office_ai_contract())).object();
        const auto interfaces = contract.value("modules").toObject();
        QFile contract_file(source.filePath("build/office-ai-contract.json"));
        const auto contract_bytes = QJsonDocument(contract).toJson(QJsonDocument::Indented);
        check(contract_file.open(QIODevice::WriteOnly) &&
                contract_file.write(contract_bytes) == contract_bytes.size(),
            "the current callable interface inventory is independently inspectable as JSON");
        contract_file.close();
        for (const auto* name : {"app", "word", "slides", "export", "images"})
            for (const auto& value : interfaces.value(name).toArray())
            {
                const auto action = value.toObject();
                check(action.value("available").toBool(),
                    "each catalog action resolves against the real GUI session");
                for (const auto& parameter : action.value("parameters").toArray())
                    check(!parameter.toObject().value("name").toString().startsWith("arg"),
                        "real contract arguments have declared semantic names, not inferred positions");
            }
        check(contract.value("schemas")
                    .toObject()
                    .value("word")
                    .toObject()
                    .value("formats")
                    .toObject()
                    .contains("cellFill") &&
                contract.value("schemas").toObject().value("slides").toObject().contains("background"),
            "live contract includes Word formats and slide-level editing schemas");
        const auto previews = api("slides", "templatePreviews", {QJsonObject{}}).value("result").toObject();
        check(previews.value("kind") == "templateMetadata" &&
                previews.value("templates").toObject().size() == 4 &&
                previews.value("templates")
                        .toObject()
                        .value("researchStudio")
                        .toObject()
                        .value("width")
                        .toDouble() > 0,
            "AI template discovery returns serializable metadata instead of GUI rendering pointers");
        int invalidations = 0;
        const auto subscription = office_subscribe_changes([&](const std::string&)
        {
            ++invalidations;
        });
        check(api("app", "setHomeView", {"recent", "报告", "writer"})["ok"].toBool() &&
                root->property("searchQuery").toString() == "报告" &&
                root->property("selectedCategory").toString() == "writer",
            "home interface edits the GUI's actual search and category bindings");
        QCoreApplication::processEvents();
        const auto previous_invalidations = invalidations;
        root->setProperty("searchQuery", "GUI query");
        QCoreApplication::processEvents();
        check(invalidations > previous_invalidations,
            "GUI-only home changes reach the public invalidation feed without snapshot polling");
        check(!api("app", "setHomeView", {"unknown", "", "writer"})["ok"].toBool(),
            "unknown home routes cannot change composed state");
        check(api("app", "setHomeView", {"ai", "", "all"})["ok"].toBool(),
            "AI configuration opens through the public home-view route");
        auto* ai_page = root->findChild<QObject*>(QStringLiteral("aiModelSettingsPage"));
        check(ai_page && ai_page->property("visible").toBool(),
            "the AI navigation route displays its full right-side page in the shared composition");
        check(api("app", "setHomeView", {"home", "", "all"})["ok"].toBool(), "home view resets");

        check(api("app", "new", {"slides"})["ok"].toBool(), "presentation opens through public app router");
        check(wait_for(
                  [&]()
        {
            return QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot()))
                .object()["ui"]
                .toObject()["ready"]
                .toBool();
        }),
            "presentation interface waits for the actual GUI readiness gate");
        check(api("slides", "applyEdit", {"addText", QJsonObject{{"text", "Shared slide state"}}})["ok"]
                  .toBool(),
            "presentation editing enters the existing session transaction");
        auto* slide_page = root->findChild<QObject*>("PresentationPage");
        check(slide_page && slide_page->property("selection").toMap().value("text") == "Shared slide state" &&
                slide_page->property("selectedShape").toInt() == slides.selectedShape(),
            "AI slide edits synchronously update the GUI selection and inspector bindings");
        check(api("slides", "undo", {})["ok"].toBool() && !slides.modified(),
            "interface edits share the GUI undo history");
        check(api("slides", "showHome", {})["ok"].toBool(), "unmodified slide session returns home");
        check(api("app", "new", {"mindmap"})["ok"].toBool(), "automation new uses real router");
        check(mindmap.active() && !sheets.active() && !word.active(), "mindmap navigation routes");
        const auto graph_before = mindmap.outline();
        check_zoom("mindmap", "CanvasPage", mindmap);
        check(graph_before == mindmap.outline(), "zoom preserves mindmap geometry and content");
        check(api("mindmap", "beginConnection", {"continuous", ""})["ok"].toBool(),
            "connection mode exposed through automation");
        auto* canvas_page = root->findChild<QObject*>("CanvasPage");
        check(canvas_page && canvas_page->property("connectionMode").toString() == "continuous",
            "connection mode synchronized to QML");
        check(api("mindmap", "beginConnection", {"off", ""})["ok"].toBool(), "API can end connection mode");
        mindmap.requestHome();
        app.requestCreate("sheets");
        check(sheets.active() && !mindmap.active(), "canvas to sheets route");
        check_zoom("sheets", "SpreadsheetPage", sheets);
        auto* grid = root->findChild<QObject*>("spreadsheetGrid");
        if (grid)
        {
            QQmlExpression width_expression(qmlContext(grid), grid, "columnWidthProvider(0)");
            const double normal_width = width_expression.evaluate().toDouble();
            sheets.setZoom(2);
            check(width_expression.evaluate().toDouble() == normal_width * 2, "grid geometry follows zoom");
            sheets.setZoom(1);
        }
        else
            check(false, "spreadsheet grid available");
        sheets.selectAddress("B2:C3");
        check(sheets.formatSelection({{"bold", true}, {"fill", "#EAF2EC"}}),
            "visible composition style update");
        check(sheets.resizeSelection(true, 28), "visible composition resize");
        sheets.undo();
        sheets.undo();
        sheets.requestHome();
        auto* intro = root->findChild<QObject*>("featureIntro");
        check(word.requestOpen(QUrl::fromLocalFile(source.filePath("tests/fixtures/word-python-docx.docx"))),
            "Word opens through the composed loading workflow");
        check(intro && intro->property("running").toBool() && intro->property("loadingMode").toBool() &&
                intro->property("kind").toString() == "word" &&
                !intro->property("loadingComplete").toBool() &&
                intro->property("loadingProgress").toReal() == 0,
            "Word loading overlay starts before the worker finishes, with real initial progress");
        check(wait_for(
                  [&]()
        {
            return word.loadingProgress() == 1;
        }),
            "composed Word loading waits for the prepared editor attachment");
        check(intro && intro->property("loadingComplete").toBool() &&
                intro->property("loadingProgress").toReal() == 1 &&
                intro->property("loadingStatus").toString() == word.loadingStage(),
            "the hidden composition receives actual Word completion and stage text");
        const auto loading_state = QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot()))
                                       .object()["modules"]
                                       .toObject()["word"]
                                       .toObject();
        check(loading_state["loadingProgress"].toDouble() == 1 &&
                loading_state["loadingStage"].toString() == word.loadingStage(),
            "automation exposes the same loading progress and stage as the Word GUI");
        check(wait_for(
                  [&]()
        {
            return intro && !intro->property("running").toBool();
        }),
            "Word activation does not restart a decorative animation after loading");
        check(word.requestOpen(QUrl::fromLocalFile(source.filePath("tests/fixtures/missing-word.docx"))),
            "composed Word loading accepts a missing-file failure case");
        check(wait_for(
                  [&]()
        {
            return !word.locked();
        }) && word.active() &&
                intro && !intro->property("running").toBool() && !word.message().isEmpty(),
            "failed Word loading closes the overlay and preserves the preceding document");
        word.requestHome();
        app.requestCreate("word");
        check(word.active() && !sheets.active(), "Word navigation route");
        check(wait_for(
                  [&]()
        {
            return word.statistics()["characters"].toInt() == 0;
        }),
            "new Word document replaces the previous prepared editor before inspection");
        const auto word_before = word.snapshot();
        check_zoom("word", "WordPage", word);
        check(word_before == word.snapshot(), "view zoom preserves Word model");
        word.setZoom(2);
        auto* paper = root->findChild<QObject*>("wordPaper");
        check(paper && paper->property("scale").toDouble() == 2.0, "Word paper uses display transform");
        word.setZoom(1);
        check(wait_for(
                  [&]()
        {
            return QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot()))
                .object()["ui"]
                .toObject()["ready"]
                .toBool();
        }),
            "composition becomes ready for automation");
        check(api("word", "insertText", {0, 0, QStringLiteral("接口写入正文")})["ok"].toBool(),
            "AI insertion uses visible Word document");
        check(word.snapshot()["plainText"].toString() == QStringLiteral("接口写入正文"),
            "Word snapshot returns inserted text");
        check(api("word", "format", {0, 6, "lineSpacing", QJsonObject{{"rule", 1}, {"value", 18.5}}})["ok"]
                    .toBool() &&
                word.inspect(0).value("lineSpacingPoints").toDouble() == 18.5,
            "object-valued Word formatting reaches the real document through the common executor");
        auto* word_page = root->findChild<QObject*>("WordPage");
        check(word_page &&
                word_page->property("selection").toMap().value("lineSpacingPoints").toDouble() == 18.5,
            "Word format execution refreshes the same GUI inspector state");
        auto* word_area = root->findChild<QObject*>("wordEditor");
        QCoreApplication::processEvents();
        const auto before_selection = invalidations;
        if (word_area)
            word_area->setProperty("cursorPosition", 2);
        QCoreApplication::processEvents();
        check(word_area && invalidations > before_selection,
            "GUI cursor-only changes notify future bindings without editing document content");
        check(api("app", "new", {"mindmap"})["ok"].toBool() && word.locked() && !mindmap.active(),
            "AI switch protects unsaved Word");
        check(api("word", "resolveUnsaved", {"cancel"})["ok"].toBool() && word.active() && !word.locked(),
            "AI resolves matching dialog without bypassing its ownership");
        check(wait_for(
                  [&]()
        {
            return QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot()))
                .object()["ui"]
                .toObject()["ready"]
                .toBool();
        }),
            "resolved unsaved dialog releases the visible interaction gate");
        check(api("app", "new", {"writer"})["ok"].toBool() && word.locked(),
            "next API navigation remains protected");
        check(api("word", "resolveUnsaved", {"invalid"})["error"] == "invalid_arguments" && word.locked(),
            "unknown unsaved decision preserves the pending dialog");
        check(api("word", "resolveUnsaved", {"discard"})["ok"].toBool(), "discard switches to text");
        check(wait_for(
                  [&]()
        {
            return QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot()))
                .object()["ui"]
                .toObject()["ready"]
                .toBool();
        }),
            "text editor becomes ready");
        check(api("text", "replaceContent", {QStringLiteral("接口替换正文")})["ok"].toBool(),
            "text replacement reloads the editor through its public document revision");
        check_zoom("text", "TextEditorPage", text);
        text.setZoom(2);
        auto* text_area = root->findChild<QObject*>("textEditorArea");
        check(text_area && text_area->property("scale").toDouble() == 2.0 &&
                text.content() == QStringLiteral("接口替换正文"),
            "text uses display transform without rewriting content");
        text.setZoom(1);
        check(
            !text.replaceContent(QString(QChar(0xD800))) && text.content() == QStringLiteral("接口替换正文"),
            "invalid Unicode cannot replace existing text");
        QCoreApplication::processEvents();
        QTemporaryDir exported;
        check(wait_for(
                  [&]()
        {
            return !root->property("editorInteractionAllowed").isNull() &&
                QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot()))
                    .object()["ui"]
                    .toObject()["ready"]
                    .toBool();
        }),
            "export waits for settled text input");
        check(api("export", "start",
                  {"text", QUrl::fromLocalFile(exported.filePath("api.pdf")).toString(), QJsonObject{}})["ok"]
                  .toBool(),
            "export is reachable through the same public automation catalog");
        check(wait_for(
                  [&]()
        {
            return !exporter.busy();
        }) && exporter.snapshot()["success"].toBool(),
            "automation export reaches completed state");
        check(api("app", "open", {QUrl::fromLocalFile(exported.filePath("api.pdf")).toString()})["ok"]
                    .toBool() &&
                text.locked(),
            "opening exported PDF still protects unsaved text");
        check(api("text", "resolveUnsaved", {"discard"})["ok"].toBool(),
            "PDF navigation resolves text transaction");
        check(wait_for(
                  [&]()
        {
            return pdf.active() && !pdf.locked();
        }),
            "PDF opens through real router");
        check_zoom("pdf", "CanvasPage", pdf);
        check(api("app", "new", {"markdown"})["ok"].toBool(), "Markdown reuses text session routing");
        check(text.markdown(), "Markdown mode is active for zoom regression");
        const auto markdown_before = text.content();
        check_zoom("text", "TextEditorPage", text);
        check(text.content() == markdown_before, "Markdown view zoom preserves source content");
        auto* main_window = qobject_cast<QQuickWindow*>(root.get());
        int background_requests = 0;
        int exit_approvals = 0;
        QObject::connect(&app, &InterfaceBridge::hideMainRequested, root.get(), [&]()
        {
            ++background_requests;
            main_window->hide();
        });
        QObject::connect(&app, &InterfaceBridge::restoreMainRequested, main_window, &QQuickWindow::show);
        QObject::connect(&app, &InterfaceBridge::quitApproved, root.get(), [&]()
        {
            ++exit_approvals;
        });
        app.setResidentEnabled(true);
        text.updateText(text.content() + QStringLiteral("\n保留后台文档"));
        main_window->show();
        main_window->close();
        check(background_requests == 1 && !main_window->isVisible() && text.modified() && !text.locked(),
            "the window close button hides a resident session without discarding its document");
        app.requestQuit();
        check(wait_for(
                  [&]()
        {
            return text.locked();
        }) && exit_approvals == 0,
            "explicit exit still asks about unsaved content");
        check(api("text", "resolveUnsaved", {"cancel"})["ok"].toBool() && text.modified() &&
                exit_approvals == 0,
            "canceling explicit exit keeps the resident session");
        app.requestQuit();
        check(wait_for(
                  [&]()
        {
            return text.locked();
        }),
            "another explicit exit can be requested");
        check(api("text", "resolveUnsaved", {"discard"})["ok"].toBool() && exit_approvals == 1,
            "explicit exit completes only after the document decision");
        app.setResidentEnabled(false);
        check(office_unsubscribe_changes(subscription), "composition test releases its public observer");
        for (const auto& warning : warnings)
            std::cerr << warning.toStdString() << '\n';
        check(warnings.isEmpty(), "all composed pages have no QML runtime warnings");
    }
}

int run_canvas_ui_tests(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("Mirrorfly Canvas Contract Test");
    qml_register_types_Mirrorfly_Native();
    QTemporaryDir directory;
    const bool ai_multiformat = application.arguments().contains("--ai-multiformat");
    const bool ai_scenario = ai_multiformat || application.arguments().contains("--ai-scenario") ||
        application.arguments().contains("--ai-batched");
    if (!ai_scenario)
        run_sessions(directory.path());
    run_composition(QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY)), ai_scenario, ai_multiformat);
    return failures == 0 ? 0 : 1;
}
int main(int argc, char* argv[])
{
    return run_canvas_ui_tests(argc, argv);
}
