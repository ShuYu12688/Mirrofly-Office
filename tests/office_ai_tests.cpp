#include "automation_bridge.hpp"
#include "canvas_bridge.hpp"
#include "office_ai_sequence.hpp"
#include "office_ai_stream.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"
#include "presentation_bridge.hpp"
#include "text_bridge.hpp"
#include <mirrorfly/markdown.hpp>

#include <mirrorfly/automation.hpp>
#include <mirrorfly/presentation_storage.hpp>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <iostream>
#include <memory>

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

    bool wait_for(const std::function<bool()>& ready)
    {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 10000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return ready();
    }

    QJsonObject parse(const std::string& data)
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(data)).object();
    }

    QJsonObject step(const QString& action, const QJsonArray& args = {})
    {
        return {{"module", "slides"}, {"action", action}, {"args", args}};
    }

    class ReadyRoot : public QObject
    {
        Q_OBJECT
    public:
        QString module = "slides";
        bool input_pending = false;
        Q_INVOKABLE bool automationReady() const
        {
            return true;
        }
        Q_INVOKABLE QVariantMap automationState() const
        {
            return {{"module", module}, {"pendingInput", input_pending}};
        }
    };

    void test_stream()
    {
        using mirrorfly::OfficeAiStream;
        const auto event = [](const QJsonObject& delta, const QString& finish = QString{})
        {
            return "data: " +
                QJsonDocument(
                    QJsonObject{{"choices",
                        QJsonArray{QJsonObject{{"index", 0}, {"delta", delta},
                            {"finish_reason", finish.isNull() ? QJsonValue{} : QJsonValue(finish)}}}}})
                    .toJson(QJsonDocument::Compact) +
                "\n\n";
        };
        OfficeAiStream phases;
        check(phases.activity() == "waiting", "empty SSE waits without pretending to produce text");
        phases.append(event({{"reasoning_content", "private reasoning"}}));
        check(phases.activity() == "thinking" && phases.content().isEmpty(),
            "reasoning-only packets report thinking without exposing private reasoning");
        phases.append(event({{"content", "short answer"}}));
        check(phases.activity() == "responding", "answer packets report response generation");
        phases.append(event({{"tool_calls",
            QJsonArray{QJsonObject{{"index", 0}, {"id", "phase-call"}, {"type", "function"},
                {"function", QJsonObject{{"name", "office_state"}, {"arguments", "{}"}}}}}}}));
        check(phases.activity() == "planning_tools" && !phases.complete(),
            "tool fragments announce planning but cannot execute before completion");
        const QByteArray first = event({{"content", QStringLiteral("正在制作导图")},
            {"tool_calls",
                QJsonArray{QJsonObject{{"index", 0}, {"id", "call_1"},
                    {"function", QJsonObject{{"name", "office_batch"}, {"arguments", "{\"steps\":"}}}}}}});
        const QByteArray second = event(
            {{"tool_calls",
                QJsonArray{QJsonObject{{"index", 0}, {"function", QJsonObject{{"arguments", "[]}"}}}}}}},
            "tool_calls");
        OfficeAiStream stream;
        for (const char byte : first)
            stream.append(QByteArray(1, byte));
        check(!stream.complete() && stream.response().isEmpty(),
            "partial stream never exposes executable response");
        stream.append(second);
        stream.append(
            "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":5}}\n\ndata: "
            "[DONE]\n\n");
        const auto response = stream.response();
        const auto message =
            response.value("choices").toArray().first().toObject().value("message").toObject();
        check(stream.complete() && stream.content() == QStringLiteral("正在制作导图"),
            "UTF-8 survives arbitrary packet boundaries");
        check(message.value("tool_calls")
                    .toArray()
                    .first()
                    .toObject()
                    .value("function")
                    .toObject()
                    .value("arguments") == "{\"steps\":[]}",
            "tool argument fragments reassemble");
        check(response.value("usage").toObject().value("completion_tokens") == 5,
            "usage-only event is retained");
        OfficeAiStream incomplete;
        incomplete.append(event({{"content", "partial"}}, "stop"));
        check(!incomplete.complete(), "missing DONE is not executable");
        OfficeAiStream malformed;
        malformed.append("data: invalid json\n\n");
        check(malformed.failed(), "malformed stream is rejected");
        OfficeAiStream truncated;
        truncated.append(event({}, "length") + "data: [DONE]\n\n");
        check(truncated.response().value("choices").toArray().first().toObject().value("finish_reason") ==
                "length",
            "length termination remains distinguishable");
    }

    void test_mindmap(const QString& directory)
    {
        using namespace mirrorfly;
        CanvasBridge map(false);
        map.requestNew();
        AutomationBridge automation;
        automation.registerModule("mindmap", &map);
        ReadyRoot root;
        root.module = "mindmap";
        automation.setUiRoot(&root);
        const auto state = []()
        {
            return parse(office_runtime_snapshot());
        };
        const QString key = office_ai_document_key(state());
        const auto schema =
            parse(office_ai_contract()).value("schemas").toObject().value("mindmap").toObject();
        check(schema.contains("createNode") && schema.contains("connect"),
            "mindmap publishes real command schemas");
        check(!state().value("modules").toObject().value("mindmap").toObject().contains("viewData"),
            "runtime snapshot omits content trees");
        OfficeAiSequence sequence(state, [](const QJsonObject& action)
        {
            auto request = action;
            request.insert("version", 1);
            return parse(office_execute(QJsonDocument(request).toJson(QJsonDocument::Compact).toStdString()));
        });
        const QString root_id = map.snapshot().value("selectedId").toString();
        QJsonArray steps;
        const auto command = [&](const QString& action, const QJsonObject& args)
        {
            steps.append(QJsonObject{
                {"module", "mindmap"}, {"action", "execute"}, {"args", QJsonArray{action, args}}});
        };
        command("rename", {{"id", root_id}, {"text", "Plan"}});
        command("createNode", {{"newId", "ai-child"}, {"text", "Research"}, {"x", 400}, {"y", 160}});
        command("connect", {{"id", root_id}, {"toId", "ai-child"}, {"newId", "ai-edge"}});
        command("styleNode", {{"id", "ai-child"}, {"fill", "#E0EAFE"}, {"shape", "rounded"}});
        QJsonObject result;
        sequence.start(steps, state().value("revision").toString(), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && result.value("ok").toBool(),
            "native mindmap batch executes");
        check(office_ai_document_key(state()) == key, "edits preserve document identity");
        check(map.snapshot().value("nodes").toList().size() == 2 &&
                map.snapshot().value("edges").toList().size() == 1,
            "mindmap creates graph instead of slides");
        std::cout
            << "Mindmap batch timing: "
            << QJsonDocument(result.value("timing").toObject()).toJson(QJsonDocument::Compact).constData()
            << '\n';
        const auto before_layout = map.snapshot().value("nodes");
        check(map.execute("autoLayout", {}), "public layout command");
        const auto after_layout = map.snapshot().value("nodes");
        check(after_layout != before_layout, "layout moves the branch");
        map.undo();
        check(map.snapshot().value("nodes") == before_layout, "layout is one undoable transaction");
        map.redo();
        check(map.snapshot().value("nodes") == after_layout, "redo restores exact layout");
        map.saveAs();
        const auto path = directory + "/agent-map.mfg";
        map.selectSaveFile(QUrl::fromLocalFile(path));
        check(wait_for(
                  [&]()
        {
            return !map.locked();
        }),
            "mindmap saves headlessly");
        const auto reopened = load_mindmap_file(path.toStdString());
        check(reopened.error == MindMapError::None && reopened.document.nodes.size() == 2 &&
                reopened.document.edges.size() == 1,
            "mindmap graph survives save and readback");
        map.requestNew();
        check(
            office_ai_document_key(state()) != key, "new document changes identity even in the same module");
        root.input_pending = true;
        sequence.start(steps, state().value("revision").toString(), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && result.value("error") == "user_input_required" &&
                result.value("executed").toInt() == 0,
            "pending user input stops without polling a dialog");
    }

    void test_sequence()
    {
        int revision = 0;
        int executed = 0;
        int reject_at = -1;
        bool pending = false;
        auto state = [&]() -> QJsonObject
        {
            return {{"ok", true}, {"revision", QString::number(revision)},
                {"ui", QJsonObject{{"ready", !pending}}},
                {"modules", QJsonObject{{"slides", QJsonObject{{"busy", pending}}}}}};
        };
        mirrorfly::OfficeAiSequence sequence(state, [&](const QJsonObject& action) -> QJsonObject
        {
            check(action.value("expectedRevision").toString() == QString::number(revision),
                "sequence uses only its preceding action revision");
            if (executed == reject_at)
                return {{"ok", false}, {"error", "rejected"}, {"revision", QString::number(revision)}};
            ++executed;
            ++revision;
            return {{"ok", true}, {"revision", QString::number(revision)}};
        });
        QJsonArray steps;
        for (int index = 0; index < 50; ++index)
            steps.append(step("undo"));
        QJsonObject result;
        sequence.start(steps, "0", [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && result.value("ok").toBool() &&
                executed == 50,
            "batch exceeds old 4/40 call limits and preserves sequential revision chaining");
        executed = 0;
        reject_at = 1;
        sequence.start(steps, QString::number(revision), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && executed == 1 &&
                result.value("error") == "rejected" && result.value("remaining").toInt() == 49 &&
                result.value("nextStep").toInt() == 1,
            "failure skips remaining dependent actions");
        reject_at = -1;
        executed = 0;
        sequence.start(steps, QString::number(revision), [&](const QJsonObject& value)
        {
            result = value;
        });
        ++revision;
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && executed == 0 &&
                result.value("error") == "stale_revision",
            "external edit is never masked by refreshing revision");
        sequence.start(steps, QString::number(revision), [&](const QJsonObject&)
        {
            check(false, "canceled callback");
        });
        sequence.cancel();
        QCoreApplication::processEvents();
        check(executed == 0, "cancel removes queued actions");

        mirrorfly::OfficeAiSequence asynchronous(state, [&](const QJsonObject&) -> QJsonObject
        {
            ++executed;
            ++revision;
            pending = true;
            QTimer::singleShot(5, [&]()
            {
                pending = false;
                ++revision;
            });
            return {{"ok", true}, {"revision", QString::number(revision)},
                {"result", QJsonObject{{"status", "pending"}}}};
        });
        asynchronous.start(steps, QString::number(revision), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !asynchronous.active();
        }) && !pending &&
                executed == 1 && result.value("ok").toBool() && result.value("reobserve").toBool() &&
                result.value("revision").toString() == QString::number(revision),
            "await async completion then re-plan instead of masking unrelated edits");

        executed = 0;
        pending = true;
        QTimer::singleShot(10, [&]()
        {
            pending = false;
        });
        sequence.start(QJsonArray{step("undo")}, QString::number(revision), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && executed == 1 &&
                result.value("ok").toBool(),
            "transient UI busy waits locally before executing instead of failing or asking the model");
    }

    void test_sequence_notifications()
    {
        using namespace mirrorfly;
        int snapshots = 0;
        int executed = 0;
        int subscriptions = 0;
        int unsubscriptions = 0;
        bool pending = true;
        OfficeChangeObserver observer;
        auto snapshot = [&]() -> QJsonObject
        {
            ++snapshots;
            return {{"ok", true}, {"revision", "event-revision"}, {"ui", QJsonObject{{"ready", !pending}}},
                {"modules", QJsonObject{{"slides", QJsonObject{{"busy", pending}}}}}};
        };
        auto subscribe = [&](OfficeChangeObserver callback)
        {
            observer = std::move(callback);
            return static_cast<OfficeChangeSubscription>(++subscriptions);
        };
        auto unsubscribe = [&](OfficeChangeSubscription)
        {
            ++unsubscriptions;
            observer = {};
            return true;
        };
        QJsonObject result;
        auto sequence = std::make_unique<OfficeAiSequence>(snapshot, [&](const QJsonObject&) -> QJsonObject
        {
            ++executed;
            return {{"ok", true}, {"revision", "event-revision"}};
        }, nullptr, subscribe, unsubscribe);
        sequence->start({step("undo")}, "event-revision", [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return bool(observer);
        }),
            "waiting sequence subscribes to state invalidations");
        const int before = snapshots;
        QElapsedTimer idle;
        idle.start();
        while (idle.elapsed() < 250)
        {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        check(snapshots == before && executed == 0,
            "waiting does not poll snapshots every 100ms between real state events");
        for (int index = 0; index < 100; ++index)
            observer("event-revision");
        check(snapshots == before, "notification never executes a batch recursively");
        QCoreApplication::processEvents();
        check(snapshots == before + 1 && executed == 0,
            "an event storm coalesces to one queued readiness snapshot");
        const auto stale_observer = observer;
        observer("event-revision");
        sequence->cancel();
        pending = false;
        sequence->start({step("undo")}, "event-revision", [&](const QJsonObject& value)
        {
            result = value;
        });
        stale_observer("event-revision");
        check(wait_for(
                  [&]()
        {
            return !sequence->active();
        }) && executed == 1 &&
                result.value("ok").toBool(),
            "cancel/restart ignores both a queued old advance and its stale subscription callback");
        pending = true;
        sequence->start({step("undo")}, "event-revision", [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return bool(observer);
        }),
            "restart subscribes only when actually waiting");
        QElapsedTimer response;
        response.start();
        pending = false;
        observer("event-revision");
        check(wait_for(
                  [&]()
        {
            return !sequence->active();
        }) && response.elapsed() < 500 &&
                executed == 2,
            "a readiness event completes well before the one-second fallback");
        pending = true;
        sequence->start({step("undo")}, "event-revision", [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return bool(observer);
        }),
            "fallback test begins in a waiting phase");
        pending = false; // Simulate a completed event missed before subscription.
        check(wait_for(
                  [&]()
        {
            return !sequence->active();
        }) && executed == 3 &&
                result.value("ok").toBool(),
            "one-second fallback still completes when no notification was delivered");
        pending = true;
        sequence->start({step("undo")}, "event-revision", [](const QJsonObject&)
        {
        });
        check(wait_for(
                  [&]()
        {
            return bool(observer);
        }),
            "destruction test owns a live subscription");
        const auto destroyed_observer = observer;
        sequence.reset();
        destroyed_observer("event-revision");
        QCoreApplication::processEvents();
        check(subscriptions == unsubscriptions && executed == 3,
            "destruction unsubscribes and retained callback cannot access a destroyed sequence");
    }

    void test_markdown_find_tool(const QString& directory)
    {
        using namespace mirrorfly;
        TextEditorBridge editor;
        editor.requestNewMarkdown();
        editor.replaceContent(QStringLiteral("🦋 加粗目标 保留内容"));
        AutomationBridge automation;
        ReadyRoot root;
        root.module = "text";
        automation.registerModule("text", &editor);
        automation.setUiRoot(&root);
        OfficeAiToolbox toolbox(directory);
        toolbox.query("office_load_group", {{"group", "text"}});
        const auto schema = toolbox.query("office_schema", {{"module", "text"}, {"name", "formatMarkdown"}});
        check(schema.value("options").toObject().contains("expectedText"),
            "AI schema exposes the exact source guard");
        const auto found = toolbox.query(
            "office_read", {{"module", "text"}, {"view", "find"}, {"text", QStringLiteral("加粗目标")}});
        const auto match = found.value("items").toArray().first().toObject();
        check(found.value("ok").toBool() && match.value("start") == 3 && match.value("end") == 7,
            "actual AI read forwards the literal query and UTF-16 match positions");
        for (const bool correct : {false, true})
        {
            const auto before = editor.content();
            toolbox.beginResponse(parse(office_runtime_snapshot()).value("revision").toString(), 1);
            bool finished = false;
            QJsonObject result;
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{{"start", match.value("start")}, {"end", match.value("end")},
                            {"action", "bold"},
                            {"options",
                                QJsonObject{{"expectedText",
                                    correct ? match.value("text") : QJsonValue("wrong")}}}}}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            check(wait_for(
                      [&]()
            {
                return finished;
            }) && result.value("ok").toBool() == correct,
                "actual AI action enforces the source guard");
            check(correct ? editor.content() == QStringLiteral("🦋 **加粗目标** 保留内容")
                          : editor.content() == before,
                "guarded AI edits preserve all unrelated source text");
            if (correct)
                check(result.value("progress") == "document_edit_accepted" &&
                        result.value("editReceipt").toObject().value("action") == "bold",
                    "AI reports the exact accepted Markdown edit");
        }
    }

    void test_markdown_style_tool(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNewMarkdown();
        editor.replaceContent("| A | B |\n| --- | --- |\n| a | b |\n");
        mirrorfly::AutomationBridge automation;
        ReadyRoot root;
        root.module = "text";
        automation.registerModule("text", &editor);
        automation.setUiRoot(&root);
        mirrorfly::OfficeAiToolbox toolbox(directory);
        check(toolbox.query("office_load_group", {{"group", "text"}}).value("ok").toBool(),
            "load the active Markdown group through the public AI discovery path");
        const auto schema = toolbox.query("office_schema", {{"module", "text"}, {"name", "formatMarkdown"}});
        check(schema.value("ok").toBool() && schema.value("actions").toArray().contains("tableAlign") &&
                schema.value("actions").toArray().contains("heading") &&
                schema.value("actions").toArray().contains("paragraph") &&
                schema.value("options").toObject().contains("headingLevel") &&
                schema.value("actions").toArray().contains("listIndent") &&
                schema.value("actions").toArray().contains("listOutdent") &&
                schema.value("actions").toArray().contains("taskSet") &&
                schema.value("actions").toArray().contains("quoteSet") &&
                schema.value("actions").toArray().contains("unlink") &&
                schema.value("options").toObject().contains("url") &&
                schema.value("options").toObject().contains("title") &&
                schema.value("options").toObject().contains("quoteLevel") &&
                schema.value("options").toObject().contains("checked") &&
                schema.value("options").toObject().contains("column") &&
                schema.value("options").toObject().contains("alignment") &&
                schema.value("positions").toString().contains("UTF-16"),
            "AI discovers explicit column alignment options and source position units");
        const auto revision = parse(mirrorfly::office_snapshot()).value("revision");
        QJsonObject result;
        bool finished = false;
        toolbox.beginResponse(revision.toString(), 1);
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{{"start", 26}, {"end", 26}, {"action", "tableAlign"},
                        {"options", QJsonObject{{"column", 1}, {"alignment", "center"}}}}},
                {"expectedRevision", revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        check(wait_for(
                  [&]()
        {
            return finished;
        }) && result.value("ok").toBool() &&
                editor.content().contains("| --- | :---: |"),
            "named AI tool arguments reach the public Markdown column transaction");
        editor.replaceContent("- [ ] first\n  - [ ] nested\n");
        const auto task_revision = parse(mirrorfly::office_snapshot()).value("revision");
        toolbox.beginResponse(task_revision.toString(), 1);
        finished = false;
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{{"start", 0}, {"end", editor.content().size()}, {"action", "taskSet"},
                        {"options", QJsonObject{{"checked", true}}}}},
                {"expectedRevision", task_revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        check(wait_for(
                  [&]()
        {
            return finished;
        }) && result.value("ok").toBool() &&
                editor.content() == "- [x] first\n  - [x] nested\n",
            "typed task completion reaches the public interface through the real AI tool dispatcher");
        editor.replaceContent("> parent\n> > child\nplain\n");
        const auto quote_revision = parse(mirrorfly::office_snapshot()).value("revision");
        toolbox.beginResponse(quote_revision.toString(), 1);
        finished = false;
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{{"start", 2}, {"end", 2}, {"action", "quoteSet"},
                        {"options", QJsonObject{{"quoteLevel", 2}}}}},
                {"expectedRevision", quote_revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        check(wait_for(
                  [&]()
        {
            return finished;
        }) && result.value("ok").toBool() &&
                editor.content() == "> > parent\n> > > child\nplain\n",
            "explicit quote levels reach the public interface through real named AI tool arguments");
        editor.replaceContent(QString::fromUtf8(u8"前 你好🦋 后\n"));
        for (const auto& action : {QStringLiteral("link"), QStringLiteral("unlink")})
        {
            const auto link_revision = parse(mirrorfly::office_snapshot()).value("revision");
            toolbox.beginResponse(link_revision.toString(), 1);
            finished = false;
            const QJsonObject options{{"url", "../a (b)/x?one=1&two=2"}, {"title", "a \"title\""}};
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{{"start", action == "link" ? 2 : 3}, {"end", action == "link" ? 6 : 3},
                            {"action", action}, {"options", options}}},
                    {"expectedRevision", link_revision}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            check(wait_for(
                      [&]()
            {
                return finished;
            }) && result.value("ok").toBool() &&
                    (action == "link" ? editor.content().contains(QString::fromUtf8(u8"[你好🦋]"))
                                      : editor.content() == QString::fromUtf8(u8"前 你好🦋 后\n")),
                "named AI link and unlink arguments reach the real public source transaction");
        }
        for (const auto& source : {QStringLiteral("[**label**][r] [r]\n\n[r]: ../original\n"),
                 QStringLiteral("[first](../same)[second](../same)\n"),
                 QStringLiteral("<https://example.com/a> tail\n")})
        {
            editor.replaceContent(source);
            const auto resolved_revision = parse(mirrorfly::office_snapshot()).value("revision");
            toolbox.beginResponse(resolved_revision.toString(), 1);
            finished = false;
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{{"start", 3}, {"end", 3}, {"action", "link"},
                            {"options", QJsonObject{{"url", "../actual"}}}}},
                    {"expectedRevision", resolved_revision}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            check(wait_for(
                      [&]()
            {
                return finished;
            }) && result.value("ok").toBool() &&
                    editor.content().contains("](../actual)") &&
                    (!source.contains("[r]:") || editor.content().endsWith("[r]: ../original\n")) &&
                    (!source.contains("[second]") || editor.content().endsWith("[second](../same)\n")),
                "named AI actions update reference and automatic occurrences without changing shared "
                "definitions");
        }
        editor.replaceContent(QString::fromUtf8(u8"🦋前后\n"));
        const auto break_revision = parse(mirrorfly::office_snapshot()).value("revision");
        toolbox.beginResponse(break_revision.toString(), 1);
        finished = false;
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{
                        {"start", 3}, {"end", 3}, {"action", "hardBreak"}, {"options", QJsonObject{}}}},
                {"expectedRevision", break_revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        check(wait_for(
                  [&]()
        {
            return finished;
        }) && result.value("ok").toBool() &&
                editor.content() == QString::fromUtf8(u8"🦋前  \n后\n"),
            "named AI hardBreak uses source UTF-16 and the actual public source transaction");
        const auto rule_revision = parse(mirrorfly::office_snapshot()).value("revision");
        toolbox.beginResponse(rule_revision.toString(), 1);
        finished = false;
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{
                        {"start", 3}, {"end", 3}, {"action", "thematicBreak"}, {"options", QJsonObject{}}}},
                {"expectedRevision", rule_revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        check(wait_for(
                  [&]()
        {
            return finished;
        }) && result.value("ok").toBool() &&
                mirrorfly::markdown_thematic_break_count(editor.content().toStdString()) == 1 &&
                editor.content().startsWith(QString::fromUtf8(u8"🦋前  \n后\n")),
            "named AI separator preserves the entire current Unicode paragraph through the public interface");
        editor.replaceContent("a**b**c\n");
        const auto style_revision = parse(mirrorfly::office_snapshot()).value("revision");
        toolbox.beginResponse(style_revision.toString(), 1);
        finished = false;
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{{"start", 3}, {"end", 4}, {"action", "italic"}, {"options", QJsonObject{}}}},
                {"expectedRevision", style_revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        const bool completed = wait_for([&]()
        {
            return finished;
        });
        const auto paragraphs = mirrorfly::markdown_paragraphs(editor.content().toStdString());
        bool mixed = false;
        if (paragraphs.size() == 1)
            for (const auto& run : paragraphs[0].runs)
                mixed = mixed || (run.style.text == "b" && run.style.bold && run.style.italic);
        check(completed && result.value("ok").toBool() && mixed && editor.content() == "a***b***c\n",
            "real named AI source action retains existing bold while adding italic to the selected body run");
        const QString nested_source =
            "100) parent\n\n     > quote\n     >\n     > ```cpp\n     > x\n"
            "     > ```\n     >\n     > | A |\n     > | :---: |\n     > | a |\n     >\n     > ---\n"
            "\n     after\n\n101) tail\n";
        editor.replaceContent(nested_source);
        const auto container_revision = parse(mirrorfly::office_snapshot()).value("revision");
        toolbox.beginResponse(container_revision.toString(), 1);
        finished = false;
        const int quote_start = nested_source.indexOf("quote");
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{{"start", quote_start}, {"end", quote_start + 5}, {"action", "italic"},
                        {"options", QJsonObject{}}}},
                {"expectedRevision", container_revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        const bool container_completed = wait_for([&]()
        {
            return finished;
        });
        const auto nested = mirrorfly::markdown_paragraphs(editor.content().toStdString());
        const auto leaves = mirrorfly::markdown_blocks(editor.content().toStdString());
        check(container_completed && result.value("ok").toBool() && nested.size() == 4 &&
                leaves.size() == 3 && leaves[0].kind == "code" && leaves[0].text == "x\n" &&
                leaves[0].language == "cpp" && leaves[1].kind == "table" &&
                leaves[2].kind == "thematicBreak" && leaves[0].containers.size() == 2 &&
                leaves[0].containers[0].identity == nested[0].containers[0].identity &&
                nested[1].containers.size() == 2 && nested[1].containers[0].kind == "listItem" &&
                nested[1].containers[1].kind == "quote" &&
                nested[0].containers[0].identity == nested[2].containers[0].identity &&
                nested[1].runs[0].style.italic && nested[0].containers[0].ordinal == 100 &&
                nested[0].containers[0].delimiter == ')',
            "named AI character style action preserves mixed code/table/rule ownership and literals");
        const QString moving_source = "- first\n- parent\n\n  > ```cpp\n  > x\n  > ```\n  >\n"
                                      "  > | A |\n  > | :---: |\n  > | a |\n  >\n  > ---\n\n- tail\n";
        editor.replaceContent(moving_source);
        const auto move_revision = parse(mirrorfly::office_snapshot()).value("revision");
        toolbox.beginResponse(move_revision.toString(), 1);
        finished = false;
        const int parent_start = moving_source.indexOf("parent");
        toolbox.execute("office_action",
            {{"op", "text.formatMarkdown"},
                {"args",
                    QJsonObject{{"start", parent_start}, {"end", parent_start}, {"action", "listIndent"},
                        {"options", QJsonObject{}}}},
                {"expectedRevision", move_revision}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        const bool moved = wait_for([&]()
        {
            return finished;
        });
        const auto moved_leaves = mirrorfly::markdown_blocks(editor.content().toStdString());
        check(moved && result.value("ok").toBool() && moved_leaves.size() == 3 &&
                moved_leaves[0].text == "x\n" && moved_leaves[0].language == "cpp" &&
                moved_leaves[0].containers.size() == 3 && moved_leaves[0].containers[0].kind == "listItem" &&
                moved_leaves[0].containers[1].kind == "listItem" &&
                moved_leaves[0].containers[2].kind == "quote",
            "named AI source list action moves owned code/table/rule through the public transaction");
        const QString heading_source =
            QString::fromUtf8(u8"🦋前\n\n> - **bold** [link](../x)\n>   ===\n> - tail\n");
        editor.replaceContent(heading_source);
        for (const auto& action : {QStringLiteral("heading"), QStringLiteral("paragraph")})
        {
            const auto heading_revision = parse(mirrorfly::office_snapshot()).value("revision");
            toolbox.beginResponse(heading_revision.toString(), 1);
            finished = false;
            const int caret = editor.content().indexOf("bold");
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{{"start", caret}, {"end", caret}, {"action", action},
                            {"options", QJsonObject{{"headingLevel", 6}}}}},
                    {"expectedRevision", heading_revision}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            const bool heading_completed = wait_for([&]()
            {
                return finished;
            });
            const auto headings = mirrorfly::markdown_paragraphs(editor.content().toUtf8().toStdString());
            check(heading_completed && result.value("ok").toBool() && headings.size() == 3 &&
                    headings[1].heading_level == (action == "heading" ? 6 : 0) &&
                    headings[1].containers.size() == 2 && headings[1].containers[0].kind == "quote" &&
                    headings[1].containers[1].kind == "listItem" && headings[1].runs[0].style.bold,
                "named AI heading and paragraph actions preserve public source parent/style semantics");
        }
        editor.replaceContent(QString::fromUtf8(u8"🦋前\n\n> - ##\n> - tail\n"));
        for (const auto& action : {QStringLiteral("heading"), QStringLiteral("paragraph")})
        {
            const auto empty_heading_revision = parse(mirrorfly::office_snapshot()).value("revision");
            toolbox.beginResponse(empty_heading_revision.toString(), 1);
            finished = false;
            const int caret = editor.content().indexOf("#");
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{{"start", caret}, {"end", caret}, {"action", action},
                            {"options", QJsonObject{{"headingLevel", 6}}}}},
                    {"expectedRevision", empty_heading_revision}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            const bool complete = wait_for([&]()
            {
                return finished;
            });
            const auto empty_paragraphs =
                mirrorfly::markdown_paragraphs(editor.content().toUtf8().toStdString());
            check(complete && result.value("ok").toBool() &&
                    empty_paragraphs.size() == (action == "heading" ? 3 : 2) &&
                    (action != "heading" ||
                        (empty_paragraphs[1].heading_level == 6 && empty_paragraphs[1].runs.empty())) &&
                    editor.content().contains("> - "),
                "actual named AI transactions handle empty headings and retain source UTF-16 owner syntax");
        }
        editor.replaceContent(QString::fromUtf8(u8"🦋前\n\n> - caption\n"));
        for (const auto& action : {QStringLiteral("image"), QStringLiteral("removeImage")})
        {
            const auto image_revision = parse(mirrorfly::office_snapshot()).value("revision");
            toolbox.beginResponse(image_revision.toString(), 1);
            finished = false;
            const int caret = editor.content().indexOf(action == "image" ? "caption" : "![");
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{{"start", caret}, {"end", action == "image" ? caret + 7 : caret},
                            {"action", action},
                            {"options", QJsonObject{{"url", "../photo.png"}, {"alt", "caption"}}}}},
                    {"expectedRevision", image_revision}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            const bool image_completed = wait_for([&]()
            {
                return finished;
            });
            const auto images = mirrorfly::markdown_images(editor.content().toUtf8().toStdString());
            check(image_completed && result.value("ok").toBool() &&
                    images.size() == (action == "image" ? 1 : 0) && editor.content().contains("> - "),
                "named AI image actions preserve source UTF-16 offsets and parent hierarchy");
        }
        editor.replaceContent(QString::fromUtf8(u8"🦋前\n\n> - a\r\n>   b after\r\n> - tail\r\n"));
        for (const auto& action : {QStringLiteral("inlineCode"), QStringLiteral("removeInlineCode")})
        {
            const auto code_revision = parse(mirrorfly::office_snapshot()).value("revision");
            toolbox.beginResponse(code_revision.toString(), 1);
            finished = false;
            const int caret = action == "inlineCode" ? editor.content().indexOf("> - ") + 4
                                                     : editor.content().indexOf("`a");
            const int end = action == "inlineCode" ? editor.content().indexOf(" after") : caret;
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{
                            {"start", caret}, {"end", end}, {"action", action}, {"options", QJsonObject{}}}},
                    {"expectedRevision", code_revision}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            const bool code_completed = wait_for([&]()
            {
                return finished;
            });
            const auto spans = mirrorfly::markdown_code_spans(editor.content().toUtf8().toStdString());
            const auto code_paragraphs =
                mirrorfly::markdown_paragraphs(editor.content().toUtf8().toStdString());
            check(code_completed && result.value("ok").toBool() && code_paragraphs.size() == 3 &&
                    code_paragraphs[1].containers.size() == 2 &&
                    spans.size() == (action == "inlineCode" ? 1 : 0) &&
                    (spans.empty() || spans[0].text == "a b"),
                "named AI multiline code actions preserve UTF-16 positions and quote/list ownership");
        }
        for (const auto& action :
            {QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("strike")})
        {
            editor.replaceContent(QString::fromUtf8(u8"> - 🦋L \t R\n> - tail\n"));
            const auto whitespace_revision = parse(mirrorfly::office_snapshot()).value("revision");
            toolbox.beginResponse(whitespace_revision.toString(), 1);
            finished = false;
            const int whitespace_start = editor.content().indexOf("L") + 1;
            toolbox.execute("office_action",
                {{"op", "text.formatMarkdown"},
                    {"args",
                        QJsonObject{{"start", whitespace_start}, {"end", whitespace_start + 3},
                            {"action", action}, {"options", QJsonObject{}}}},
                    {"expectedRevision", whitespace_revision}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            const bool whitespace_completed = wait_for([&]()
            {
                return finished;
            });
            const auto whitespace_paragraphs =
                mirrorfly::markdown_paragraphs(editor.content().toUtf8().toStdString());
            std::string styled_whitespace;
            if (whitespace_paragraphs.size() == 2)
                for (const auto& run : whitespace_paragraphs[0].runs)
                    if ((action == "bold" && run.style.bold) || (action == "italic" && run.style.italic) ||
                        (action == "strike" && run.style.strike))
                        styled_whitespace += run.style.text;
            check(whitespace_completed && result.value("ok").toBool() && styled_whitespace == " \t " &&
                    whitespace_paragraphs.size() == 2 && whitespace_paragraphs[0].containers.size() == 2,
                "actual named AI emphasis preserves whitespace styles and source parent hierarchy");
        }
    }

    void test_styles(const QString& directory)
    {
        using namespace mirrorfly;
        PresentationBridge slides;
        slides.requestNew();
        check(wait_for(
                  [&]()
        {
            return slides.active() && !slides.locked();
        }),
            "new presentation ready");
        AutomationBridge automation;
        ReadyRoot root;
        automation.registerModule("slides", &slides);
        automation.setUiRoot(&root);
        auto snapshot = []()
        {
            return parse(office_snapshot());
        };
        OfficeAiSequence sequence(snapshot, [](const QJsonObject& action)
        {
            auto request = action;
            request.insert("version", 1);
            return parse(office_execute(QJsonDocument(request).toJson(QJsonDocument::Compact).toStdString()));
        });
        const QJsonArray recipe{
            QJsonObject{{"edit", "formatText"},
                {"options", QJsonObject{{"fontSize", 34}, {"bold", true}, {"textColor", "#10233F"}}}},
            QJsonObject{{"edit", "formatTextBox"},
                {"options",
                    QJsonObject{{"insetLeft", 0}, {"insetRight", 0}, {"insetTop", 0}, {"insetBottom", 0}}}}};
        QJsonArray batch{step("applyEdit", {"background", QJsonObject{{"color", "#F3F6FC"}}}),
            step("applyEdit",
                {"addText",
                    QJsonObject{{"text", "AI design validation"}, {"x", 48}, {"y", 38}, {"width", 760},
                        {"height", 64}}})};
        for (const auto& item : recipe)
        {
            const auto edit = item.toObject();
            batch.append(step("applyEdit", {edit.value("edit"), edit.value("options")}));
        }
        QJsonObject result;
        sequence.start(batch, snapshot().value("revision").toString(), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && result.value("ok").toBool(),
            "published style recipe executes through actual Office public API");
        const auto selection = slides.selection();
        check(selection.value("fontSize").toInt() == 34 && selection.value("bold").toBool(),
            "title hierarchy is applied to created text");
        QJsonArray card{step("applyEdit",
            {"addShape",
                QJsonObject{
                    {"geometry", "roundRect"}, {"x", 48}, {"y", 130}, {"width", 250}, {"height", 190}}})};
        const QJsonArray card_recipe{QJsonObject{{"edit", "formatShape"},
            {"options",
                QJsonObject{{"fillColor", "#FFFFFF"}, {"outlineWidth", 0}, {"shadowEnabled", true},
                    {"shadowColor", "#10233F"}, {"shadowOpacity", 0.12}, {"shadowBlur", 6}, {"shadowX", 0},
                    {"shadowY", 2}}}}};
        for (const auto& item : card_recipe)
        {
            const auto edit = item.toObject();
            card.append(step("applyEdit", {edit.value("edit"), edit.value("options")}));
        }
        sequence.start(card, snapshot().value("revision").toString(), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && result.value("ok").toBool(),
            "card shadow style recipe executes through public API");
        const QString document_key = office_ai_document_key(snapshot());
        const int page_count = slides.slideCount();
        sequence.start(QJsonArray{step("applyEdit", {"addSlide", QJsonObject{{"layout", "blank"}}})},
            snapshot().value("revision").toString(), [&](const QJsonObject& value)
        {
            result = value;
        });
        check(wait_for(
                  [&]()
        {
            return !sequence.active();
        }) && result.value("ok").toBool() &&
                slides.slideCount() == page_count + 1 && office_ai_document_key(snapshot()) == document_key,
            "append a page in the existing document without app.new or identity change");
        const QString path = directory + "/styled.pptx";
        slides.saveAs();
        slides.selectSaveFile(QUrl::fromLocalFile(path));
        check(wait_for(
                  [&]()
        {
            return !slides.busy() && !slides.locked();
        }) && !slides.modified(),
            "styled document saves without a window");
        const auto reopened = load_presentation_file(path.toStdString());
        check(reopened.error == PresentationError::None, "styled PPTX reloads");
        bool title_found = false;
        bool shadow_found = false;
        for (const auto& page : reopened.scene.slides)
        {
            for (const auto& shape : page.shapes)
            {
                shadow_found = shadow_found || shape.effects.shadow_opacity > 0;
                for (const auto& paragraph : shape.text.paragraphs)
                    for (const auto& run : paragraph.runs)
                        if (run.text == "AI design validation")
                            title_found = run.font_size == 34 && run.bold;
            }
        }
        check(title_found && shadow_found, "typography and effects survive PPTX serialization and readback");
    }
}

