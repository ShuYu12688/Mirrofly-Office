#include "office_ai_agent.hpp"
#include "office_ai_qml_scenario.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/mindmap_storage.hpp>
#include <mirrorfly/pdf_storage.hpp>
#include <mirrorfly/presentation_storage.hpp>
#include <mirrorfly/spreadsheet_storage.hpp>
#include <mirrorfly/text_storage.hpp>
#include <mirrorfly/word_storage.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QThread>

#include <iostream>

void run_office_ai_multiformat_scenario(mirrorfly::OfficeAiAgent& agent, office_ai_test::Transport& network,
    const QString& directory, const std::function<void(bool, const char*)>& check)
{
    using namespace mirrorfly;
    using namespace office_ai_test;
    const QStringList kinds{"writer", "markdown", "word", "sheets", "slides", "mindmap", "pdf"};
    const QStringList modules{"text", "text", "word", "sheets", "slides", "mindmap", "pdf"};
    const QStringList extensions{"txt", "md", "docx", "xlsx", "pptx", "mfg", "pdf"};
    const QString source = directory + "/existing.pdf";
    {
        QPdfWriter writer(source);
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.drawText(30, 50, "Existing source PDF");
    }
    QFile source_file(source);
    check(source_file.open(QIODevice::ReadOnly), "read original PDF bytes");
    const auto source_bytes = source_file.readAll();
    source_file.close();
    QStringList outputs;
    for (int index = 0; index < kinds.size(); ++index)
    {
        QFile original(directory + '/' + kinds[index] + '.' + extensions[index]);
        check(original.open(QIODevice::WriteOnly), "prepare destination collision");
        original.write("keep original");
        outputs.append(directory + '/' + kinds[index] + " (2)." + extensions[index]);
    }
    const auto step = [](const QString& module, const QString& action, const QJsonArray& args)
    {
        return QJsonObject{{"module", module}, {"action", action}, {"args", args}};
    };
    network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
    {
        const auto state = runtime();
        const auto workspace = office_ai_workspace(state);
        const auto revision = state.value("revision");
        const int document = turn / 7;
        const int phase = turn % 7;
        check(workspace.value("ok").toBool(), "cross-format task always has a valid QML workspace");
        if (document == kinds.size())
        {
            check(workspace.value("currentModule") == "home",
                "last document returns home after confirmed save");
            return {};
        }
        if (document > kinds.size())
        {
            check(false, "cross-format task must not enter a retry loop");
            return {};
        }
        const auto kind = kinds[document];
        const auto module = modules[document];
        if (phase == 0)
            return {call("office_load_group", {{"group", module}})};
        if (phase == 1)
            return {kind == "pdf" ? call("office_open", {{"path", source}, {"expectedRevision", revision}})
                                  : call("office_new", {{"kind", kind}, {"expectedRevision", revision}})};
        const QString current = workspace.value("currentModule").toString();
        check(current == module || (kind == "markdown" && current == "markdown"),
            "public navigation selects the requested native editor");
        if (phase == 2)
        {
            if (kind == "slides")
            {
                const auto dimensions = workspace.value("documents").toObject().value("slides").toObject();
                check(dimensions.value("slideWidth").toDouble() > 0 &&
                        dimensions.value("slideHeight").toDouble() > 0,
                    "workspace supplies dimensions promised by the slide guide");
                return {call("office_compose_slide",
                    {{"page", 0}, {"background", "#FFFFFF"},
                        {"elements",
                            QJsonArray{QJsonObject{{"type", "text"}, {"text", "Cross-format slides"},
                                {"x", 40}, {"y", 40}, {"width", 700}, {"height", 90},
                                {"style", QJsonObject{{"fontSize", 28}}}}}},
                        {"expectedRevision", revision}})};
            }
            if (module == "word")
                return {call("office_compose_word",
                    {{"title", "Cross-format Word"},
                        {"sections",
                            QJsonArray{QJsonObject{
                                {"heading", "Overview"}, {"paragraphs", QJsonArray{"Second paragraph"}}}}},
                        {"expectedRevision", revision}})};
            if (module == "sheets")
                return {call("office_compose_table",
                    {{"title", "Cross-format table"}, {"columns", QJsonArray{"Item", "Count"}},
                        {"rows", QJsonArray{QJsonArray{"Alpha", 3}, QJsonArray{"Beta", 4}}},
                        {"expectedRevision", revision}})};
            QJsonArray steps;
            if (module == "text")
                steps.append(step(module, "replaceContent",
                    {kind == "markdown" ? "# Markdown\nShared agent" : "Plain text shared agent"}));
            if (module == "mindmap")
            {
                steps.append(step(module, "execute", {"rename", QJsonObject{{"text", "Shared agent map"}}}));
                steps.append(step(module, "execute", {"addChild", QJsonObject{{"text", "Native module"}}}));
            }
            if (module == "pdf")
            {
                steps.append(step(module, "execute", {"rotate", QJsonObject{{"turns", 1}}}));
                steps.append(step(module, "execute",
                    {"note", QJsonObject{{"text", "Check source"}, {"x", 0.1}, {"y", 0.2}}}));
            }
            return {call("office_batch", {{"steps", steps}, {"expectedRevision", revision}})};
        }
        if (phase == 3)
        {
            const auto read = [&](const QJsonObject& query)
            {
                const QJsonObject request{{"module", module}, {"action", "readContent"},
                    {"args", QJsonArray{query}}, {"expectedRevision", runtime().value("revision")}};
                const auto reply = QJsonDocument::fromJson(
                    QByteArray::fromStdString(office_execute(json(request).toStdString())))
                                       .object();
                check(reply.value("ok").toBool(), "public bounded read invocation succeeds for every format");
                return reply.value("result").toObject();
            };
            const auto overview = read({{"view", "overview"}});
            check(overview.value("ok").toBool() && overview.value("views").isArray(),
                "every native format advertises its concrete content views");
            int offset = 0;
            int items = 0;
            const QString view = module == "text" || module == "pdf" ? "text" : "content";
            for (int attempt = 0; attempt < 100 && offset >= 0; ++attempt)
            {
                const auto part = read({{"view", view}, {"offset", offset}, {"limit", 2}});
                check(part.value("ok").toBool() && json(part).size() < 12000,
                    "paged content remains available and bounded instead of losing the entire document");
                items += part.value("items").toArray().size();
                const int next = part.value("nextOffset").toInt(-1);
                check(next == -1 || next > offset, "read cursor always advances or ends");
                offset = next;
            }
            check(offset == -1, "all content pages terminate");
            if (module == "sheets")
                check(items == 6, "sheet reads enumerate all stored cells independently of GUI selection");
            if (module == "word")
            {
                const auto paragraph = read({{"view", "text"}, {"index", 1}});
                check(paragraph.value("text") == "Overview" && paragraph.value("start").toInt() == 18,
                    "Word reads expose the composed heading position after one high-level call");
            }
            if (document == 0)
                return {call("office_load_group", {{"group", "word"}}),
                    call("office_new", {{"kind", "word"}, {"expectedRevision", revision}}, 1)};
            if (module == "pdf")
                return {call("office_save", {{"current", true}})};
            return {call("office_schema", {{"module", module}, {"name", ""}})};
        }
        if (phase == 4)
        {
            if (document == 0)
                check(json(body).contains("unsaved_changes") && current == "text",
                    "cross-document creation preserves unsaved content without opening a dialog");
            else
            {
                check(!json(body).contains("unsupported_schema_module"),
                    "all native modules support discovery");
                if (module == "pdf")
                    check(json(body).contains("current_save_unavailable"),
                        "imported PDF cannot silently save to a suggested destination");
            }
            return {call("office_save", {{"title", kind}, {"expectedRevision", revision}})};
        }
        if (!QFile::exists(outputs[document]))
            std::cerr << "Missing saved file: " << outputs[document].toStdString()
                      << "\nRuntime: " << json(state).toStdString() << "\nLast model input: "
                      << QJsonDocument(body.value("messages").toArray())
                             .toJson(QJsonDocument::Compact)
                             .right(7000)
                             .toStdString()
                      << '\n';
        check(QFile::exists(outputs[document]) && json(body).contains("file:"),
            "save completes and supplies evidence before switching modules");
        if (phase == 5)
            return {call("office_save", {{"current", true}})};
        check(!state.value("modules").toObject().value(module).toObject().value("modified").toBool(),
            "every native editor can save its already saved editable file in place");
        return {call("office_home", {{"expectedRevision", revision}})};
    };
    agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
    agent.start("Create native text, Markdown, Word, spreadsheet, PPT and mindmap documents, then rotate the "
                "existing PDF. Save each and continue to the next.");
    QElapsedTimer clock;
    clock.start();
    while (agent.busy() && clock.elapsed() < 45000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    if (agent.busy())
        agent.cancel();
    check(agent.answer() == "Finished" && network.requests.size() == 50,
        "single agent completes seven native document lifecycles through real QML");
    for (int index = 0; index < kinds.size(); ++index)
    {
        QFile original(directory + '/' + kinds[index] + '.' + extensions[index]);
        check(original.open(QIODevice::ReadOnly) && original.readAll() == "keep original",
            "every preexisting destination remains unchanged");
    }
    check(load_text_file(outputs[0].toStdString()).text == "Plain text shared agent",
        "TXT independently reads back");
    check(load_text_file(outputs[1].toStdString()).text == "# Markdown\nShared agent",
        "Markdown independently reads back");
    const auto word = load_word_file(outputs[2].toStdString());
    check(word.success && word.document.paragraphs.size() == 3 && word.document.paragraphs[0].heading == 1 &&
            word.document.paragraphs[1].heading == 2,
        "DOCX independently reads back the composed title, section and paragraph styles");
    const auto sheets = load_spreadsheet_file(outputs[3].toStdString());
    check(sheets.error == SpreadsheetError::None && sheets.document.sheets.size() == 1 &&
            sheets.document.sheets[0].name == "Cross-format table" &&
            spreadsheet_cell(sheets.document, 0, {1, 0}).value.text == "Alpha" &&
            spreadsheet_cell(sheets.document, 0, {2, 1}).value.text == "4",
        "XLSX independently reads back composed data rows and numeric cells");
    const auto slides = load_presentation_file(outputs[4].toStdString());
    check(slides.error == PresentationError::None && slides.scene.slides.size() == 1 &&
            find_presentation_text(slides.scene, "Cross-format slides").size() == 1,
        "PPTX independently reads back");
    const auto map = load_mindmap_file(outputs[5].toStdString());
    check(map.error == MindMapError::None && map.document.nodes.size() == 2,
        "native mindmap independently reads back");
    const auto pdf = load_pdf_file(outputs[6].toStdString());
    check(pdf.error == PdfError::None && pdf.document.pages.size() == 1 &&
            pdf.document.pages[0].rotation == 1 && pdf.document.pages[0].original_annotations.size() == 1 &&
            pdf.document.pages[0].original_annotations[0].contents == "Check source",
        "PDF copy independently reads back rotation and AI annotation");
    check(source_file.open(QIODevice::ReadOnly) && source_file.readAll() == source_bytes,
        "PDF input bytes are never overwritten");
    std::cout << "Cross-format agent: requests=" << network.requests.size()
              << ", elapsedMs=" << clock.elapsed() << ", status=" << agent.status().toStdString() << '\n';
}
