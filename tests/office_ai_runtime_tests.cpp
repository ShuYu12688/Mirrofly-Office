#include "automation_bridge.hpp"
#include "office_ai_context.hpp"
#include "office_ai_contract_adapter.hpp"
#include "office_ai_log.hpp"
#include "office_ai_run.hpp"
#include "office_ai_toolbox.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <iostream>

namespace
{
    int failures = 0;

    void check(bool value, const char* description)
    {
        if (!value)
        {
            ++failures;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    bool wait_for(const std::function<bool()>& condition)
    {
        QElapsedTimer clock;
        clock.start();
        while (!condition() && clock.elapsed() < 6000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return condition();
    }

    void test_wrapped_arguments()
    {
        const QJsonObject value_signature{
            {"ok", true}, {"parameters", QJsonArray{QJsonObject{{"name", "value"}, {"type", "json_value"}}}}};
        const QJsonArray stop_array{
            QJsonObject{{"position", 72.05}, {"alignment", "decimal"}, {"leader", "dot"}}};
        for (const auto& array : {stop_array, QJsonArray{}})
        {
            const auto normalized = mirrorfly::office_ai_normalize_action(
                {{"module", "word"}, {"action", "format"}, {"args", QJsonObject{{"value", array}}}},
                value_signature);
            check(normalized.value("ok").toBool() &&
                    normalized.value("step").toObject().value("args").toArray() ==
                        QJsonArray{QJsonValue(array)},
                "json_value arrays normalize losslessly, including explicit empty replacements");
        }
        check(!mirrorfly::office_ai_normalize_action(
                  {{"module", "word"}, {"action", "format"}, {"args", QJsonObject{{"value", QJsonValue()}}}},
                  value_signature)
                  .value("ok")
                  .toBool(),
            "null json_value cannot silently become an empty array replacement");
        const QJsonObject save_signature{{"ok", true}, {"parameters", QJsonArray{}}};
        const auto save = [&](const QJsonObject& args, const QString& action = "save")
        {
            return mirrorfly::office_ai_normalize_action(
                {{"module", "word"}, {"action", action}, {"args", args}}, save_signature, -1);
        };
        check(save({{"current", true}}) == save({}) && save({}).value("ok").toBool(),
            "explicit current-save alias reaches the same zero-argument public save contract");
        for (const auto& invalid : {QJsonObject{{"current", false}}, QJsonObject{{"current", "true"}},
                 QJsonObject{{"current", true}, {"path", "C:/other.docx"}},
                 QJsonObject{{"current", true}, {"title", "other"}}})
            check(!save(invalid).value("ok").toBool(),
                "save compatibility cannot silently discard destination or false/invalid intent");
        check(!save({{"current", true}}, "undo").value("ok").toBool(),
            "current-save alias cannot normalize parameters for unrelated actions");
        const QJsonObject file_signature{{"ok", true},
            {"parameters", QJsonArray{QJsonObject{{"name", "destination"}, {"type", "local_file"}}}}};
        const auto save_to = [&](const QJsonObject& args, const QString& action = "saveTo")
        {
            return mirrorfly::office_ai_normalize_action(
                {{"module", "slides"}, {"action", action}, {"args", args}}, file_signature);
        };
        check(save_to({{"path", "C:/copy.pptx"}}) == save_to({{"destination", "C:/copy.pptx"}}) &&
                save_to({{"path", "C:/copy.pptx"}}).value("ok").toBool(),
            "sole saveTo path aliases the public local-file destination without changing its value");
        check(!save_to({{"path", "C:/copy.pptx"}, {"destination", "C:/other.pptx"}}).value("ok").toBool() &&
                !save_to({{"path", "C:/copy.pptx"}, {"overwrite", true}}).value("ok").toBool() &&
                !save_to({{"path", "https://example.invalid/file"}}).value("ok").toBool() &&
                !save_to({{"path", 12}}).value("ok").toBool() &&
                !save_to({{"path", "C:/copy.pptx"}}, "open").value("ok").toBool(),
            "path alias cannot hide conflicting destinations, options, remote paths or other operations");
        const QJsonObject signature{
            {"ok", true}, {"parameters", QJsonArray{QJsonObject{{"name", "patch"}, {"type", "object"}}}}};
        const auto normalize = [&](const QJsonValue& args)
        {
            return mirrorfly::office_ai_normalize_action(
                {{"module", "sheets"}, {"action", "formatSelection"}, {"args", args}}, signature, -1);
        };
        const QJsonObject patch{{"shrinkToFit", true}};
        const auto direct = normalize(QJsonObject{{"patch", patch}});
        check(direct.value("ok").toBool() &&
                direct == normalize(QStringLiteral("{\"patch\":{\"shrinkToFit\":true}}")) &&
                direct == normalize(QStringLiteral("[{\"shrinkToFit\":true}]")),
            "one accidental JSON string wrapper normalizes to the same strictly checked action");
        const QJsonObject envelope{{"op", "sheets.formatSelection"}, {"args", QJsonObject{{"patch", patch}}}};
        check(normalize(envelope) == direct &&
                normalize(QJsonObject{{"op", "sheets.formatSelection"}, {"args", QJsonArray{patch}}}) ==
                    direct,
            "one redundant matching action envelope preserves the same normalized operation");
        check(normalize(QJsonObject{{"action", "formatSelection"}, {"patch", patch}}) == direct &&
                !normalize(QJsonObject{{"action", "saveTo"}, {"patch", patch}}).value("ok").toBool(),
            "redundant matching action name is accepted only when it is not a declared method parameter");
        const QJsonObject action_signature{{"ok", true},
            {"parameters",
                QJsonArray{QJsonObject{{"name", "action"}, {"type", "string"}},
                    QJsonObject{{"name", "options"}, {"type", "object"}}}}};
        const auto native = mirrorfly::office_ai_normalize_action(
            {{"module", "slides"}, {"action", "applyEdit"},
                {"args", QJsonObject{{"action", "applyEdit"}, {"options", QJsonObject{}}}}},
            action_signature, -1);
        check(native.value("step").toObject().value("args").toArray().first() == "applyEdit",
            "declared action argument is never removed even when its value matches the outer method");
        for (const auto& invalid :
            std::vector<QJsonObject>{{{"op", "word.format"}, {"args", QJsonObject{{"patch", patch}}}},
                {{"op", "sheets.formatSelection"}, {"args", QJsonObject{{"patch", patch}}}, {"id", "other"}},
                {{"op", "sheets.formatSelection"}, {"args", envelope}},
                {{"op", "sheets.formatSelection"}, {"args", "not an object"}}})
            check(!normalize(invalid).value("ok").toBool(),
                "ambiguous operation, extra keys, deeper envelopes and invalid nested types remain errors");
        for (const auto& invalid : {QStringLiteral("{\"patch\":{}} trailing"), QStringLiteral("{bad}"),
                 QStringLiteral("{\"patch\":true}"), QStringLiteral("{\"patch\":{},\"extra\":1}"),
                 QStringLiteral("null"), QStringLiteral("[]"), QString(65537, ' ')})
            check(!normalize(invalid).value("ok").toBool(),
                "malformed, oversized, missing or unknown wrapped parameters remain rejected");
    }

    void test_run()
    {
        using mirrorfly::OfficeAiRun;
        OfficeAiRun run;
        const QJsonObject first{{"id", "first"}, {"name", "read"}, {"input", QJsonObject{}}};
        const QJsonObject second{{"id", "second"}, {"name", "edit"}, {"input", QJsonObject{}}};
        check(!run.request() && !run.resolve("first"), "idle run rejects requests and late tool results");
        run.begin();
        const auto epoch = run.epoch();
        check(run.request() && !run.request(), "only one model request can be active");
        check(!run.accept({first, first}) && run.phase() == OfficeAiRun::Phase::Model,
            "duplicate IDs reject the whole response before executing any tool");
        check(run.accept({first, second}), "valid call exchange enters tool phase");
        check(!run.resolve("second") && run.current() == first, "out-of-order results cannot consume a call");
        check(!run.request() && run.resolve("first") && !run.resolve("first"),
            "every call completes once before the next model request");
        check(run.current() == second && run.resolve("second") && run.request(),
            "model resumes only after all paired results");
        run.cancel();
        check(run.epoch() != epoch && !run.accept({first}) && run.current().isEmpty(),
            "cancel invalidates queued work and late model replies");
    }

    void test_large_result_status()
    {
        const QJsonArray observations{
            QJsonObject{{"module", "slides"}, {"action", "readContent"},
                {"response", QJsonObject{{"ok", true}, {"text", QString(12000, 'a')}}}},
            QJsonObject{{"module", "slides"}, {"action", "readContent"},
                {"response", QJsonObject{{"ok", true}, {"text", QString(12000, 'b')}}}}};
        const QJsonObject result{{"ok", true}, {"revision", "r5"}, {"executed", 2}, {"remaining", 0},
            {"nextStep", 2}, {"file", QJsonObject{{"saved", true}, {"path", "C:/output.pptx"}}},
            {"results", observations}};
        const auto compacted = mirrorfly::office_ai_compact_result(result);
        check(compacted.value("ok").toBool() && !compacted.contains("error") &&
                compacted.value("truncated").toBool() && compacted.value("executed") == 2 &&
                compacted.value("remaining") == 0 &&
                compacted.value("file").toObject().value("saved").toBool() &&
                QJsonDocument(compacted).toJson(QJsonDocument::Compact).size() <= 16 * 1024,
            "large successful observations keep the real edit and save receipts");

        mirrorfly::OfficeAiContext context;
        context.begin("system", "read two pages");
        const QJsonObject call{
            {"id", "read-1"}, {"name", "office_batch"}, {"input", QJsonObject{{"steps", QJsonArray{}}}}};
        context.assistant({{"role", "assistant"}, {"calls", QJsonArray{call}}});
        context.result(call, result);
        QJsonObject model_result;
        for (const auto& value : context.messages({}))
            if (value.toObject().value("role") == "tool_result")
                model_result =
                    QJsonDocument::fromJson(value.toObject().value("content").toString().toUtf8()).object();
        check(model_result.value("ok").toBool() && model_result.value("truncated").toBool() &&
                model_result.value("file").toObject().value("saved").toBool(),
            "the next model request sees the truthful successful result");

        auto failed = result;
        failed.insert("ok", false);
        failed.insert("error", "object_not_found");
        const auto compacted_failure = mirrorfly::office_ai_compact_result(failed);
        check(!compacted_failure.value("ok").toBool() &&
                compacted_failure.value("error") == "object_not_found" &&
                compacted_failure.value("truncated").toBool(),
            "large failed observations retain their actual error code");
    }

    void test_diagnostics(const QString& directory)
    {
        const QString path = directory + "/log.jsonl";
        {
            QFile oversized(path);
            check(oversized.open(QIODevice::WriteOnly), "prepare diagnostic rotation");
            oversized.write(QByteArray(2 * 1024 * 1024 + 1, 'x'));
        }
        {
            mirrorfly::OfficeAiLog log(path);
            for (int index = 0; index < 100; ++index)
                log.append({{"event", "test"}, {"number", index}});
        }
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "diagnostics written without a database");
        const auto lines = file.readAll().trimmed().split('\n');
        check(lines.size() == 100 && QFile::exists(directory + "/log.previous.jsonl"),
            "worker drains accepted records and rotates oversized metadata log");
        for (int index = 0; index < lines.size(); ++index)
            check(QJsonDocument::fromJson(lines.at(index)).object().value("number").toInt() == index,
                "diagnostic writes remain ordered");
    }

    class PageRoot final : public QObject
    {
        Q_OBJECT
    public:
        QString module = "home";
        Q_INVOKABLE bool automationReady() const
        {
            return true;
        }
        Q_INVOKABLE QVariantMap automationState() const
        {
            return {{"module", module}, {"pendingInput", false}};
        }
    };

    class SchemaModule final : public QObject
    {
        Q_OBJECT
    public:
        int reads = 0;
        QVariantMap schema;
        Q_INVOKABLE QVariantMap editSchema()
        {
            ++reads;
            return schema;
        }
    };

    void test_scoped_schema()
    {
        mirrorfly::AutomationBridge automation;
        PageRoot root;
        automation.setUiRoot(&root);
        SchemaModule modules[5];
        const QStringList names{"word", "slides", "sheets", "mindmap", "pdf"};
        for (int index = 0; index < names.size(); ++index)
        {
            modules[index].schema = {{"sample", QVariantMap{{"value", "first"}}}};
            if (names[index] == "word")
                modules[index].schema = {{"formats", modules[index].schema}};
            automation.registerModule(names[index], &modules[index]);
        }
        mirrorfly::OfficeAiToolbox toolbox;
        for (int index = 0; index < names.size(); ++index)
        {
            root.module = names[index];
            toolbox.focusWorkspace();
            toolbox.query("office_load_group", {{"group", names[index]}});
            for (auto& module : modules)
                module.reads = 0;
            const auto schema =
                toolbox.query("office_schema", {{"module", names[index]}, {"name", "sample"}});
            check(schema.value("ok").toBool() && schema.value("options").toObject().value("value") == "first",
                "scoped lookup retains module edit options");
            for (int other = 0; other < names.size(); ++other)
                check(modules[other].reads == (other == index ? 1 : 0),
                    "one edit lookup reads only the requested module schema");
        }
        modules[4].schema = {{"sample", QVariantMap{{"value", "updated"}}}};
        check(toolbox.query("office_schema", {{"module", "pdf"}, {"name", "sample"}})
                    .value("options")
                    .toObject()
                    .value("value") == "updated",
            "scoped lookup never reuses stale schema state");
    }

    void test_discovery()
    {
        mirrorfly::AutomationBridge automation;
        PageRoot root;
        automation.setUiRoot(&root);
        mirrorfly::OfficeAiToolbox toolbox;
        for (const auto* module : {"text", "word", "sheets", "slides", "mindmap", "pdf", "export", "images"})
        {
            root.module = "home";
            const auto unavailable = toolbox.query("office_load_group", {{"group", module}});
            check(!unavailable.contains("guide"), "inactive pages never expose their guide");
            root.module = QString(module) == "export" || QString(module) == "images" ? "slides" : module;
            toolbox.focusWorkspace();
            check(toolbox.query("office_load_group", {{"group", module}}).value("ok").toBool(),
                "load known module");
            const auto schema = toolbox.query("office_schema", {{"module", module}, {"name", ""}});
            check(schema.value("ok").toBool() && schema.value("actions").isArray() &&
                    schema.value("names").isArray(),
                "every module has consistent action and edit-name discovery");
            const auto invalid =
                toolbox.query("office_schema", {{"module", module}, {"name", "not-a-real-name"}});
            check(!invalid.value("ok").toBool() && invalid.value("module") == module &&
                    invalid.value("requested") == "not-a-real-name" &&
                    invalid.value("availableActions").isArray(),
                "unknown schema reports requested module/name and recoverable alternatives");
        }
        check(toolbox.query("office_schema", {{"module", "app"}, {"name", ""}}).value("ok").toBool(),
            "app shares the same empty-name discovery contract");
    }
}

int run_office_ai_runtime_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "isolated runtime test directory");
    test_run();
    test_wrapped_arguments();
    test_large_result_status();
    test_diagnostics(directory.path());
    test_discovery();
    test_scoped_schema();
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_office_ai_runtime_tests(argc, argv);
}

#include "office_ai_runtime_tests.moc"