int run_office_ai_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    check(directory.isValid(), "isolated test directory");
    const auto cold_tools = QJsonDocument(mirrorfly::office_ai_tools({})).toJson();
    check(mirrorfly::office_ai_tools({}).size() == 8 && cold_tools.contains("office_new") &&
            cold_tools.contains("mindmap"),
        "cold start exposes discovery, native document creation and durable task checklist");
    const auto slides = QJsonDocument(mirrorfly::office_ai_tools({"slides", "actions", "media"})).toJson();
    check(slides.contains("office_batch") && !slides.contains("memory_search"), "group tools load on demand");
    for (const auto* module : {"word", "sheets", "slides"})
    {
        const auto editing = QJsonDocument(mirrorfly::office_ai_tools({module, "actions"})).toJson();
        const auto creating =
            QJsonDocument(mirrorfly::office_ai_tools({module, "actions", "compose"})).toJson();
        check(!editing.contains("office_compose_") && creating.contains("office_compose_"),
            "bulk creation schemas load only on demand for the selected Office page");
    }
    check(!slides.contains("office_compose_slides") && slides.contains("office_image_fetch") &&
            slides.contains("office_style"),
        "editing retains independent image/style helpers without the bulk page recipe");
    check(mirrorfly::office_ai_permitted("slides", "saveTo") &&
            !mirrorfly::office_ai_permitted("slides", "resolveUnsaved"),
        "AI can save to a new path but cannot discard unsaved user changes");
    test_sequence();
    test_sequence_notifications();
    test_styles(directory.path());
    test_markdown_style_tool(directory.path());
    test_markdown_find_tool(directory.path());
    test_stream();
    test_mindmap(directory.path());
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_office_ai_tests(argc, argv);
}

#include "office_ai_tests.moc"
