#include "office_ai_qml_scenario.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_contract_adapter.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/presentation_storage.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLoggingCategory>
#include <QThread>

#include <iostream>

void run_office_ai_qml_scenario(mirrorfly::OfficeAiAgent& agent, office_ai_test::Transport& network,
    const QString& directory, const std::function<void(bool, const char*)>& check, bool batched)
{
    using namespace office_ai_test;
    if (batched)
        QLoggingCategory::setFilterRules("mirrorfly.ai.sequence.latency.debug=true");
    const auto catalog =
        QJsonDocument::fromJson(QByteArray::fromStdString(mirrorfly::office_action_catalog())).object();
    const auto modules = catalog.value("modules").toObject();
    int checked_actions = 0;
    int exposed_actions = 0;
    for (auto module = modules.begin(); module != modules.end(); ++module)
        for (const auto& value : module.value().toArray())
        {
            const auto action = value.toObject();
            check(
                action.value("available").toBool(), "registered QML public action has a matching signature");
            const auto name = action.value("name").toString();
            const auto signature = mirrorfly::office_ai_action_signature(catalog, module.key(), name);
            check(signature.value("ok").toBool(),
                "every registered public action has a discoverable signature");
            QJsonObject named;
            QJsonArray positional;
            for (const auto& parameter : signature.value("parameters").toArray())
            {
                const auto field = parameter.toObject();
                const auto type = field.value("type").toString();
                QJsonValue sample;
                if (type == "string" || type == "json_value")
                    sample = "sample";
                if (type == "local_file")
                    sample = "file:///C:/Temp/test.pptx";
                if (type == "integer" || type == "finite_number")
                    sample = 1;
                if (type == "boolean")
                    sample = true;
                if (type == "object")
                    sample = QJsonObject{};
                check(!sample.isNull(), "adapter covers every public argument type");
                named.insert(field.value("name").toString(), sample);
                positional.append(sample);
            }
            const QJsonObject input{{"module", module.key()}, {"action", name}, {"args", named}};
            const auto normalized = mirrorfly::office_ai_normalize_action(input, signature, 0);
            check(normalized.value("ok").toBool() &&
                    normalized.value("step").toObject().value("args") == positional,
                "all named public arguments normalize to the declared positional order");
            auto array_input = input;
            array_input.insert("args", positional);
            check(mirrorfly::office_ai_normalize_action(array_input, signature, 0) == normalized,
                "existing positional actions remain compatible");
            ++checked_actions;
            if (mirrorfly::office_ai_permitted(module.key(), name))
                ++exposed_actions;
        }
    std::cout << "Public signature audit: registered=" << checked_actions
              << ", AI exposed=" << exposed_actions << '\n';
    const auto snapshot = []()
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(mirrorfly::office_snapshot())).object();
    };
    const auto step = [](const QString& module, const QString& action, const QJsonArray& args)
    {
        return QJsonObject{{"module", module}, {"action", action}, {"args", args}};
    };
    const QString destination = directory + "/Drug prevention.pptx";
    network.respond = [&](int request_index, const QJsonObject& body) -> QJsonArray
    {
        const int turn = batched && request_index >= 10 ? request_index + 22 : request_index;
        const auto state = runtime();
        const auto workspace = mirrorfly::office_ai_workspace(state);
        check(workspace.value("ok").toBool(), "real QML location is valid for every model request");
        const auto revision = state.value("revision");
        if (turn == 0)
            return {
                call("office_task",
                    {{"mode", "create"}, {"phase", "edit"}, {"goal", "30-page PPT then native C++ roadmap"},
                        {"targets", QJsonArray{"PPT", "mindmap"}}, {"completed", QJsonArray{}},
                        {"remaining", QJsonArray{"PPT", "rewrite title", "save", "mindmap"}}}),
                call("office_load_group", {{"group", "slides"}}, 1)};
        if (turn == 1)
            return {call("office_new", {{"kind", "slides"}, {"expectedRevision", revision}})};
        if (batched && request_index >= 2 && request_index <= 9)
        {
            const int first = (request_index - 2) * 4;
            QJsonArray pages;
            for (int page = first; page < qMin(first + 4, 30); ++page)
            {
                const auto text = [&](int item)
                {
                    return QStringLiteral("Prevention page %1 item %2").arg(page + 1).arg(item);
                };
                pages.append(QJsonObject{{"title", text(0)}, {"layout", "columns"},
                    {"blocks",
                        QJsonArray{QJsonObject{{"text", text(1) + '\n' + text(2)}},
                            QJsonObject{{"text", text(3) + '\n' + text(4) + '\n' + text(5)}}}}});
            }
            return {call("office_compose_slides",
                {{"batchId", QStringLiteral("pages-%1").arg(first)}, {"targetPages", 30}, {"pages", pages}})};
        }
        if (turn >= 2 && turn <= 31)
        {
            check(workspace.value("currentModule") == "slides", "created PPT is the actual QML location");
            QJsonArray elements;
            for (int item = 0; item < 6; ++item)
                elements.append(QJsonObject{{"type", "text"},
                    {"text", QStringLiteral("Prevention page %1 item %2").arg(turn - 1).arg(item)},
                    {"x", 40 + (item % 2) * 450}, {"y", 30 + (item / 2) * 155}, {"width", 400},
                    {"height", 120},
                    {"style",
                        QJsonObject{{"fontSize", item == 0 ? 30 : 20}, {"bold", item == 0},
                            {"textColor", "#10233F"}}}});
            return {call("office_compose_slide",
                {{"page", turn == 2 ? 0 : -1}, {"background", "#F3F6FC"}, {"elements", elements},
                    {"expectedRevision", revision}})};
        }
        if (turn == 32)
        {
            check(state.value("modules").toObject().value("slides").toObject().value("slideCount") == 30,
                "all thirty composed pages complete through the real UI contract");
            return {call("office_batch",
                {{"expectedRevision", revision},
                    {"steps",
                        QJsonArray{step("slides", "setSlide", {0}),
                            QJsonObject{{"module", "slides"}, {"action", "semanticPage"},
                                {"args", QJsonObject{{"limit", "1"}}}}}}})};
        }
        if (turn == 33)
        {
            const auto detail = snapshot()
                                    .value("modules")
                                    .toObject()
                                    .value("slides")
                                    .toObject()
                                    .value("snapshot")
                                    .toObject();
            QString id;
            for (const auto& node : detail.value("nodes").toArray())
                if (node.toObject().value("index").toInt(-1) == 0)
                    id = node.toObject().value("id").toString();
            check(!id.isEmpty() && json(body).contains(id.toUtf8()),
                "rewrite uses a returned semantic object ID");
            return {call("office_schema", {{"module", "slides"}, {"name", "updateText"}}),
                call("office_batch",
                    {{"expectedRevision", revision},
                        {"steps",
                            QJsonArray{step("slides", "selectObject", {id}),
                                step("slides", "applyEdit",
                                    {"updateText", QJsonObject{{"text", "Rewritten prevention title"}}})}}},
                    1)};
        }
        if (turn == 34)
        {
            const auto selection =
                snapshot().value("ui").toObject().value("state").toObject().value("selection").toObject();
            check(selection.value("text") == "Rewritten prevention title",
                "AI rewrite updates the actual QML selection and shared document");
            return {call("office_save", {{"title", "Drug prevention"}, {"expectedRevision", revision}})};
        }
        if (turn == 35)
        {
            check(QFile::exists(destination) && json(body).contains("file:"),
                "save finishes before navigation and provides a receipt");
            return {call("office_home", {{"expectedRevision", revision}})};
        }
        if (turn == 36)
        {
            check(workspace.value("currentModule") == "home", "real navigation reaches home");
            return {call("office_load_group", {{"group", "mindmap"}})};
        }
        if (turn == 37)
            return {call("office_new", {{"kind", "mindmap"}, {"expectedRevision", revision}})};
        if (turn == 38)
            return {call("office_schema", {{"module", "mindmap"}, {"name", "createNode"}})};
        if (turn == 39)
        {
            check(workspace.value("currentModule") == "mindmap",
                "second deliverable uses the native map module");
            const auto map = snapshot()
                                 .value("modules")
                                 .toObject()
                                 .value("mindmap")
                                 .toObject()
                                 .value("snapshot")
                                 .toObject();
            const auto id = map.value("selectedId");
            QJsonArray steps{
                step("mindmap", "execute", {"rename", QJsonObject{{"id", id}, {"text", "C++ roadmap"}}})};
            for (int index = 0; index < 6; ++index)
                steps.append(step("mindmap", "execute",
                    {"createNode",
                        QJsonObject{{"id", id}, {"newId", QStringLiteral("stage-%1").arg(index)},
                            {"text", QStringLiteral("Stage %1").arg(index)}, {"x", 400},
                            {"y", index * 150}}}));
            return {call("office_batch", {{"steps", steps}, {"expectedRevision", revision}})};
        }
        if (turn == 40)
            return {call("office_task",
                {{"mode", "create"}, {"phase", "done"}, {"goal", "30-page PPT then native C++ roadmap"},
                    {"targets", QJsonArray{"PPT", "mindmap"}},
                    {"completed", QJsonArray{"PPT", "rewrite title", "save", "mindmap"}},
                    {"remaining", QJsonArray{}}})};
        check(turn == 41, "complex workflow never falls into extra model retries");
        return {};
    };
    agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
    QMap<QString, QJsonObject> action_timings;
    const auto action_connection = QObject::connect(agent.findChild<mirrorfly::OfficeAiToolbox*>(),
        &mirrorfly::OfficeAiToolbox::diagnostic, &agent, [&](const QString& event, const QJsonObject& fields)
    {
        if (event != "action")
            return;
        const auto name = fields.value("edit").toString(fields.value("action").toString());
        auto timing = action_timings.value(name);
        timing.insert("count", timing.value("count").toInt() + 1);
        for (const auto* key : {"elapsedMs", "preparationMs", "schemaMs"})
            timing.insert(key, timing.value(key).toDouble() + fields.value(key).toDouble());
        action_timings.insert(name, timing);
    });
    QElapsedTimer tool_clock;
    tool_clock.start();
    const auto timing_connection = QObject::connect(&agent, &mirrorfly::OfficeAiAgent::toolObserved, &agent,
        [&](const QString& name, const QJsonObject&, const QJsonObject& result)
    {
        std::cout
            << "QML tool timing: name=" << name.toStdString() << ", elapsedMs=" << tool_clock.restart()
            << ", timing="
            << QJsonDocument(result.value("timing").toObject()).toJson(QJsonDocument::Compact).toStdString()
            << '\n';
    });
    agent.start("Create a beautiful 30-page prevention PPT, rewrite its first title, save it, then make a "
                "native C++ roadmap.");
    QElapsedTimer clock;
    clock.start();
    while (agent.busy() && clock.elapsed() < 45000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    if (agent.busy())
    {
        const auto runtime = snapshot();
        QJsonObject waiting{{"ui", runtime.value("ui")}, {"revision", runtime.value("revision")},
            {"activity", agent.activity()}, {"status", agent.status()}};
        for (const auto* name : {"slides", "mindmap", "text"})
        {
            auto module = runtime.value("modules").toObject().value(name).toObject();
            for (const auto* key : {"snapshot", "content", "outline", "viewData", "selection"})
                module.remove(key);
            waiting.insert(name, module);
        }
        auto* trace = agent.trace();
        const int role = trace->roleNames().key("modelData");
        QJsonArray recent;
        for (int row = qMax(0, trace->rowCount() - 6); row < trace->rowCount(); ++row)
            recent.append(QJsonObject::fromVariantMap(trace->data(trace->index(row, 0), role).toMap()));
        waiting.insert("recentTrace", recent);
        std::cout << "QML stalled state: "
                  << QJsonDocument(waiting).toJson(QJsonDocument::Compact).toStdString() << '\n';
        agent.cancel();
    }
    QObject::disconnect(timing_connection);
    if (batched)
        QLoggingCategory::setFilterRules({});
    check(agent.answer() == "Finished" && network.requests.size() == (batched ? 20 : 42),
        "real QML agent completes creation, rewrite, save, home and next document without retries");
    const auto reopened = mirrorfly::load_presentation_file(destination.toStdString());
    check(reopened.error == mirrorfly::PresentationError::None && reopened.scene.slides.size() == 30,
        "thirty-page saved deliverable independently reopens");
    if (reopened.scene.slides.size() == 30)
    {
        check(mirrorfly::find_presentation_text(reopened.scene, "Rewritten prevention title").size() == 1,
            "saved bytes include the real rewrite");
        for (int page = 0; page < 30; ++page)
        {
            check(reopened.scene.slides[page].shapes.size() == (batched ? 10 : 6),
                "each page retains its complete layout without duplicate objects");
            for (int item = page == 0 ? 1 : 0; item < 6; ++item)
            {
                const auto text = QStringLiteral("Prevention page %1 item %2").arg(page + 1).arg(item);
                check(mirrorfly::find_presentation_text(reopened.scene, text.toStdString()).size() == 1,
                    "every requested page item survives native readback exactly once");
            }
        }
    }
    const auto map = snapshot().value("modules").toObject().value("mindmap").toObject();
    check(map.value("active").toBool() && map.value("outline").toString().contains("C++ roadmap"),
        "native C++ deliverable is present after preserving the PPT");
    qsizetype total = 0;
    qsizetype peak = 0;
    qsizetype peak_tools = 0;
    int compactions = 0;
    auto* trace = agent.trace();
    const int data_role = trace->roleNames().key("modelData");
    for (int row = 0; row < trace->rowCount(); ++row)
    {
        const auto title = trace->data(trace->index(row, 0), data_role).toMap().value("title").toString();
        const auto count = title.section(QStringLiteral("已整理 "), 1).section(' ', 0, 0).toInt();
        compactions = qMax(compactions, count);
    }
    check(compactions > 0, "long-task metrics include completed-exchange compactions");
    for (const auto& request : network.requests)
    {
        total += request.size();
        peak = qMax(peak, request.size());
        const auto tools = QJsonDocument::fromJson(request).object().value("tools").toArray();
        peak_tools = qMax(peak_tools, QJsonDocument(tools).toJson(QJsonDocument::Compact).size());
    }
    QObject::disconnect(action_connection);
    for (auto it = action_timings.begin(); it != action_timings.end(); ++it)
        std::cout << "QML action timing: name=" << it.key().toStdString()
                  << ", timing=" << QJsonDocument(it.value()).toJson(QJsonDocument::Compact).toStdString()
                  << '\n';
    std::cout << (batched ? "Real QML batched agent: requests=" : "Real QML complex agent: requests=")
              << network.requests.size() << ", bytes=" << total << ", peak=" << peak
              << ", peakTools=" << peak_tools << ", outputCallBytes=" << network.output_call_bytes
              << ", compactions=" << compactions << ", elapsedMs=" << clock.elapsed()
              << ", status=" << agent.status().toStdString() << '\n';
}
