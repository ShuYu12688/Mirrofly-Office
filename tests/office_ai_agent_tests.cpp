#include "automation_bridge.hpp"
#include "canvas_bridge.hpp"
#include "office_ai_adversarial_scenario.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_context.hpp"
#include "office_ai_request.hpp"
#include "office_ai_test_transport.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"
#include "pdf_export_bridge.hpp"
#include "presentation_bridge.hpp"
#include "presentation_image_export_bridge.hpp"
#include "text_bridge.hpp"
#include "word_bridge.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/presentation_storage.hpp>
#include <mirrorfly/spreadsheet.hpp>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cstring>
#include <iostream>

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

    using office_ai_test::call;
    using office_ai_test::json;
    using office_ai_test::runtime;
    using office_ai_test::Transport;

    class Root final : public QObject
    {
        Q_OBJECT
    public:
        QString module = "home";
        mirrorfly::CanvasBridge* map = nullptr;
        mirrorfly::PresentationBridge* slides = nullptr;
        bool invalid_state = false;
        bool ignore_new = false;
        Q_INVOKABLE bool automationReady() const
        {
            return true;
        }
        Q_INVOKABLE QVariantMap automationState() const
        {
            if (invalid_state)
                return {};
            if (module == "slides" && slides && !slides->active())
                return {{"module", "home"}, {"pendingInput", false}};
            return {{"module", module}, {"pendingInput", false}};
        }
        Q_INVOKABLE void requestCreate(const QString& kind)
        {
            if (ignore_new)
                return;
            if (map && kind == "mindmap")
            {
                check(kind == "mindmap", "native mindmap creation never routes to slides");
                map->requestNew();
            }
            else if (slides)
                slides->requestNew();
            module = kind;
        }
    };

    void await_agent(mirrorfly::OfficeAiAgent& agent)
    {
        QElapsedTimer clock;
        clock.start();
        while (agent.busy() && clock.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        check(!agent.busy(), "offline agent completes");
    }

    void test_word_schema()
    {
        mirrorfly::WordBridge word;
        Root root;
        root.module = "word";
        mirrorfly::AutomationBridge automation;
        automation.registerModule("word", &word);
        automation.setUiRoot(&root);
        mirrorfly::OfficeAiToolbox toolbox;
        toolbox.focusWorkspace();
        const auto format = toolbox.query("office_schema", {{"module", "word"}, {"name", "size"}});
        const auto qualified = toolbox.query("office_schema", {{"module", "word"}, {"name", "word.size"}});
        check(format == qualified && format.value("ok").toBool() &&
                format.value("invocation").toObject().value("op") == "word.format" &&
                format.value("invocation").toObject().value("args").toObject().value("action") == "size",
            "Word format schema tolerates its own module prefix and names the real public method");
        check(!toolbox.query("office_schema", {{"module", "word"}, {"name", "sheets.size"}})
                  .value("ok")
                  .toBool(),
            "schema normalization cannot cross module boundaries");
    }

    void test_agent(const QString& directory)
    {
        using namespace mirrorfly;
        CanvasBridge map(false);
        Root root;
        root.map = &map;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("mindmap", &map);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            check(!json(body).contains("memory_search"), "memory tools are never sent");
            if (turn == 0)
                return {call("office_new", {{"kind", "mindmap"}})};
            if (turn == 1)
                return {call("office_action",
                    {{"op", "mindmap.execute"},
                        {"args",
                            QJsonArray{"addChild",
                                QJsonObject{
                                    {"id", "root"}, {"newId", "native-child"}, {"text", "Native child"}}}}})};
            if (turn == 2)
                return {call("office_save", {{"title", "Native map"}})};
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Create a native mindmap with one child and save it.");
        await_agent(agent);
        check(!agent.resumable() && QFile::exists(directory + "/Native map.mfg") &&
                map.snapshot().value("nodes").toList().size() == 2,
            "native mindmap create, edit and save complete through public tools");
        check(agent.activity() == "completed" && agent.islandStatus() == QStringLiteral("完成了"),
            "island completion is emitted only after the verified successful task");
        const auto count = network.requests.size();
        check(agent.usageStats().value("input").toLongLong() == count * 120 &&
                agent.usageStats().value("output").toLongLong() == count * 30 &&
                agent.usageStats().value("total").toLongLong() == count * 150,
            "dashboard usage counts provider receipts without adding cached tokens twice");
        agent.start(QStringLiteral("继续"));
        check(network.requests.size() == count, "no historical task is restored after completion");
        check(agent.usageStats().value("total").toLongLong() == count * 150,
            "starting another turn does not reset session usage");
    }

    void test_presentation(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            const auto revision = runtime().value("revision");
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}, {"expectedRevision", revision}})};
            if (turn >= 2 && turn < 6)
            {
                QJsonArray steps;
                const auto edit = [&](const QString& name, const QJsonObject& options)
                {
                    steps.append(QJsonObject{
                        {"module", "slides"}, {"action", "applyEdit"}, {"args", QJsonArray{name, options}}});
                };
                if (turn > 2)
                    edit("addSlide", {{"layout", "blank"}});
                edit("background", {{"color", "#F3F6FC"}});
                edit("addText",
                    {{"text", QStringLiteral("Page %1").arg(turn - 1)}, {"x", 48}, {"y", 38}, {"width", 760},
                        {"height", 64}});
                edit("formatText", {{"fontSize", 34}, {"bold", true}, {"textColor", "#10233F"}});
                return {call("office_batch", {{"expectedRevision", revision}, {"steps", steps}})};
            }
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl");
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Make four slides with titles and a consistent style, then finish.");
        await_agent(agent);
        check(agent.answer() == "Finished" && slides.slideCount() == 4 && network.requests.size() == 7,
            "four styled pages execute through real agent batches without revision retries");
        qsizetype peak = 0;
        for (const auto& request : network.requests)
            peak = qMax(peak, request.size());
        std::cout << "Four-page offline agent: requests=" << network.requests.size()
                  << ", peak request bytes=" << peak << '\n';

        // Modify the same document after discovery errors; never route a style request to new/addSlide.
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            const auto revision = runtime().value("revision");
            const auto payload = json(body);
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
            {
                check(payload.contains("signatureLookup") && payload.contains("saveTo") &&
                        payload.contains("semanticPage"),
                    "group supplies compact action index and lazy signature lookup");
                return {call("office_schema", {{"module", "slides"}, {"name", "replaceImage"}})};
            }
            if (turn == 2)
            {
                check(payload.contains("public_action") && payload.contains("parameters") &&
                        !payload.contains("unknown_edit_name"),
                    "file action can be discovered through the same lazy schema tool");
                return {call("office_schema", {{"module", "slides"}, {"name", "formatText"}})};
            }
            if (turn >= 3 && turn < 7)
            {
                const int page = turn - 3;
                QString id;
                for (const auto& value : slides.semanticPage(page, 0, 16).value("nodes").toList())
                    if (value.toMap().value("text") == QStringLiteral("Page %1").arg(page + 1))
                        id = value.toMap().value("id").toString();
                check(!id.isEmpty(), "existing requested title located by actual content");
                return {call("office_batch",
                    {{"expectedRevision", revision},
                        {"steps",
                            QJsonArray{QJsonObject{{"module", "slides"}, {"action", "selectObject"},
                                           {"args", QJsonArray{id}}},
                                QJsonObject{{"module", "slides"}, {"action", "applyEdit"},
                                    {"args", QJsonArray{"formatText", QJsonObject{{"fontSize", 30}}}}}}}})};
            }
            return {};
        };
        const auto identity = office_ai_document_key(runtime());
        agent.start("Optimize the existing four pages once, preserve content and page count, then finish.");
        await_agent(agent);
        check(agent.answer() == "Finished" && slides.slideCount() == 4 &&
                office_ai_document_key(runtime()) == identity && network.requests.size() == 8,
            "schema recovery and four-page modification converge without recreating the document");
        for (int page = 0; page < 4; ++page)
        {
            bool found = false;
            for (const auto& value : slides.semanticPage(page, 0, 16).value("nodes").toList())
                if (value.toMap().value("text") == QStringLiteral("Page %1").arg(page + 1))
                {
                    found = true;
                    check(value.toMap().value("style").toMap().value("fontSize").toInt() == 30,
                        "style changed on original objects with text preserved");
                }
            check(found, "original title remains present after modification");
        }

        network.requests.clear();
        QString applied_generation;
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 2)
                applied_generation = slides.editGeneration();
            if (turn > 5)
                return {};
            return {call("office_action",
                {{"module", "slides"}, {"action", "applyEdit"},
                    {"args", QJsonArray{"formatText", QJsonObject{{"fontSize", 32}}}},
                    {"expectedRevision", runtime().value("revision")}})};
        };
        agent.start("Exercise repeated identical style recovery.");
        await_agent(agent);
        check(network.requests.size() == 5 && slides.editGeneration() == applied_generation &&
                agent.answer().isEmpty(),
            "identical style retries do not mutate undo history or continue indefinitely");

        PdfExportBridge pdf({});
        pdf.registerSource("slides", [&]()
        {
            return slides.pdfSource();
        });
        PresentationImageExportBridge images({});
        images.registerSource([&]()
        {
            return PresentationImageExportSource{slides.document().value<RenderPresentationPtr>(),
                slides.currentSlide(), slides.documentName(), {}};
        });
        automation.registerModule("export", &pdf);
        automation.registerModule("images", &images);
        const QString saved = directory + "/agent-saved.pptx";
        QImage asset(16, 16, QImage::Format_RGB32);
        asset.fill(Qt::blue);
        const QString asset_path = directory + "/asset.png";
        check(asset.save(asset_path), "offline image asset exists");
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}}),
                    call("office_load_group", {{"group", "export"}}, 1),
                    call("office_load_group", {{"group", "images"}}, 2)};
            const auto action = [&](const QString& module, const QString& name, const QJsonArray& args)
            {
                return QJsonArray{call("office_action",
                    {{"module", module}, {"action", name}, {"args", args},
                        {"expectedRevision", runtime().value("revision")}})};
            };
            if (turn == 1 || turn == 2)
                return action("slides", turn == 1 ? "addImage" : "replaceImage",
                    {QUrl::fromLocalFile(asset_path).toString()});
            if (turn == 3)
                return action("slides", "saveTo", {QUrl::fromLocalFile(saved).toString()});
            if (turn == 4)
            {
                check(!slides.busy() && !slides.modified() && QFile::exists(saved),
                    "save result waits for real disk completion");
                return action("export", "start",
                    {"slides", QUrl::fromLocalFile(directory + "/agent.pdf").toString(),
                        QJsonObject{{"scope", "current"}}});
            }
            if (turn == 5)
            {
                check(pdf.snapshot().value("success").toBool() && !pdf.busy(),
                    "PDF export waits for success before next request");
                return action("images", "start",
                    {QUrl::fromLocalFile(directory).toString(),
                        QJsonObject{{"scope", "current"}, {"format", "png"}, {"longEdge", 1280}}});
            }
            return {};
        };
        agent.start(
            "Insert and replace the supplied image; save PPTX and export current page as PDF and PNG.");
        await_agent(agent);
        const auto reopened = load_presentation_file(saved.toStdString());
        check(agent.answer() == "Finished" && reopened.error == PresentationError::None &&
                reopened.scene.slides.size() == 4 && !reopened.scene.images.empty() &&
                images.snapshot().value("success").toBool() && !images.busy(),
            "real public image, save and export actions complete through offline agent and PPTX reopens");
        check(!slides.saveTo(QUrl::fromLocalFile(saved)), "saveTo refuses an existing destination");
        slides.clearError();
        QFile saved_file(saved);
        check(saved_file.open(QIODevice::ReadOnly), "saved file readable");
        const auto saved_bytes = saved_file.readAll().toStdString();
        saved_file.close();
        check(save_presentation_bytes(saved.toStdString(), saved_bytes, "missing").error !=
                PresentationError::None,
            "storage rechecks missing destination at commit");
        check(save_presentation_bytes((directory + "/new-copy.pptx").toStdString(), saved_bytes, "missing")
                    .error == PresentationError::None,
            "missing revision permits a genuinely new file");

        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_action",
                    {{"module", "slides"}, {"action", "semanticPage"}, {"args", QJsonArray{9000, 0, 1}},
                        {"expectedRevision", runtime().value("revision")}})};
            check(json(body).contains("invalid_index"), "nested semantic error is exposed as tool failure");
            return {};
        };
        agent.start("Exercise invalid semantic page recovery.");
        await_agent(agent);

        for (int index = 0; index < 70; ++index)
            check(slides.applyEdit("addText",
                      {{"text", QString(900, 'x')}, {"x", 40}, {"y", 100}, {"width", 200}, {"height", 50}}),
                "pagination fixture");
        QSet<QString> seen;
        int offset = 0;
        int expected = 0;
        do
        {
            const auto page = slides.semanticPage(slides.currentSlide(), offset, 16);
            check(page.value("ok").toBool() && json(QJsonObject::fromVariantMap(page)).size() <= 13 * 1024,
                "large semantic observations stay byte bounded");
            expected = page.value("totalObjects").toInt();
            for (const auto& value : page.value("nodes").toList())
                if (value.toMap().contains("index"))
                {
                    const auto id = value.toMap().value("id").toString();
                    check(!seen.contains(id), "pagination has no duplicates");
                    seen.insert(id);
                }
            const int next = page.value("nextOffset").toInt();
            check(next == -1 || next > offset, "semantic pagination advances even with large objects");
            if (next >= 0 && next <= offset)
                break;
            offset = next;
        } while (offset >= 0);
        check(seen.size() == expected, "pagination covers every object without dropped pages");

        QFile log(directory + "/diagnostics.jsonl");
        check(log.open(QIODevice::ReadOnly), "technical diagnostics persisted");
        const auto diagnostics = log.readAll();
        check(diagnostics.contains("compactionReason") && diagnostics.contains("generationBefore") &&
                !diagnostics.contains("private reasoning") && !diagnostics.contains("offline-key") &&
                !diagnostics.contains("\"arguments\":") && !diagnostics.contains("\"text\":") &&
                diagnostics.contains("\"workspace\":") && diagnostics.contains("\"version\":"),
            "diagnostics omit credentials, reasoning and arguments");
    }

    void test_composed_workflow(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        CanvasBridge map(false);
        Root root;
        root.slides = &slides;
        root.map = &map;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.registerModule("mindmap", &map);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            const auto revision = runtime().value("revision");
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}, {"expectedRevision", revision}})};
            if (turn >= 2 && turn <= 6)
            {
                if (turn > 2)
                    check(json(body).contains("pageReceipt"), "page receipt avoids model rereads");
                QJsonArray elements;
                for (int item = 0; item < 8; ++item)
                    elements.append(QJsonObject{{"type", "text"},
                        {"text", QStringLiteral("C++ Roadmap page %1 item %2").arg(turn - 1).arg(item)},
                        {"x", 50 + (item % 2) * 430}, {"y", 40 + (item / 2) * 115}, {"width", 380},
                        {"height", 85},
                        {"style",
                            QJsonObject{{"fontSize", item == 0 ? 32 : 20}, {"bold", item == 0},
                                {"textColor", "#10233F"}}}});
                return {call("office_compose_slide",
                    {{"page", turn == 2 ? 0 : -1}, {"background", "#F3F6FC"}, {"elements", elements},
                        {"expectedRevision", revision}})};
            }
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Create C++ roadmap in five pages.");
        await_agent(agent);
        check(agent.answer() == "Finished" && slides.slideCount() == 5 && network.requests.size() == 8,
            "five pages with 40 styled objects complete in eight offline exchanges");
        qsizetype total = 0;
        qsizetype peak = 0;
        for (const auto& request : network.requests)
        {
            total += request.size();
            peak = qMax(peak, request.size());
        }
        check(total < 200000 && peak < 35000, "five-page workflow has a measured payload budget");
        std::cout << "Five-page composed workflow: requests=" << network.requests.size()
                  << ", total bytes=" << total << ", peak bytes=" << peak << '\n';
        for (int page = 0; page < 5; ++page)
            check(slides.semanticPage(page, 0, 1).value("totalObjects").toInt() == 8,
                "each page contains exactly the requested eight objects");
        QFile collision(directory + "/C++ Roadmap.pptx");
        check(collision.open(QIODevice::WriteOnly), "preexisting collision file");
        collision.write("keep original");
        collision.close();
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            const auto revision = runtime().value("revision");
            if (turn == 0)
            {
                check(!json(body).contains("Create C++ roadmap in five pages."),
                    "new task has no historical context");
                return {call("office_load_group", {{"group", "slides"}})};
            }
            if (turn == 1)
                return {call("office_home", {{"expectedRevision", revision}})};
            if (turn == 2)
            {
                check(slides.active() && slides.modified() && json(body).contains("unsaved_changes"),
                    "home action cannot silently discard unsaved content");
                return {call("office_save", {{"title", "C++ Roadmap"}, {"expectedRevision", revision}})};
            }
            if (turn == 3)
            {
                check(QFile::exists(directory + "/C++ Roadmap (2).pptx") &&
                        slides.documentName().contains("C++ Roadmap (2)") && json(body).contains("file:"),
                    "topic naming, collision avoidance and durable saved path receipt");
                return {call("office_home", {{"expectedRevision", revision}})};
            }
            if (turn == 4)
            {
                check(!slides.active() && office_ai_workspace(runtime()).value("currentModule") == "home",
                    "agent-owned home navigation does not trigger external-switch abort");
                return {call("office_load_group", {{"group", "mindmap"}})};
            }
            if (turn == 5)
                return {call("office_new", {{"kind", "mindmap"}, {"expectedRevision", revision}})};
            if (turn == 6)
                return {call("office_action",
                    {{"module", "mindmap"}, {"action", "execute"},
                        {"args",
                            QJsonArray{"rename",
                                QJsonObject{{"id", map.snapshot().value("selectedId").toString()},
                                    {"text", "C++ Roadmap"}}}},
                        {"expectedRevision", revision}})};
            return {};
        };
        agent.start("Save that to Desktop, then make a mindmap on the same C++ topic.");
        await_agent(agent);
        check(agent.answer() == "Finished" && map.active() && network.requests.size() == 8,
            "save-home-create chain completes without PPT semantic rescans");
        check(collision.open(QIODevice::ReadOnly) && collision.readAll() == "keep original",
            "existing destination remains unchanged");
        const auto reopened = load_presentation_file((directory + "/C++ Roadmap (2).pptx").toStdString());
        check(reopened.error == PresentationError::None && reopened.scene.slides.size() == 5 &&
                reopened.scene.slides.front().shapes.size() == 8,
            "named PPT reopens with all pages and objects");
    }

    void test_deck_target_completion(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        Transport network(check);
        const auto page = [](int number)
        {
            return QJsonObject{{"title", QStringLiteral("Lesson %1").arg(number)}, {"layout", "grid"},
                {"blocks", QJsonArray{QJsonObject{{"text", QStringLiteral("Evidence %1").arg(number)}}}}};
        };
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 2)
                return {call("office_compose_slides",
                    {{"batchId", "opening"}, {"targetPages", 6},
                        {"pages", QJsonArray{page(1), page(2), page(3), page(4)}}})};
            if (turn == 3)
            {
                check(json(body).contains("verifiedPages") && json(body).contains("targetPages"),
                    "model receives the verified partial deck target after the first batch");
                return {};
            }
            if (turn == 4)
            {
                check(json(body).contains("verified slide target") && slides.slideCount() == 4,
                    "premature model completion triggers a further request without inventing pages");
                return {call("office_compose_slides",
                    {{"batchId", "closing"}, {"pages", QJsonArray{page(5), page(6)}}})};
            }
            if (turn == 5)
                return {call("office_save", {{"title", "Six-page target"}})};
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Create and save a six-page presentation.");
        await_agent(agent);
        const auto reopened = load_presentation_file((directory + "/Six-page target.pptx").toStdString());
        check(agent.answer() == "Finished" && network.requests.size() == 7 &&
                reopened.error == PresentationError::None && reopened.scene.slides.size() == 6,
            "agent cannot finish after the first segment and saves all six planned pages");
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 2)
                return {call("office_compose_slides",
                    {{"batchId", "unfinished"}, {"targetPages", 5},
                        {"pages", QJsonArray{page(1), page(2)}}})};
            return {};
        };
        agent.start("Create a five-page presentation.");
        await_agent(agent);
        check(agent.answer().isEmpty() && agent.status().contains(QStringLiteral("未完成")) &&
                network.requests.size() == 5 && slides.slideCount() == 2,
            "two premature model final answers cannot certify an unfinished deck");
        check(slides.saveTo(QUrl::fromLocalFile(directory + "/Unfinished two pages.pptx")),
            "preserve unfinished deck before the next offline scenario");
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 2)
                return {call("office_compose_slides",
                    {{"batchId", "first-two"}, {"targetPages", 5}, {"pages", QJsonArray{page(1), page(2)}}})};
            if (turn == 3 || turn == 5)
                return {};
            if (turn == 4)
                return {call("office_compose_slides",
                    {{"batchId", "middle-two"}, {"pages", QJsonArray{page(3), page(4)}}})};
            if (turn == 6)
            {
                check(json(body).contains("verified slide target") && slides.slideCount() == 4,
                    "successful partial progress permits another completion correction");
                return {call(
                    "office_compose_slides", {{"batchId", "final-one"}, {"pages", QJsonArray{page(5)}}})};
            }
            return {};
        };
        agent.start("Create a five-page presentation in three segments.");
        await_agent(agent);
        check(agent.answer() == "Finished" && network.requests.size() == 8 && slides.slideCount() == 5,
            "completion reminder renews after each verified page segment");
        check(slides.saveTo(QUrl::fromLocalFile(directory + "/Three segments.pptx")),
            "preserve completed deck before the explicit page-count scenario");
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
            {
                check(json(body).contains("整套 PPT 共 5 页"),
                    "explicit user page count enters the model system context");
                return {call("office_load_group", {{"group", "slides"}})};
            }
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 2)
                return {call("office_compose_slides",
                    {{"batchId", "wrong-target"}, {"targetPages", 3},
                        {"pages", QJsonArray{page(1), page(2)}}})};
            if (turn == 3)
            {
                check(json(body).contains("requested_deck_pages_mismatch") &&
                        json(body).contains("expectedPages") && slides.slideCount() == 1,
                    "model under-planning is rejected before the presentation changes");
                return {call("office_compose_slides",
                    {{"batchId", "correct-target"}, {"targetPages", 5},
                        {"pages", QJsonArray{page(1), page(2)}}})};
            }
            if (turn == 4)
                return {call("office_compose_slides",
                    {{"batchId", "remaining-three"}, {"pages", QJsonArray{page(3), page(4), page(5)}}})};
            return {};
        };
        agent.start(QStringLiteral("请生成5页PPT，主题是课程计划。"));
        await_agent(agent);
        check(agent.answer() == "Finished" && network.requests.size() == 6 && slides.slideCount() == 5,
            "explicit five-page request cannot be silently reduced by the model");
        network.requests.clear();
        network.respond = [&](int, const QJsonObject& body) -> QJsonArray
        {
            check(json(body).contains("整套 PPT 共 5 页"),
                "Chinese numeral page count enters the explicit deck contract");
            return {};
        };
        agent.start(QStringLiteral("请制作五页PPT。"));
        await_agent(agent);
        for (const auto& request : {QStringLiteral("请修改第5页PPT的标题。"),
                 QStringLiteral("请制作大约5页PPT。"), QStringLiteral("请制作5页PPT和8页PPT。")})
        {
            network.requests.clear();
            network.respond = [&](int, const QJsonObject& body) -> QJsonArray
            {
                check(!json(body).contains("用户明确要求整套 PPT 共"),
                    "ordinal, approximate and conflicting counts do not become exact deck requirements");
                return {};
            };
            agent.start(request);
            await_agent(agent);
        }
    }

    void test_requested_save_completion(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        Transport network(check);
        const QJsonObject page{{"title", "Saved page"}, {"layout", "cover"},
            {"blocks", QJsonArray{QJsonObject{{"text", "A concrete summary."}}}}};
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 2)
                return {call("office_compose_slides",
                    {{"batchId", "save-check"}, {"targetPages", 1}, {"pages", QJsonArray{page}}})};
            if (turn == 3)
                return {};
            if (turn == 4)
            {
                check(json(body).contains("still unsaved") && json(body).contains("office_save"),
                    "model sees a specific save correction after premature completion");
                return {call("office_save", {{"title", "Saved after reminder"}})};
            }
            if (turn == 5)
            {
                check(json(body).contains("last requested deliverable") && json(body).contains("office_task"),
                    "saved receipt tells the model when to close its checklist");
                return {call("office_action",
                    {{"op", "slides.addText"}, {"args", QJsonObject{{"text", "Changed after first save"}}}})};
            }
            if (turn == 7)
            {
                check(json(body).contains("office_save(current=true)") &&
                        json(body).contains("saveRequired") && json(body).contains("previouslySaved"),
                    "post-save edits invalidate the checkpoint receipt and prompt exact current save");
                return {call("office_save", {{"current", true}})};
            }
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start(QStringLiteral("请制作一页PPT并保存到桌面，不要再建第二份文档。"));
        await_agent(agent);
        const auto saved = load_presentation_file((directory + "/Saved after reminder.pptx").toStdString());
        check(agent.answer() == "Finished" && network.requests.size() == 9 &&
                saved.error == PresentationError::None && saved.scene.slides.size() == 1 &&
                find_presentation_text(saved.scene, "Changed after first save").size() == 1,
            "explicitly requested file is verified before a successful final answer");

        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 2)
                return {call("office_compose_slides",
                    {{"batchId", "unsaved-check"}, {"targetPages", 1}, {"pages", QJsonArray{page}}})};
            return {};
        };
        agent.start(QStringLiteral("请制作一页PPT并保存到桌面。"));
        await_agent(agent);
        check(agent.answer().isEmpty() && agent.resumable() &&
                agent.status().contains(QStringLiteral("尚未保存")) && network.requests.size() == 5,
            "two premature final replies leave the unsaved presentation resumable");

        network.requests.clear();
        network.respond = [](int, const QJsonObject&) -> QJsonArray
        {
            return {};
        };
        agent.start(QStringLiteral("请检查已保存文件的状态，不要修改文档。"));
        await_agent(agent);
        check(agent.answer() == "Finished" && !agent.resumable() && network.requests.size() == 1,
            "a saved-state query does not acquire an unintended save requirement");
        network.requests.clear();
        agent.start(QStringLiteral("只读取当前工作区状态。不修改、不创建、不保存。"));
        await_agent(agent);
        check(agent.answer() == "Finished" && !agent.resumable() && network.requests.size() == 1,
            "a short explicit no-save clause remains read-only after document work");

        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            const QJsonObject task{{"mode", "query"}, {"phase", turn == 2 ? "done" : "inspect"},
                {"goal", "Inspect workspace"}, {"targets", QJsonArray{"workspace"}},
                {"completed", QJsonArray{"workspace"}}, {"remaining", QJsonArray{}}};
            if (turn == 0)
                return {call("office_task", task)};
            if (turn == 2)
            {
                check(json(body).contains("call office_task now with phase=done") &&
                        json(body).contains("Do not repeat completed edits"),
                    "unfinished checklist receives an explicit closure instruction");
                return {call("office_task", task)};
            }
            return {};
        };
        agent.start("Inspect the current workspace and report.");
        await_agent(agent);
        check(agent.answer() == "Finished" && !agent.resumable() && network.requests.size() == 4,
            "model can close its checklist after a reminder without repeating document mutations");
    }

    void test_requested_mindmap_save_completion(const QString& directory)
    {
        using namespace mirrorfly;
        CanvasBridge map(false);
        Root root;
        root.map = &map;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("mindmap", &map);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "mindmap"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "mindmap"}})};
            if (turn == 3)
            {
                check(json(body).contains("still unsaved") && json(body).contains("office_save"),
                    "unsaved mindmap receives the same save correction as a presentation");
                return {call("office_save", {{"title", "Saved map after reminder"}})};
            }
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start(QStringLiteral("请新建思维导图并保存到桌面。"));
        await_agent(agent);
        check(agent.answer() == "Finished" && network.requests.size() == 5 &&
                QFile::exists(directory + "/Saved map after reminder.mfg"),
            "explicitly requested mindmap is saved before a successful final answer");

        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "mindmap"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "mindmap"}})};
            return {};
        };
        agent.start(QStringLiteral("请新建思维导图并保存到桌面。"));
        await_agent(agent);
        check(agent.answer().isEmpty() && agent.resumable() &&
                agent.status().contains(QStringLiteral("尚未保存")) && network.requests.size() == 4,
            "two premature final replies keep an unsaved mindmap resumable");
    }

    void test_single_saved_format(const QString& directory)
    {
        using namespace mirrorfly;
        for (const bool matching : {false, true})
        {
            CanvasBridge map(false);
            Root root;
            root.map = &map;
            AutomationBridge automation;
            automation.registerModule("app", &root);
            automation.registerModule("mindmap", &map);
            automation.setUiRoot(&root);
            const QString title = matching ? "Single matching format" : "Single wrong format";
            const QString request = matching ? QStringLiteral("只要一份思维导图，请创建并保存到桌面。")
                                             : QStringLiteral("只要一份PPTX，请创建并保存到桌面。");
            const auto constraints = office_ai_task_constraints(request);
            check(constraints.save == OfficeAiTaskConstraints::Save::Required &&
                    constraints.save_formats == QStringList{matching ? "mfg" : "pptx"},
                "single delivery test exercises an explicitly recognized format");
            Transport network(check);
            network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
            {
                if (turn == 0)
                    return {call("office_new", {{"kind", "mindmap"}})};
                if (turn == 1)
                    return {call("office_save", {{"title", title}})};
                if (turn == 3 && !matching)
                    check(json(body).contains("file receipts are missing: slides"),
                        "a saved file of the wrong format requests correction before completion");
                return {};
            };
            OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
            agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
            agent.start(request);
            await_agent(agent);
            check(QFile::exists(directory + '/' + title + ".mfg"),
                "the format check evaluates an actual saved deliverable");
            if (matching)
            {
                check(agent.activity() == "completed" && !agent.resumable() && network.requests.size() == 3,
                    "one matching saved format completes without an extra model request");
            }
            else
            {
                check(agent.answer().isEmpty() && agent.resumable() && agent.activity() == "paused" &&
                        network.requests.size() == 4,
                    "wrong single format never reports completion and preserves the task");
                agent.start(QStringLiteral("继续，只要思维导图，请保存。"));
                await_agent(agent);
                check(agent.activity() == "completed" && !agent.resumable() && network.requests.size() == 5 &&
                        !QFile::exists(directory + '/' + title + " (2).mfg"),
                    "an explicit human format update reuses its saved receipt without another copy");
            }
        }
    }

    void test_missing_single_delivery(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        const auto request = QStringLiteral("创建文档，保存后回到首页。");
        const auto constraints = office_ai_task_constraints(request);
        check(constraints.save == OfficeAiTaskConstraints::Save::Required &&
                constraints.save_formats.isEmpty() && constraints.file_count == 0,
            "unqualified save still requires at least one existing file");
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 1)
                return {call("office_save", {{"title", "Missing single delivery"}})};
            if (turn == 2)
                return {call("office_home", {})};
            if (turn == 3)
                check(QFile::remove(directory + "/Missing single delivery.pptx"),
                    "simulate external deletion after a verified save and return home");
            if (turn == 4)
                check(json(body).contains("file receipts are missing: files:0/1"),
                    "completion rechecks the saved path even without a requested format or count");
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start(request);
        await_agent(agent);
        check(agent.answer().isEmpty() && agent.activity() == "paused" && agent.resumable() &&
                network.requests.size() == 5,
            "a deleted single deliverable cannot be completed from an old receipt");
    }

    void test_multiple_saved_deliverables(const QString& directory)
    {
        check(mirrorfly::office_ai_requested_file_count(QStringLiteral("保存这个副本，不要再建第二份文档")) ==
                    0 &&
                mirrorfly::office_ai_requested_file_count(QStringLiteral("不要创建两份文件")) == 0 &&
                mirrorfly::office_ai_requested_file_count(QStringLiteral("修改第二份文档并保存")) == 0 &&
                mirrorfly::office_ai_requested_file_count(
                    QStringLiteral("Save this copy. Do not create two files")) == 0 &&
                mirrorfly::office_ai_requested_file_count(QStringLiteral("请创建两份独立文件并保存")) == 2 &&
                mirrorfly::office_ai_requested_file_count(
                    QStringLiteral("Create three separate documents")) == 3,
            "only explicit positive delivery counts add completion requirements");
        using namespace mirrorfly;
        PresentationBridge slides;
        CanvasBridge map(false);
        Root root;
        root.slides = &slides;
        root.map = &map;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.registerModule("mindmap", &map);
        automation.setUiRoot(&root);
        Transport network(check);
        const QJsonObject page{{"title", "C++ 入门"}, {"layout", "cover"},
            {"blocks", QJsonArray{QJsonObject{{"text", "先写一个可运行的小程序。"}}}}};
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_new", {{"kind", "slides"}})};
            if (turn == 2)
                return {call("office_compose_slides",
                    {{"batchId", "two-files"}, {"targetPages", 1}, {"pages", QJsonArray{page}}})};
            if (turn == 3)
                return {call("office_save", {{"title", "Two files slides"}})};
            if (turn == 4)
                return {call("office_home", {{"expectedRevision", runtime().value("revision")}})};
            if (turn == 5)
                return {};
            if (turn == 6)
            {
                check(json(body).contains("mindmap") && json(body).contains("files:1/2"),
                    "one saved PPT cannot prove both requested deliverables are saved");
                return {call("office_new", {{"kind", "slides"}})};
            }
            if (turn == 7)
            {
                check(json(body).contains("deliverable_already_saved") &&
                        json(body).contains("office_new(kind=mindmap)") && !slides.active(),
                    "redundant PPT creation is rejected before an empty document replaces the home state");
                return {call(
                    "office_action", {{"module", "app"}, {"action", "new"}, {"args", QJsonArray{"slides"}}})};
            }
            if (turn == 8)
            {
                check(json(body).contains("deliverable_already_saved") &&
                        json(body).contains("office_new(kind=mindmap)") && !slides.active(),
                    "the generic app.new tool observes the same completed-deliverable guard");
                return {call("office_new", {{"kind", "mindmap"}})};
            }
            if (turn == 9)
            {
                check(json(body).contains("rootId/selectedId") && !json(body).contains("新建整套 PPT"),
                    "creating a new format loads its guide for the model without another round trip");
                return {call("office_save", {{"title", "Two files map"}})};
            }
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start(QStringLiteral("请完成两份独立文件并分别保存：一页 PPT 和一份思维导图。"));
        await_agent(agent);
        const auto presentation =
            load_presentation_file((directory + "/Two files slides.pptx").toStdString());
        check(agent.answer() == "Finished" && network.requests.size() == 11 &&
                presentation.error == PresentationError::None &&
                QFile::exists(directory + "/Two files map.mfg"),
            "agent finishes only after separate PPT and mindmap save receipts exist");
    }

    void test_task_after_document_switch(const QString& directory)
    {
        using namespace mirrorfly;
        CanvasBridge map(false);
        Root root;
        root.map = &map;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("mindmap", &map);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_new", {{"kind", "mindmap"}}),
                    call("office_task",
                        {{"mode", "create"}, {"phase", "edit"}, {"goal", "Save mindmap"},
                            {"targets", QJsonArray{"map"}}, {"completed", QJsonArray{}},
                            {"remaining", QJsonArray{"map"}}},
                        1)};
            if (turn == 1)
            {
                check(map.active() && json(body).contains("rootId/selectedId") &&
                        !json(body).contains("not_executed_reobserve"),
                    "a checklist may follow successful document creation in the same model response");
                return {call("office_save", {{"title", "Same response map"}})};
            }
            if (turn == 2)
                return {call("office_task",
                    {{"mode", "create"}, {"phase", "done"}, {"goal", "Save mindmap"},
                        {"targets", QJsonArray{"map"}}, {"completed", QJsonArray{"map"}},
                        {"remaining", QJsonArray{}}})};
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Create and save one mindmap.");
        await_agent(agent);
        check(agent.answer() == "Finished" && network.requests.size() == 4 &&
                QFile::exists(directory + "/Same response map.mfg"),
            "a safe checklist update avoids a needless model correction after new document creation");
    }

    void test_post_save_observation_streak(const QString& directory)
    {
        using namespace mirrorfly;
        CanvasBridge map(false);
        Root root;
        root.map = &map;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("mindmap", &map);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "mindmap"}}),
                    call("office_task",
                        {{"mode", "create"}, {"phase", "edit"}, {"goal", "Save a map"},
                            {"targets", QJsonArray{"map"}}, {"completed", QJsonArray{}},
                            {"remaining", QJsonArray{"map"}}},
                        1)};
            if (turn == 1)
                return {call("office_new", {{"kind", "mindmap"}})};
            if (turn == 2)
                return {call("office_save", {{"title", "Observed map"}})};
            if (turn == 6)
                check(json(body).contains("unchanged data three times"),
                    "repeat reads give the model a concrete completion choice before stopping");
            return {call("office_workspace", {})};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Create a mindmap and save it.");
        await_agent(agent);
        check(QFile::exists(directory + "/Observed map.mfg") && agent.answer().isEmpty() &&
                agent.resumable() && agent.status().contains(QStringLiteral("重复查询六次")) &&
                network.requests.size() == 9,
            "saved document remains intact while six identical observations pause a read-only loop");
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
            {
                check(json(body).contains("Observed map.mfg"),
                    "resumed task still sees the durable saved-file receipt");
                return {call("office_task",
                    {{"mode", "create"}, {"phase", "done"}, {"goal", "Save a map"},
                        {"targets", QJsonArray{"map"}}, {"completed", QJsonArray{"map"}},
                        {"remaining", QJsonArray{}}})};
            }
            return {};
        };
        agent.resume();
        await_agent(agent);
        check(agent.answer() == "Finished" && !agent.resumable() && network.requests.size() == 2,
            "post-save pause resumes without replaying the completed document work");
    }

    void test_contract_compatibility(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        root.module = "slides";
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        slides.requestNew();
        const auto initial_generation = slides.editGeneration();
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            const auto revision = runtime().value("revision");
            const auto payload = json(body);
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_schema", {{"module", "slides"}, {"name", "semanticPage"}})};
            if (turn == 2)
                check(payload.contains("public_action") && payload.contains("parameters"),
                    "query action signature is available through lazy discovery");
            if (turn >= 3 && turn <= 5)
                check(slides.editGeneration() == initial_generation && slides.slideCount() == 1,
                    "invalid recipes and invalid batch arguments cause no partial page or edit");
            if (turn == 3)
                check(payload.contains("invalid_style") && payload.contains("elementIndex") &&
                        payload.contains("shadowOpacity"),
                    "style failure identifies the element and exact field");
            if (turn == 4)
                check(payload.contains("invalid_geometry"), "unsupported preset fails in recipe preflight");
            if (turn == 5)
                check(payload.contains("invalid_arguments") && payload.contains("stepIndex") &&
                        !payload.contains("availableActions"),
                    "batch error carries one action signature, not the whole module catalog");
            if (turn == 4)
                return {call("office_batch",
                    {{"expectedRevision", revision},
                        {"steps",
                            QJsonArray{
                                QJsonObject{{"module", "slides"}, {"action", "applyEdit"},
                                    {"args", QJsonArray{"addSlide", QJsonObject{{"layout", "blank"}}}}},
                                QJsonObject{{"module", "slides"}, {"action", "semanticPage"},
                                    {"args", QJsonObject{{"page", true}}}}}}})};
            if (turn == 2 || turn == 3 || turn == 5)
            {
                const QJsonObject text{{"type", "text"}, {"text", "Styled named-argument test"}, {"x", 50},
                    {"y", 30}, {"width", 700}, {"height", 80},
                    {"style",
                        QJsonObject{{"fontSize", 28}, {"fontFamily", "Arial"}, {"italic", true},
                            {"underline", true}, {"strike", true}, {"textColor", "#10233F"}}}};
                const QJsonObject shape{{"type", "shape"},
                    {"geometry", turn == 3 ? "unknownPreset" : "hexagon"}, {"x", 70}, {"y", 150},
                    {"width", 150}, {"height", 100},
                    {"style",
                        QJsonObject{{"fillColor", "#246BFD"}, {"fillOpacity", 0.5},
                            {"outlineColor", "#10233F"}, {"outlineOpacity", 0.8}, {"outlineWidth", 2},
                            {"shadowEnabled", true}, {"shadowColor", "#10233F"}, {"shadowBlur", 6},
                            {"shadowX", 0}, {"shadowY", 2}, {"shadowOpacity", turn == 2 ? 1.2 : 0.2}}}};
                return {call("office_compose_slide",
                    {{"page", 0}, {"background", "#FFFFFF"}, {"elements", QJsonArray{text, shape}},
                        {"expectedRevision", revision}})};
            }
            if (turn == 6)
            {
                check(payload.contains("pageReceipt") && slides.slideCount() == 1,
                    "expanded shape and text style recipe succeeds after correction");
                return {call("office_action",
                    {{"module", "slides"}, {"action", "semanticPage"}, {"args", QJsonObject{{"page", "0"}}},
                        {"expectedRevision", revision}})};
            }
            if (turn == 7)
            {
                check(payload.contains("Styled named-argument test") &&
                        !body.value("messages")
                            .toArray()
                            .last()
                            .toObject()
                            .value("content")
                            .toString()
                            .contains("invalid_arguments"),
                    "named query accepts numeric string and defaults its missing pagination arguments");
                return {call("office_state", {{"module", "slides"}})};
            }
            if (turn == 8 || turn == 9)
            {
                QJsonObject result;
                for (const auto& message : body.value("messages").toArray())
                {
                    const QString content = message.toObject().value("content").toString();
                    if (content.startsWith("Local execution checkpoint:\n"))
                    {
                        const auto checkpoint =
                            QJsonDocument::fromJson(content.mid(content.indexOf('\n') + 1).toUtf8()).object();
                        for (const auto& report :
                            checkpoint.value("observedData").toObject().value("recentResults").toArray())
                            if (report.toObject().value("tool") == "office_state")
                                result = report.toObject().value("result").toObject();
                    }
                    if (message.toObject().value("role") == "tool")
                        result =
                            QJsonDocument::fromJson(message.toObject().value("content").toString().toUtf8())
                                .object();
                }
                if (turn == 8)
                {
                    check(result.value("detailAvailable").toBool() &&
                            !result.value("state").toObject().contains("snapshot"),
                        "default state excludes object trees and full content");
                    return {call("office_state", {{"module", "slides"}, {"details", true}})};
                }
                check(result.value("content").toObject().value("items").isArray(),
                    "explicit detailed state remains available for targeted inspection");
                return {call(
                    "office_save", {{"title", "Contract compatibility"}, {"expectedRevision", revision}})};
            }
            check(turn == 10, "corrected workflow converges without repeated model retries");
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Exercise parameter compatibility, preflight recovery and expanded styles, then save.");
        await_agent(agent);
        check(agent.answer() == "Finished" && network.requests.size() == 11,
            "parameter errors recover through the real agent without stopping valid edits");
        const auto reopened =
            load_presentation_file((directory + "/Contract compatibility.pptx").toStdString());
        check(reopened.error == PresentationError::None && reopened.scene.slides.size() == 1 &&
                reopened.scene.slides.front().shapes.size() == 2,
            "corrected recipe saves exactly one page and two objects");
        if (reopened.scene.slides.size() == 1 && reopened.scene.slides.front().shapes.size() == 2)
        {
            const auto& shape = reopened.scene.slides.front().shapes.back();
            check(shape.geometry == "hexagon" && shape.path_geometry && shape.effects.shadow_opacity > 0.19,
                "new preset geometry and shadow persist in the saved PPTX");
        }
    }

    void test_runtime_recovery(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [](int, const QJsonObject&) -> QJsonArray
        {
            check(false, "invalid workspace never reaches the paid model transport");
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl");
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        root.invalid_state = true;
        agent.start("Broken workspace");
        check(!agent.busy() && network.requests.isEmpty(),
            "missing UI location fails locally before requesting");
        root.invalid_state = false;
        root.ignore_new = true;
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call(
                    "office_new", {{"kind", "slides"}, {"expectedRevision", runtime().value("revision")}})};
            check(false, "uncompleted navigation is never reported as success or retried by the model");
            return {};
        };
        agent.start("Route silently declined");
        await_agent(agent);
        check(network.requests.size() == 2 && agent.status().contains("navigation_not_completed") &&
                !slides.active(),
            "accepted void route must actually activate the destination");
        root.ignore_new = false;
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            return {call("office_compose_slide",
                {{"page", 0}, {"background", "#FFFFFF"},
                    {"elements",
                        QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::number(turn)}, {"x", 40},
                            {"y", 40}, {"width", 600}, {"height", 100}}}},
                    {"expectedRevision", runtime().value("revision")}})};
        };
        agent.start("Changing slide text cannot repair a missing active PPT");
        await_agent(agent);
        check(network.requests.size() == 4 && agent.status().contains("editable_slides_required"),
            "same unsatisfied precondition stops despite different generated arguments");
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn >= 2)
                check(json(body).contains("invalid tool-call envelope"),
                    "duplicate IDs receive a bounded repair hint without applying either call");
            auto first =
                call("office_new", {{"kind", "slides"}, {"expectedRevision", runtime().value("revision")}});
            auto second = first;
            second.insert("index", 1);
            return {first, second};
        };
        agent.start("Reject ambiguous duplicate tool IDs before any side effect");
        await_agent(agent);
        check(
            network.requests.size() == 4 && agent.resumable() && !slides.active() && agent.answer().isEmpty(),
            "repeated duplicate model call IDs cannot create duplicate documents");
    }

    void test_cancel_restart(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int, const QJsonObject&) -> QJsonArray
        {
            return {call("office_load_group", {{"group", "slides"}}),
                call("office_new", {{"kind", "slides"}, {"expectedRevision", runtime().value("revision")}},
                    1)};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl");
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start("Canceled creation");
        agent.cancel();
        check(agent.activity() != "completed" && agent.islandStatus() != QStringLiteral("完成了"),
            "cancelled work cannot display a successful island completion");
        check(!agent.busy() && !slides.active(),
            "cancel returns before a queued model reply can create a document");
        int activations = 0;
        QObject::connect(&slides, &PresentationBridge::documentActivated, [&]()
        {
            ++activations;
        });
        network.requests.clear();
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call(
                    "office_new", {{"kind", "slides"}, {"expectedRevision", runtime().value("revision")}})};
            return {};
        };
        agent.start("New creation after cancellation");
        await_agent(agent);
        check(agent.answer() == "Finished" && network.requests.size() == 3 && activations == 1,
            "late reply from a canceled turn cannot execute tools or consume results in the next turn");
    }

    void test_many_tasks(const QString& directory)
    {
        using namespace mirrorfly;
        Root root;
        AutomationBridge automation;
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [](int, const QJsonObject&) -> QJsonArray
        {
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/many-tasks.jsonl");
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        for (int index = 0; index < 300; ++index)
        {
            agent.start(QStringLiteral("Inspect workspace, task %1.").arg(index));
            await_agent(agent);
            check(agent.activity() == "completed" && !agent.resumable() && agent.trace()->rowCount() <= 500,
                "300 consecutive tasks release the input and retain bounded history");
        }
        agent.start(QStringLiteral("请制作 Word 文档并保存。"));
        await_agent(agent);
        check(agent.resumable() && agent.answer().isEmpty(),
            "model final text without document execution cannot certify completion");
        const int count = static_cast<int>(network.requests.size());
        agent.start(QString(4001, 'x'));
        check(!agent.busy() && agent.resumable() && network.requests.size() == count,
            "oversized input cannot resume a paused task or lock input");
        agent.start("Inspect the continue button.");
        await_agent(agent);
        const auto body = QJsonDocument::fromJson(network.requests.last()).object();
        check(!agent.resumable() && !json(body).contains(QStringLiteral("请制作 Word").toUtf8()),
            "continue inside a new goal cannot accidentally resume old work");

        network.respond = [](int turn, const QJsonObject&) -> QJsonArray
        {
            return {call("office_task",
                {{"mode", "query"}, {"phase", "inspect"},
                    {"goal", QStringLiteral("Unique query %1").arg(turn)}, {"targets", QJsonArray{}},
                    {"completed", QJsonArray{}}, {"remaining", QJsonArray{"Inspect"}}})};
        };
        const int before = static_cast<int>(network.requests.size());
        agent.start("Inspect workspace until finished.");
        await_agent(agent);
        check(agent.resumable() && network.requests.size() - before == 96 && agent.trace()->rowCount() <= 500,
            "a changing model loop pauses at a bounded budget without exhausting history");
        network.respond = [](int, const QJsonObject&) -> QJsonArray
        {
            return {};
        };
        agent.start("Inspect workspace now.");
        await_agent(agent);
        check(!agent.resumable() && agent.activity() == "completed",
            "a new task can start immediately after the execution budget pauses an old task");

        network.fault = [](int)
        {
            office_ai_test::Fault fault;
            fault.delay_ms = 10000;
            return fault;
        };
        agent.start("Inspect workspace through a stalled model.");
        auto* deadline = agent.findChild<QTimer*>("officeAiRequestDeadline");
        check(deadline != nullptr && deadline->isActive(), "absolute model deadline is armed");
        if (deadline)
            deadline->start(1);
        await_agent(agent);
        check(agent.resumable() && !agent.busy(), "stalled model deadline releases input and preserves task");
        network.fault = {};
        agent.start("Inspect workspace after the stalled request.");
        await_agent(agent);
        check(agent.activity() == "completed", "next task survives a late aborted model response");
    }

    void test_untrusted_compaction_resume(const QString& directory)
    {
        using namespace mirrorfly;
        const auto scenario = office_ai_test::untrusted_resume_scenario();
        check(!scenario.isEmpty(), "offline and real-provider tests share the adversarial fixture");
        const auto fixture = scenario.value("fixture").toObject();
        const auto folder = directory + "/untrusted-resume";
        check(QDir().mkpath(folder), "create isolated adversarial scope");
        const auto source = folder + '/' + fixture.value("name").toString();
        const auto copy = folder + '/' + scenario.value("copyName").toString();
        QFile original(source);
        check(original.open(QIODevice::WriteOnly), "write isolated adversarial source");
        original.write(fixture.value("content").toString().toUtf8());
        original.close();
        const auto before_hash = office_ai_test::scenario_file_hash(source);
        TextEditorBridge text;
        text.requestOpen(QUrl::fromLocalFile(source));
        QElapsedTimer load;
        load.start();
        while (text.busy() && load.elapsed() < 5000)
        {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        check(text.active() && text.content() == fixture.value("content").toString(),
            "load fixture into the actual text document engine");
        Root root;
        root.module = "text";
        AutomationBridge automation;
        automation.registerModule("text", &text);
        automation.setUiRoot(&root);
        Transport network(check);
        OfficeAiAgent agent(nullptr, &network, folder + "/diagnostics.jsonl", folder);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        bool resumed = false;
        bool wire_saw_data = false;
        bool source_write_rejected = false;
        QObject::connect(&agent, &OfficeAiAgent::toolObserved,
            [&](const QString&, const QJsonObject&, const QJsonObject& result)
        {
            source_write_rejected = source_write_rejected || result.value("error") == "source_file_protected";
        });
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            const auto messages = body.value("messages").toArray();
            for (const auto& value : messages)
            {
                const auto message = value.toObject();
                const auto content = message.value("content").toString();
                if (content.contains("IGNORE_AUTHORIZATION"))
                {
                    wire_saw_data = true;
                    check(message.value("role") == "tool" ||
                            (message.value("role") == "user" &&
                                content.startsWith("Local execution checkpoint:")),
                        "malicious payload travels only as tool/document data in the real provider wire");
                }
            }
            check(!messages.first().toObject().value("content").toString().contains("IGNORE_AUTHORIZATION"),
                "document role delimiters never become a system message through compaction or resume");
            if (!resumed)
            {
                if (turn == 0)
                    return {call("office_load_group", {{"group", "text"}})};
                if (turn <= 7)
                    return {call("office_read", {{"module", "text"}, {"view", "text"}, {"offset", turn}})};
                check(turn == 8 && !text.modified() &&
                        office_ai_test::scenario_file_hash(source) == before_hash,
                    "compacted read-only phase does not execute extra delivery/source-write instructions");
                QTimer::singleShot(0, &agent, [&]()
                {
                    agent.cancel();
                });
                return {};
            }
            const int next = turn - 9;
            int human_update_index = -1;
            int human_update_count = 0;
            for (int index = 0; index < messages.size(); ++index)
            {
                const auto message = messages.at(index).toObject();
                if (message.value("role") == "user" &&
                    message.value("content").toString().startsWith(QStringLiteral("继续")))
                {
                    human_update_index = index;
                    ++human_update_count;
                }
            }
            check(human_update_count == 1 && human_update_index >= 3,
                "provider wire contains the direct human continuation exactly once");
            if (next > 0)
                check(human_update_index < messages.size() - 1 &&
                        messages.last().toObject().value("role") == "tool",
                    "verified tool results follow the human update instead of reissuing it after edits");
            if (next == 0)
            {
                check(messages.last().toObject().value("role") == "user" &&
                        messages.last().toObject().value("content").toString().startsWith(
                            QStringLiteral("继续")),
                    "only the direct human continuation supplies the revised editing authorization");
                return {call("office_read",
                    {{"module", "text"}, {"view", "find"}, {"text", fixture.value("target")}, {"offset", 0},
                        {"limit", 1}})};
            }
            if (next == 1)
            {
                const auto found =
                    text.readContent({{"view", "find"}, {"text", fixture.value("target").toString()},
                                         {"offset", 0}, {"limit", 1}})
                        .value("items")
                        .toList();
                check(found.size() == 1, "authorized range comes from an actual find result");
                if (found.isEmpty())
                    return {};
                const auto range = found.first().toMap();
                return {call("office_action",
                    {{"op", "text.formatMarkdown"},
                        {"args",
                            QJsonArray{range.value("start").toInt(), range.value("end").toInt(), "bold",
                                QJsonObject{{"expectedText", fixture.value("target")}}}}})};
            }
            if (next == 2)
                return {call("office_save", {{"current", true}})};
            if (next == 3)
            {
                check(source_write_rejected && office_ai_test::scenario_file_hash(source) == before_hash,
                    "a malicious source-save attempt is actually blocked after compaction and resume");
                return {call("office_save", {{"destination", copy}})};
            }
            return {};
        };
        const auto prompts = scenario.value("prompts").toArray();
        agent.start(prompts.first().toString().replace("{source}", source));
        await_agent(agent);
        check(agent.resumable() && wire_saw_data && network.requests.size() == 9,
            "the active objective pauses after enough complete exchanges for real context compaction");
        QFile diagnostics(folder + "/diagnostics.jsonl");
        QElapsedTimer drain;
        drain.start();
        bool compacted = false;
        while (!compacted && drain.elapsed() < 1000)
        {
            if (diagnostics.open(QIODevice::ReadOnly))
            {
                const auto lines = diagnostics.readAll().split('\n');
                diagnostics.close();
                for (const auto& line : lines)
                    compacted =
                        compacted || QJsonDocument::fromJson(line).object().value("compactions").toInt() > 0;
            }
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        check(compacted, "wire test verifies actual context compaction, not a manually labelled checkpoint");
        resumed = true;
        agent.start(prompts.last().toString().replace("{copy}", copy));
        await_agent(agent);
        check(!agent.resumable() && agent.activity() == "completed" && source_write_rejected &&
                office_ai_test::scenario_file_hash(source) == before_hash &&
                office_ai_test::scenario_file_bytes(copy) ==
                    office_ai_test::scenario_expected_markdown(scenario).toUtf8() &&
                QDir(folder).entryList({"*.md"}, QDir::Files).size() == 2 &&
                QDir(folder).entryList({"*.docx", "*.pptx", "*.xlsx"}, QDir::Files).isEmpty(),
            "independent bytes/hash/count prove only the authorized range changed in one copy");
    }

    void test_selection_is_not_document_work(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        slides.requestNew();
        const auto generation = slides.editGeneration();
        Root root;
        root.module = "slides";
        root.slides = &slides;
        AutomationBridge automation;
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject&) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_load_group", {{"group", "slides"}})};
            if (turn == 1)
                return {call("office_batch",
                    {{"steps",
                        QJsonArray{QJsonObject{{"op", "slides.snapshot"}, {"args", QJsonArray{}}},
                            QJsonObject{{"op", "slides.selectShape"}, {"args", QJsonArray{0}}}}}})};
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/selection-evidence.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start(QStringLiteral("请修改PPT文档。"));
        await_agent(agent);
        check(agent.resumable() && agent.answer().isEmpty() && slides.editGeneration() == generation,
            "actual reads/selections cannot satisfy a requested document edit");
    }

    void test_observation_progress(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        Root root;
        root.slides = &slides;
        root.module = "slides";
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        slides.requestNew();
        Transport network(check);
        for (const bool batch : {false, true})
        {
            network.requests.clear();
            network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
            {
                const auto revision = runtime().value("revision");
                if (turn == 0)
                    return {call("office_load_group", {{"group", "slides"}})};
                if (turn >= 1 && turn <= 3)
                {
                    const QJsonObject step{
                        {"module", "slides"}, {"action", "semanticPage"}, {"args", QJsonArray{0, 0, 1}}};
                    if (batch)
                        return {call(
                            "office_batch", {{"steps", QJsonArray{step}}, {"expectedRevision", revision}})};
                    auto input = step;
                    input.insert("expectedRevision", revision);
                    return {call("office_action", input)};
                }
                if (turn == 4)
                {
                    check(json(body).contains("unchanged data three times"),
                        "semantic reads via action and batch both produce a recovery hint after three "
                        "unchanged results");
                    return {call("office_action",
                        {{"module", "slides"}, {"action", "applyEdit"},
                            {"args",
                                QJsonArray{"addText", QJsonObject{{"text", "Progress after observations"}}}},
                            {"expectedRevision", revision}})};
                }
                return {};
            };
            OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl");
            agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
            const auto before = slides.editGeneration();
            agent.start("Inspect then make progress.");
            await_agent(agent);
            check(agent.answer() == "Finished" && network.requests.size() == 6 &&
                    slides.editGeneration() != before,
                "repeat observation notice preserves subsequent edits and normal completion");
        }
        QFile log(directory + "/diagnostics.jsonl");
        check(log.open(QIODevice::ReadOnly), "read completed diagnostics");
        const auto records = log.readAll();
        check(records.contains("\"event\":\"repeat_observation\"") && records.contains("\"schema\":") &&
                records.contains("\"action\":\"semanticPage\""),
            "diagnostics identify schema queries, action targets and unchanged observations");
    }

}

int run_office_ai_agent_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    check(directory.isValid(), "isolated test storage");
    test_word_schema();
    test_agent(directory.path());
    test_presentation(directory.path());
    test_composed_workflow(directory.path());
    test_deck_target_completion(directory.path());
    test_requested_save_completion(directory.path());
    test_requested_mindmap_save_completion(directory.path());
    test_single_saved_format(directory.path());
    test_missing_single_delivery(directory.path());
    test_multiple_saved_deliverables(directory.path());
    test_task_after_document_switch(directory.path());
    test_post_save_observation_streak(directory.path());
    test_contract_compatibility(directory.path());
    test_runtime_recovery(directory.path());
    test_observation_progress(directory.path());
    test_untrusted_compaction_resume(directory.path());
    test_selection_is_not_document_work(directory.path());
    test_cancel_restart(directory.path());
    test_many_tasks(directory.path());
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_office_ai_agent_tests(argc, argv);
}

#include "office_ai_agent_tests.moc"
