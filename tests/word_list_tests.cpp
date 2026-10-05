#include "automation_bridge.hpp"
#include "office_ai_toolbox.hpp"
#include "word_bridge.hpp"
#include "word_document.hpp"
#include "word_editor_document.hpp"
#include "word_list_fixture.hpp"
#include <QPdfWriter>
#include <QTextList>
#include <mirrorfly/automation.hpp>

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QThreadPool>
#include <iostream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
    bool wait_for(const std::function<bool()>& ready)
    {
        QElapsedTimer clock;
        clock.start();
        while (!ready() && clock.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return ready();
    }
    QQuickItem* find_item(QQuickItem* item, const QString& name)
    {
        if (item->objectName() == name)
            return item;
        for (auto* child : item->childItems())
            if (auto* result = find_item(child, name))
                return result;
        return nullptr;
    }
    QVariantMap list_state(const QTextDocument& document, int paragraph)
    {
        return inspect_word_document(document, document.findBlockByNumber(paragraph).position())
            .value("listStyle")
            .toMap();
    }
    void transactions(const QString& output)
    {
        const auto parsed = parse_word(word_list_test::fixture());
        auto document = create_word_document(parsed.document);
        check(save_word_file(QDir(output).filePath("word-list-original.docx").toStdString(), parsed.document)
                  .success,
            "retain imported source definitions for independent comparison");
        auto imported_edit = create_word_document(parsed.document);
        check(format_word_document(*imported_edit, 0, 1, "listStart", 8) &&
                save_word_file(QDir(output).filePath("word-list-imported.docx").toStdString(),
                    extract_word_document(*imported_edit).document)
                    .success,
            "retain independently edited imported list");
        check(list_state(*document, 0).value("label") == "(V)" &&
                list_state(*document, 2).value("label") == "(VI)" &&
                list_state(*document, 3).value("label") == "AA.",
            "native labels honor override starts, suffixes, alphabet rollover and continuity across plain "
            "paragraphs");
        const auto last = document->findBlockByNumber(3).position();
        check(format_word_document(*document, last, last, "listStart", 28) &&
                list_state(*document, 3).value("label") == "AB." &&
                list_state(*document, 0).value("label") == "(V)",
            "empty selection edits just the current numbered paragraph");
        document->undo();
        check(
            list_state(*document, 3).value("label") == "AA.", "undo restores original shared list metadata");
        document->redo();
        check(list_state(*document, 3).value("label") == "AB.", "redo restores independent list start");
        const auto before = document->toHtml();
        const int gap = document->findBlockByNumber(1).position();
        check(!format_word_document(*document, gap, gap, "listStart", 4) &&
                !format_word_document(*document, 0, last, "listStart", 4) &&
                !format_word_document(*document, 0, 1, "listStart", "4") &&
                !format_word_document(*document, 0, 1, "listStart", 2.5) &&
                !format_word_document(*document, 0, 1, "listStart", true) &&
                !format_word_document(*document, 0, 1, "listMarker", "chicago") &&
                document->toHtml() == before,
            "invalid types, unknown markers and mixed list ranges reject atomically");
        const QStringList markers{"decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman"};
        const QStringList labels{"8.", "h.", "H.", "viii.", "VIII."};
        for (int index = 0; index < markers.size(); ++index)
        {
            check(format_word_document(*document, 0, gap, "listMarker", markers[index]) &&
                    format_word_document(*document, 0, gap, "listStart", 8) &&
                    list_state(*document, 0).value("label") == labels[index] &&
                    inspect_word_document(*document, gap).value("list").toInt() == 0 &&
                    list_state(*document, 2).value("label") == "(V)",
                "end-exclusive selection creates independent standard markers; remaining original list "
                "renumbers");
            const auto extracted = extract_word_document(*document);
            const auto saved = serialize_word(extracted.document);
            if (!saved.success)
                std::cerr << saved.error << '\n';
            auto reopened = create_word_document(parse_word(saved.parts).document);
            check(saved.success && list_state(*reopened, 0).value("label") == labels[index] &&
                    list_state(*reopened, 2).value("label") == "(V)",
                "independent selected markers survive preserved document save and reload");
        }
        check(!format_word_document(*document, 0, gap, "listStart", 5000) &&
                !format_word_document(*document, 0, gap, "listStart", 0),
            "Roman editing rejects numbers outside the native renderer range");
        check(format_word_document(*document, 0, gap, "listMarker", "decimal") &&
                format_word_document(*document, 0, gap, "listStart", 0) &&
                list_state(*document, 0).value("label") == "0.",
            "decimal zero is an actual supported start");
        for (const QString marker : {QString("disc"), QString("circle"), QString("square")})
        {
            check(
                format_word_document(*document, 0, gap, "listMarker", marker), "bullet variant is editable");
            const auto result = extract_word_document(*document);
            check(result.document.paragraphs[0].list == WordListKind::Bullet &&
                    result.document.paragraphs[0].list_marker == marker.toStdString(),
                "bullet marker is public model data, not fake paragraph text");
        }
        auto levels = create_word_document(parsed.document);
        check(format_word_document(*levels, 0, 1, "listLevel", 1),
            "legacy level operation still accepts imported list");
        const auto saved = serialize_word(extract_word_document(*levels).document);
        check(saved.success && parse_word(saved.parts).document.paragraphs[0].list_level == 1 &&
                parse_word(saved.parts).document.paragraphs[0].list_instance == 11,
            "legacy list level retains original numbering instance through public extraction");
    }
    void frontend(QQmlEngine& engine, const QString& output)
    {
        const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QFile theme_file(source.filePath("config/theme.json"));
        check(theme_file.open(QIODevice::ReadOnly), "theme opens");
        const auto theme = QJsonDocument::fromJson(theme_file.readAll()).object().toVariantMap();
        WordBridge bridge;
        bridge.requestNew();
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import "../ui"
WordPage
{
    required property var backend
    revision: backend.revision
    readOnly: backend.readOnly
    busy: backend.locked
    function automationReady() { return true; }
    function automationState() { return {module: "word", pendingInput: false}; }
    onLoadRequested: function(document) { backend.loadEditor(document); }
    onInspectRequested: function(position) { selection = backend.inspect(position); }
    onFormatRequested: function(start, end, action, value) { backend.format(start, end, action, value); }
}
)qml",
            QUrl::fromLocalFile(source.filePath("tests/word-lists.qml")));
        std::unique_ptr<QObject> page(component.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&bridge)}, {"theme", theme}, {"width", 1040}, {"height", 720}}));
        check(page != nullptr, "real Word parent and child controls construct without a window");
        if (!page)
        {
            std::cerr << component.errorString().toStdString();
            return;
        }
        QCoreApplication::processEvents();
        auto* editor = page->findChild<QQuickItem*>("wordEditor");
        auto* wrapper = editor->property("textDocument").value<QQuickTextDocument*>();
        QTextCursor cursor(wrapper->textDocument());
        cursor.insertText("first\nsecond\nkeep");
        page->setProperty("group", "paragraph");
        page->setProperty("section", "list");
        QCoreApplication::processEvents();
        QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 12));
        auto* marker = find_item(qobject_cast<QQuickItem*>(page.get()), "wordListMarker");
        auto* start = find_item(qobject_cast<QQuickItem*>(page.get()), "wordListStart");
        check(marker && start && QMetaObject::invokeMethod(marker, "activated", Q_ARG(int, 7)),
            "actual list child emits Roman style through the parent selection handler");
        check(bridge.inspect(0).value("listStyle").toMap().value("marker") == "upperRoman" &&
                bridge.inspect(13).value("list").toInt() == 0,
            "parent applies marker to selected paragraphs only");
        start->setProperty("value", 5);
        check(QMetaObject::invokeMethod(start, "valueModified") &&
                bridge.inspect(0).value("listStyle").toMap().value("label") == "V." &&
                bridge.inspect(6).value("listStyle").toMap().value("label") == "VI.",
            "actual start control reaches WordBridge with the retained multi-paragraph selection");
        AutomationBridge automation;
        automation.registerModule("word", &bridge);
        automation.setUiRoot(page.get());
        OfficeAiToolbox toolbox(output);
        toolbox.query("office_load_group", {{"group", "word"}});
        for (const QString action : {QString("listMarker"), QString("listStart")})
        {
            const auto schema = toolbox.query("office_schema", {{"module", "word"}, {"name", action}});
            check(QJsonDocument(schema).toJson().contains("independent"),
                "actual AI schema explains independent selection scope");
            toolbox.beginResponse(
                QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot()))
                    .object()
                    .value("revision")
                    .toString(),
                1);
            bool finished = false;
            QJsonObject result;
            toolbox.execute("office_action",
                {{"op", "word.format"},
                    {"args",
                        QJsonObject{{"start", 0}, {"end", 12}, {"action", action},
                            {"value", action == "listMarker" ? QJsonValue("lowerLetter") : QJsonValue(8)}}}},
                [&](const QJsonObject& value)
            {
                result = value;
                finished = true;
            });
            check(wait_for(
                      [&]()
            {
                return finished;
            }) && result.value("ok").toBool(),
                "real local AI list action executes through public bridge");
        }
        check(bridge.inspect(0).value("listStyle").toMap().value("label") == "h." &&
                bridge.inspect(6).value("listStyle").toMap().value("label") == "i." &&
                bridge.inspect(13).value("list").toInt() == 0,
            "AI start and marker changes share native display and selection semantics");
        check(bridge.copySelection(0, 12, false) && bridge.paste(13, 17),
            "internal rich clipboard transfers common list markers and start");
        const auto pasted = extract_word_document(*wrapper->textDocument());
        const auto pasted_package = serialize_word(pasted.document);
        auto pasted_reload = create_word_document(parse_word(pasted_package.parts).document);
        check(pasted_package.success && list_state(*pasted_reload, 0).value("label") == "h." &&
                list_state(*pasted_reload, 3).value("label") == "h." &&
                list_state(*pasted_reload, 4).value("label") == "i.",
            "pasted independent list cannot collide with destination instance after reload");
        bridge.undo();
        check(wrapper->textDocument()->toPlainText() == "first\nsecond\nkeep",
            "rich list paste remains one undo transaction");
        QTemporaryDir directory;
        const auto destination = directory.filePath("word-list-styles.docx");
        check(bridge.saveTo(QUrl::fromLocalFile(destination)) &&
                wait_for(
                    [&]()
        {
            return !bridge.locked();
        }) && !bridge.modified(),
            "actual bridge saves selected list styles");
        QFile artifact(destination);
        QSaveFile evidence(QDir(output).filePath("word-list-styles.docx"));
        check(artifact.open(QIODevice::ReadOnly) && evidence.open(QIODevice::WriteOnly) &&
                evidence.write(artifact.readAll()) > 0 && evidence.commit(),
            "retain real bridge DOCX for independent numbering evaluation");
        QPdfWriter pdf(QDir(output).filePath("word-list-styles.pdf"));
        pdf.setResolution(96);
        QPainter painter(&pdf);
        wrapper->textDocument()->drawContents(&painter);
        painter.end();
        const auto reopened = load_word_file(destination.toStdString());
        check(reopened.success && reopened.document.paragraphs[0].list_marker == "lowerLetter" &&
                reopened.document.paragraphs[1].list_start == 8,
            "saved bridge output has actual numbering definitions");
    }
}

void qml_register_types_Mirrorfly_Native();
int run_word_list_tests(int argc, char* argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    qml_register_types_Mirrorfly_Native();
    const QString output = qEnvironmentVariable("MIRRORFLY_WORD_LIST_OUTPUT",
        QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY)).filePath("build"));
    QDir().mkpath(output);
    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& errors)
    {
        for (const auto& error : errors)
        {
            ++failures;
            std::cerr << error.toString().toStdString() << '\n';
        }
    });
    check(!QFontDatabase::families().isEmpty(), "geometry tests require an actual font backend");
    transactions(output);
    frontend(engine, output);
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_list_tests(argc, argv);
}
