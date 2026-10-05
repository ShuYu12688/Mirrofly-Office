#include "automation_bridge.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/office_ai.hpp>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }

    QJsonObject json_object(const std::string& json)
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
    }

    std::string request(
        const QString& module, const QString& action, const QJsonArray& arguments, const QString& revision)
    {
        return QJsonDocument(QJsonObject{{"version", 1}, {"module", module}, {"action", action},
                                 {"args", arguments}, {"expectedRevision", revision}})
            .toJson(QJsonDocument::Compact)
            .toStdString();
    }

    QString current_revision()
    {
        return json_object(mirrorfly::office_snapshot()).value("revision").toString();
    }

    void run_qml_state()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"qml(
            import QtQml
            QtObject {
                property string module: "home"
                property bool pendingInput: false
                function automationReady() { return !pendingInput; }
                function automationState() {
                    return {module: module, pendingInput: pendingInput,
                        selection: {id: "object-1", text: "nested state"},
                        blockers: pendingInput ? ["editor_dialog"] : []};
                }
            }
        )qml",
            QUrl());
        std::unique_ptr<QObject> root(component.create());
        expect(root != nullptr, "real QML state fixture loads");
        if (!root)
            return;
        mirrorfly::AutomationBridge automation;
        automation.setUiRoot(root.get());
        const auto before = json_object(mirrorfly::office_runtime_snapshot());
        const auto ui = before.value("ui").toObject();
        expect(ui.value("state").isObject() && ui.value("state").toObject().value("module") == "home",
            "QML JavaScript workspace is a JSON object with the actual current module");
        root->setProperty("module", "slides");
        root->setProperty("pendingInput", true);
        const auto after = json_object(mirrorfly::office_runtime_snapshot());
        const auto location = after.value("ui").toObject().value("state").toObject();
        expect(location.value("selection").toObject().value("id") == "object-1" &&
                location.value("blockers").toArray() == QJsonArray{"editor_dialog"} &&
                !after.value("ui").toObject().value("ready").toBool(),
            "QML nested selection and pending input survive the public interface");
        expect(before.value("revision") != after.value("revision"),
            "QML location and input changes invalidate previous automation revisions");
    }

    class FakeRoot final : public QObject
    {
        Q_OBJECT

    public:
        bool ready = true;
        QVariantMap state{{"draft", false}, {"selection", "root"}};
        QString playback_action;
        Q_INVOKABLE bool automationSlideMedia(int shape, const QString& action, double value)
        {
            playback_action = action;
            return shape == 2 && action == "seek" && value == 3.5;
        }
        Q_INVOKABLE bool automationSlideAnimation(const QString& action, double value)
        {
            playback_action = action;
            return action == "seek" && value == 1;
        }
        Q_INVOKABLE QVariantMap automationSlidePlayback() const
        {
            return {{"animation", QVariantMap{{"click", 2}}}};
        }

        Q_INVOKABLE bool automationReady() const
        {
            return ready;
        }

        Q_INVOKABLE QVariantMap automationState() const
        {
            return state;
        }

        void setReady(bool value)
        {
            ready = value;
            emit automationChanged();
        }

    signals:
        void automationChanged();
        void frameSwapped();
    };

    class FakeText final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool active READ active NOTIFY stateChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
        Q_PROPERTY(bool markdown READ markdown NOTIFY documentChanged)
        Q_PROPERTY(QString content READ content NOTIFY contentChanged)
        Q_PROPERTY(int revision READ revision NOTIFY documentChanged)
        Q_PROPERTY(QString documentName READ documentName NOTIFY documentChanged)

    public:
        bool active() const
        {
            return true;
        }

        bool modified() const
        {
            return modified_;
        }

        bool busy() const
        {
            return false;
        }

        bool locked() const
        {
            return false;
        }

        bool markdown() const
        {
            return false;
        }

        QString content() const
        {
            return content_;
        }

        int revision() const
        {
            return revision_;
        }

        QString documentName() const
        {
            return "fake.txt";
        }

        Q_INVOKABLE void replaceContent(const QString& text)
        {
            content_ = text;
            modified_ = true;
            ++revision_;
            emit contentChanged();
            emit documentChanged();
            emit stateChanged();
        }

        Q_INVOKABLE void requestNew()
        {
            content_.clear();
            modified_ = false;
            ++revision_;
            emit contentChanged();
            emit documentChanged();
            emit stateChanged();
        }

        Q_INVOKABLE bool requestWindowClose()
        {
            return allow_close;
        }

        Q_INVOKABLE void save()
        {
            save_requested = true;
            emit stateChanged();
        }

        Q_INVOKABLE void finishHandoff(bool)
        {
            forbidden_called = true;
        }

        void setLargeContent()
        {
            content_ = QString(2 * 1024 * 1024, QLatin1Char('x'));
            emit contentChanged();
        }

        bool allow_close = false;
        bool save_requested = false;
        bool forbidden_called = false;

    signals:
        void stateChanged();
        void documentChanged();
        void contentChanged();

    private:
        QString content_ = "initial";
        int revision_ = 1;
        bool modified_ = false;
    };

    class FakeSheets final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool active MEMBER active_)
    public:
        bool active_ = true;

        Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)

    public:
        qulonglong revision() const
        {
            return revision_;
        }

        Q_INVOKABLE bool resizeSelection(bool columns, double size)
        {
            if (size <= 0)
            {
                return false;
            }
            resized_columns = columns;
            resized_size = size;
            ++revision_;
            emit stateChanged();
            return true;
        }

        Q_INVOKABLE QVariantMap snapshot() const
        {
            return {{"address", "B2"}, {"value", "42"}};
        }

        bool resized_columns = false;
        double resized_size = 0;

    signals:
        void stateChanged();

    private:
        qulonglong revision_ = 1;
    };

    class FakeWord final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool active MEMBER active_)
    public:
        bool active_ = true;

    public:
        Q_INVOKABLE bool format(int start, int end, const QString& action, const QVariant& value)
        {
            received_start = start;
            received_end = end;
            received_action = action;
            received_value = value;
            emit editorChanged();
            return true;
        }

        int received_start = -1;
        int received_end = -1;
        QString received_action;
        QVariant received_value;

    signals:
        void editorChanged();
    };

    class FakeCanvas final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool active MEMBER active_)
    public:
        bool active_ = true;

        Q_PROPERTY(QString kind READ kind CONSTANT)
        Q_PROPERTY(QVariantMap viewData READ viewData NOTIFY viewChanged)

    public:
        QString kind() const
        {
            return "mindmap";
        }

        QVariantMap viewData() const
        {
            return {{"selectedNode", selected_node_}};
        }

        Q_INVOKABLE bool execute(const QString& action, const QVariantMap& arguments)
        {
            last_action = action;
            last_arguments = arguments;
            emit viewChanged();
            return action != "reject";
        }

        Q_INVOKABLE QVariantMap snapshot() const
        {
            return {{"nodeCount", 2}, {"selectedNode", selected_node_}};
        }

        Q_INVOKABLE void selectNode(const QString& id)
        {
            selected_node_ = id;
            emit viewChanged();
        }

        Q_INVOKABLE QString outline() const
        {
            return "- root\n  - child\n";
        }

        Q_INVOKABLE void clear()
        {
            forbidden_called = true;
        }

        QString last_action;
        QVariantMap last_arguments;
        bool forbidden_called = false;

    signals:
        void viewChanged();
        void imageChanged();

    private:
        QString selected_node_ = "root";
    };

    class FakeApp final : public QObject
    {
        Q_OBJECT

    public:
        Q_INVOKABLE void requestCreate(const QString& kind)
        {
            created_kind = kind;
        }

        Q_INVOKABLE void selectFile(const QUrl& url)
        {
            opened_url = url;
        }

        QString created_kind;
        QUrl opened_url;
    };

    void run_cases()
    {
        using namespace mirrorfly;
        FakeRoot root;
        FakeText text;
        FakeWord word;
        FakeSheets sheets;
        FakeCanvas mindmap;
        FakeApp app;
        int closed_calls = 0;
        OfficeChangeSubscription closed_subscription = 0;
        expect(json_object(office_ai_contract()).value("error") == "unavailable" &&
                office_subscribe_changes(
                    [](const std::string&)
        {
        }) == 0,
            "AI preparation entry points require a live session");
        {
            AutomationBridge bridge;
            expect(bridge.setUiRoot(&root) && bridge.registerModule("text", &text) &&
                    bridge.registerModule("word", &word) && bridge.registerModule("sheets", &sheets) &&
                    bridge.registerModule("mindmap", &mindmap) && bridge.registerModule("app", &app) &&
                    bridge.registerModule("slides", &sheets),
                "public module objects register on the GUI thread");

            int notifications = 0;
            const auto subscription = office_subscribe_changes([&](const std::string& revision)
            {
                ++notifications;
                expect(QString::fromStdString(revision) == current_revision(),
                    "observers can safely read current state without a copied document payload");
            });
            expect(subscription != 0 && notifications == 0, "subscription never calls its observer inline");
            QCoreApplication::processEvents();
            expect(notifications == 1, "subscription queues one initial state invalidation");
            const auto frame_revision = current_revision();
            emit root.frameSwapped();
            QCoreApplication::processEvents();
            expect(frame_revision == current_revision() && notifications == 1,
                "window paint signals never invalidate a document command or publish false changes");
            emit mindmap.imageChanged();
            QCoreApplication::processEvents();
            expect(frame_revision == current_revision() && notifications == 1,
                "canvas preview completion preserves document revision and does not publish false edits");
            text.replaceContent("observer setup");
            text.replaceContent("initial");
            expect(notifications == 1, "several module signals do not invoke observers synchronously");
            QCoreApplication::processEvents();
            expect(notifications == 2, "one event-loop turn coalesces a burst of document changes");

            OfficeChangeSubscription self = 0;
            int self_calls = 0;
            self = office_subscribe_changes([&](const std::string&)
            {
                ++self_calls;
                expect(office_unsubscribe_changes(self), "an observer can remove itself");
            });
            const auto throwing = office_subscribe_changes([](const std::string&)
            {
                throw std::runtime_error("observer failure");
            });
            QCoreApplication::processEvents();
            root.setReady(true);
            QCoreApplication::processEvents();
            expect(self_calls == 1 && office_unsubscribe_changes(throwing),
                "self-removal and callback exceptions leave the session usable");

            const auto contract = json_object(office_ai_contract());
            const auto scoped = contract.value("modules").toObject();
            expect(contract.value("contractVersion").toInt() == office_ai_contract_version &&
                    scoped.size() == 9 && scoped.contains("app") && scoped.contains("word") &&
                    scoped.contains("slides") && scoped.contains("export") && scoped.contains("images") &&
                    scoped.contains("mindmap") && scoped.contains("pdf") && scoped.contains("text") &&
                    scoped.contains("sheets"),
                "the public AI contract covers all document modules through the shared executor");

            const auto catalog_text = office_action_catalog();
            const auto catalog = json_object(catalog_text);
            expect(catalog.value("ok").toBool() && catalog.value("protocol").toInt() == 1 &&
                    catalog_text.find("finishHandoff") == std::string::npos &&
                    catalog_text.find("loadEditor") == std::string::npos &&
                    catalog_text.find("\"name\":\"clear\"") == std::string::npos,
                "catalog exposes protocol v1 actions without lifecycle bypasses or raw document injection");
            expect(catalog_text.find("semanticTree") != std::string::npos &&
                    catalog_text.find("selectObject") != std::string::npos &&
                    catalog_text.find("editSchema") != std::string::npos,
                "presentation semantic read and stable selection actions are public");

            auto revision = current_revision();
            expect(json_object(office_execute(request("slides", "media", {2, "seek", 3.5}, revision)))
                        .value("ok")
                        .toBool() &&
                    root.playback_action == "seek",
                "media command routes through the public UI composition method");
            expect(
                json_object(office_execute(request("slides", "animation", {"seek", 1}, current_revision())))
                    .value("ok")
                    .toBool(),
                "animation command uses typed UI arguments");
            expect(json_object(office_execute(request("slides", "playbackSnapshot", {}, current_revision())))
                       .value("ok")
                       .toBool(),
                "playback snapshot is available separately from document revisions");
            expect(json_object(office_execute(
                                   request("slides", "media", {"invalid", "seek", 3.5}, current_revision())))
                        .value("error") == "invalid_arguments",
                "playback rejects incorrectly typed arguments");
            expect(
                json_object(office_execute(request("sheets", "requestNew", {}, revision))).value("error") ==
                    "unknown_action",
                "new/open commands cannot bypass the app router");
            sheets.active_ = false;
            expect(json_object(
                       office_execute(request("sheets", "resizeSelection", {true, 24.0}, current_revision())))
                        .value("error") == "inactive_module",
                "inactive modules cannot be edited");
            sheets.active_ = true;

            auto result =
                json_object(office_execute(request("text", "replaceContent", {"changed"}, revision)));
            expect(result.value("ok").toBool() && text.content() == "changed" &&
                    result.value("revision").toString() != revision,
                "typed text edits execute and advance the monotonic revision");
            expect(json_object(office_execute(request("text", "replaceContent", {"stale"}, revision)))
                            .value("error") == "stale_revision" &&
                    text.content() == "changed",
                "stale commands are rejected before mutation");

            revision = current_revision();
            root.setReady(false);
            revision = current_revision();
            result = json_object(office_execute(request("text", "replaceContent", {"blocked"}, revision)));
            expect(result.value("error") == "not_ready" && text.content() == "changed",
                "an uncommitted UI state blocks mutation without clearing the user's draft");
            root.setReady(true);

            revision = current_revision();
            result =
                json_object(office_execute(request("text", "requestWindowClose", QJsonArray{}, revision)));
            expect(!result.value("ok").toBool() && result.value("error") == "action_rejected" &&
                    result.value("result").isBool() && !result.value("result").toBool(),
                "a false method result is distinct from an accepted void action");
            revision = current_revision();
            result = json_object(office_execute(request("text", "save", QJsonArray{}, revision)));
            expect(result.value("ok").toBool() &&
                    result.value("result").toObject().value("status") == "pending" && text.save_requested,
                "asynchronous save initiation is reported as pending");

            revision = current_revision();
            result =
                json_object(office_execute(request("sheets", "resizeSelection", {true, 24.5}, revision)));
            expect(result.value("ok").toBool() && sheets.resized_columns && sheets.resized_size == 24.5,
                "strict boolean and finite-number arguments reach a whitelisted sheet action");
            revision = current_revision();
            result =
                json_object(office_execute(request("sheets", "resizeSelection", {true, "wide"}, revision)));
            expect(result.value("error") == "invalid_arguments",
                "argument types are validated before reflective invocation");

            revision = current_revision();
            result = json_object(office_execute(request("word", "format", {0, 4, "bold", true}, revision)));
            expect(result.value("ok").toBool() && word.received_start == 0 && word.received_end == 4 &&
                    word.received_action == "bold" && word.received_value.toBool(),
                "a typed JSON scalar reaches a whitelisted QVariant parameter without raw object access");
            result = json_object(office_execute(request("word", "format",
                {0, 4, "lineSpacing", QJsonObject{{"rule", 1}, {"value", 18.5}}}, current_revision())));
            expect(result.value("ok").toBool() && word.received_value.toMap().value("value") == 18.5,
                "structured Word line-spacing values reach the same public format method");
            const QJsonArray tab_stops{
                QJsonObject{{"position", 72.05}, {"alignment", "decimal"}, {"leader", "dot"}}};
            result = json_object(
                office_execute(request("word", "format", {0, 4, "tabStops", tab_stops}, current_revision())));
            expect(result.value("ok").toBool() && word.received_action == "tabStops" &&
                    QJsonValue::fromVariant(word.received_value) == tab_stops,
                "a json_value array reaches the whitelisted QVariant parameter without losing its structure");
            result = json_object(office_execute(
                request("word", "format", {0, 4, "tabStops", QJsonArray{}}, current_revision())));
            expect(result.value("ok").toBool() && word.received_value.toList().isEmpty(),
                "an empty json_value array remains an explicit replacement value");
            expect(json_object(office_execute(request("word", "format",
                                   {0, 4, "tabStops", QJsonValue(QJsonValue::Null)}, current_revision())))
                        .value("error") == "invalid_arguments",
                "null json_value parameters still reject before invoking document code");

            revision = current_revision();
            result = json_object(office_execute(request(
                "mindmap", "execute", {"rename", QJsonObject{{"id", "root"}, {"text", "new"}}}, revision)));
            expect(result.value("ok").toBool() && mindmap.last_action == "rename" &&
                    mindmap.last_arguments.value("id") == "root",
                "canvas commands accept only the declared string and object parameters");
            revision = current_revision();
            result = json_object(office_execute(request("mindmap", "clear", {}, revision)));
            expect(result.value("error") == "unknown_action" && !mindmap.forbidden_called,
                "unlisted public methods cannot be reached through reflection");
            result = json_object(office_execute(request("text", "finishHandoff", {true}, revision)));
            expect(result.value("error") == "unknown_action" && !text.forbidden_called,
                "handoff completion remains unavailable to automation");

            revision = current_revision();
            result =
                json_object(office_execute(request("app", "open", {"C:/documents/example.mm"}, revision)));
            expect(result.value("ok").toBool() && app.opened_url.isLocalFile(),
                "app open routes a validated local file URL through its public entry point");
            revision = current_revision();
            result = json_object(
                office_execute(request("app", "open", {"https://example.invalid/x.mm"}, revision)));
            expect(result.value("error") == "invalid_arguments",
                "app routing rejects non-local URLs before invocation");

            QJsonObject invalid_version{{"version", 2}, {"module", "text"}, {"action", "save"},
                {"args", QJsonArray{}}, {"expectedRevision", current_revision()}};
            expect(json_object(office_execute(QJsonDocument(invalid_version).toJson().toStdString()))
                        .value("error") == "unsupported_version",
                "unknown protocol versions are rejected");
            invalid_version.insert("version", 1);
            invalid_version.insert("extra", true);
            expect(json_object(office_execute(QJsonDocument(invalid_version).toJson().toStdString()))
                        .value("error") == "invalid_schema",
                "unknown schema fields are rejected");
            QJsonObject implicit_version{{"module", "sheets"}, {"action", "snapshot"}, {"args", QJsonArray{}},
                {"expectedRevision", current_revision()}};
            expect(json_object(office_execute(QJsonDocument(implicit_version).toJson().toStdString()))
                       .value("ok")
                       .toBool(),
                "an omitted version uses protocol v1");
            QJsonValue nested = true;
            for (int depth = 0; depth < 70; ++depth)
            {
                nested = QJsonArray{nested};
            }
            QJsonObject deep{{"version", 1}, {"module", "mindmap"}, {"action", "execute"},
                {"args", QJsonArray{"rename", QJsonObject{{"nested", nested}}}},
                {"expectedRevision", current_revision()}};
            expect(json_object(office_execute(QJsonDocument(deep).toJson().toStdString())).value("error") ==
                    "invalid_json",
                "deeply nested JSON is rejected before reflective invocation");
            expect(json_object(office_execute(std::string(1024 * 1024 + 1, 'x'))).value("error") ==
                    "request_too_large",
                "requests over 1 MiB are rejected before JSON parsing");

            const auto state = json_object(office_snapshot());
            const auto modules = state.value("modules").toObject();
            expect(state.value("ok").toBool() &&
                    modules.value("text").toObject().value("content") == "changed" &&
                    modules.value("sheets").toObject().value("snapshot").toObject().value("address") ==
                        "B2" &&
                    modules.value("mindmap").toObject().value("outline") == "- root\n  - child\n",
                "snapshot exposes intentional document, selection and view data through public interfaces");

            text.setLargeContent();
            const auto bounded = office_snapshot();
            expect(bounded.size() <= 2 * 1024 * 1024 && json_object(bounded).value("truncated").toBool(),
                "oversized snapshots stay valid JSON and report truncation under 2 MiB");

            std::string threaded;
            std::string threaded_contract;
            OfficeChangeSubscription threaded_subscription = 1;
            std::thread worker([&]()
            {
                threaded = office_snapshot();
                threaded_contract = office_ai_contract();
                threaded_subscription = office_subscribe_changes([](const std::string&)
                {
                });
            });
            worker.join();
            expect(json_object(threaded).value("error") == "wrong_thread",
                "off-thread calls fail immediately without queued or blocking invocation");
            expect(
                json_object(threaded_contract).value("error") == "wrong_thread" && threaded_subscription == 0,
                "contract discovery and event subscription also reject worker-thread calls");
            expect(office_unsubscribe_changes(subscription), "owner releases its observer before teardown");
            std::vector<OfficeChangeSubscription> capacity;
            for (int index = 0; index < 65; ++index)
                capacity.push_back(office_subscribe_changes([](const std::string&)
                {
                }));
            expect(capacity[63] != 0 && capacity[64] == 0, "observer capacity is explicitly bounded");
            for (const auto id : capacity)
                office_unsubscribe_changes(id);
            closed_subscription = office_subscribe_changes([&](const std::string&)
            {
                ++closed_calls;
            });
        }
        QCoreApplication::processEvents();
        expect(closed_calls == 0 && !office_unsubscribe_changes(closed_subscription),
            "session teardown cancels queued callbacks and releases subscription handles");
        {
            AutomationBridge replacement;
            const auto id = office_subscribe_changes([](const std::string&)
            {
            });
            expect(id > closed_subscription && !office_unsubscribe_changes(closed_subscription),
                "a stale handle cannot remove an observer in a replacement session");
            office_unsubscribe_changes(id);
        }
        expect(json_object(office_snapshot()).value("error") == "unavailable",
            "global entry points close when their unique QPointer-backed instance is destroyed");
    }
}

int run_automation_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    run_cases();
    run_qml_state();
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_automation_tests(argc, argv);
}

#include "automation_tests.moc"
