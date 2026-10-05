#include "automation_bridge.hpp"
#include "office_ai_toolbox.hpp"
#include "word_bridge.hpp"
#include "word_document.hpp"
#include "word_editor_document.hpp"
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
    void transactions()
    {
        WordDocument source;
        source.paragraphs[0].runs = {{"target"}, {" keep"}};
        source.paragraphs[0].runs[1].italic = true;
        auto document = create_word_document(source, 240);
        for (const auto& action : {QString("underlineStyle"), QString("strikeStyle")})
        {
            for (const auto& style : {QString("single"), QString("double"), QString("none")})
            {
                check(format_word_document(*document, 0, 6, action, style), "public line action succeeds");
                check(inspect_word_document(*document, 1).value(action) == style &&
                        inspect_word_document(*document, 8).value(action) == "none" &&
                        inspect_word_document(*document, 8).value("italic").toBool(),
                    "selected line style preserves the unselected text and its style");
                const auto extracted = extract_word_document(*document);
                const auto saved = serialize_word(extracted.document);
                auto reopened = create_word_document(parse_word(saved.parts).document);
                check(extracted.success && saved.success &&
                        inspect_word_document(*reopened, 1).value(action) == style,
                    "line style survives editor extraction, OOXML and display reload");
            }
            check(!format_word_document(*document, 0, 6, action, true) &&
                    !format_word_document(*document, 0, 6, action, "wavy"),
                "invalid line types and variants reject without editing");
            document->undo();
            check(inspect_word_document(*document, 1).value(action) == "double", "undo restores double");
            document->redo();
            check(inspect_word_document(*document, 1).value(action) == "none", "redo clears double");
        }
        format_word_document(*document, 0, 6, "underlineStyle", "double");
        format_word_document(*document, 0, 6, "strikeStyle", "double");
        const auto sample = extract_word_document(*document).document.paragraphs[0];
        check(paint_word_format(*document, 7, 11, sample) &&
                inspect_word_document(*document, 8).value("underlineStyle") == "double" &&
                inspect_word_document(*document, 8).value("strikeStyle") == "double",
            "format brush carries both double lines");
        for (const auto& action : {QString("underline"), QString("strike")})
        {
            format_word_document(*document, 0, 6, action, true);
            check(inspect_word_document(*document, 1).value(action + "Style") == "single",
                "legacy true selects single, replacing double");
            format_word_document(*document, 0, 6, action, false);
            check(inspect_word_document(*document, 1).value(action + "Style") == "none",
                "legacy false clears all line variants");
        }
        format_word_document(*document, 0, 11, "clear", false);
        check(
            word_paragraph_decorations(*document).isEmpty(), "clear removes custom lines and cache entries");
    }
    void geometry()
    {
        for (const QString text : {QString("abcdefghijklmno pqrstuvwxyz"),
                 QStringLiteral("甲乙🦋丙丁戊己庚辛壬癸"), QStringLiteral("אבגדהוזחטיכלמנסעפצקרשת")})
        {
            WordDocument source;
            WordRun run{text.toStdString()};
            run.font = run.east_asia_font = "Arial";
            run.size = 24;
            run.underline = run.double_underline = run.strike = run.double_strike = true;
            source.paragraphs[0].runs = {run};
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            auto document = create_word_document(source, 140, false, option);
            const auto size = document->documentLayout()->documentSize();
            const auto* layout = document->begin().layout();
            const auto decorations = word_paragraph_decorations(*document);
            check(layout->lineCount() > 1 && decorations.size() == layout->lineCount() * 4,
                "wrapped Latin, RTL and emoji have exactly two underline and two strike strokes per line");
            QImage image(QSize(int(size.width() * 2 + 4), int(size.height() * 2 + 40)),
                QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.scale(2, 2);
            for (const auto& item : decorations)
                paint_word_decoration(painter, item.toMap());
            painter.end();
            for (const auto& item : decorations)
            {
                const auto line = item.toMap();
                const int x = qRound((line.value("x").toDouble() + line.value("width").toDouble() / 2) * 2);
                const int y = qRound(line.value("y").toDouble() * 2);
                bool ink = false;
                for (int dy = -1; dy <= 1; ++dy)
                    if (image.rect().contains(x, y + dy))
                        ink = ink || image.pixelColor(x, y + dy) != QColor(Qt::white);
                check(ink, "every custom stroke produces raster pixels at its shaped line position");
            }
            auto cloned = std::unique_ptr<QTextDocument>(document->clone());
            check(word_paragraph_decorations(*cloned).size() == decorations.size(),
                "export/preflight clones retain custom line decoration discovery");
        }
    }
    void tab_and_typing()
    {
        WordDocument source;
        source.paragraphs[0].runs = {{"a\tb"}};
        source.paragraphs[0].runs[0].font = "Arial";
        auto document = create_word_document(source, 300);
        format_word_document(*document, 0, 3, "underlineStyle", "double");
        document->documentLayout()->documentSize();
        const auto strokes = word_paragraph_decorations(*document);
        const auto line = document->begin().layout()->lineAt(0);
        check(
            strokes.size() == 2 && strokes.front().toMap().value("width").toReal() >= line.cursorToX(3) - 0.1,
            "selected tab spacing has one continuous pair of underline strokes");
        WordDocument empty;
        auto typing = create_word_document(empty);
        check(format_word_document(*typing, 0, 0, "underlineStyle", "double"),
            "empty paragraph receives double underline");
        QTextCursor cursor(typing.get());
        cursor.insertText("typed");
        check(inspect_word_document(*typing, 2).value("underlineStyle") == "double",
            "new text inherits the empty paragraph double-line format");
    }

    void mixed_direction()
    {
        WordDocument source;
        WordRun run{QStringLiteral("abc אבג XYZ").toStdString()};
        run.font = run.east_asia_font = "Arial";
        run.size = 24;
        source.paragraphs[0].runs = {run};
        auto document = create_word_document(source, 500);
        // The logical selection ends inside an RTL run: its visual regions are disjoint.
        check(format_word_document(*document, 0, 5, "underlineStyle", "double"), "mixed selection edits");
        document->documentLayout()->documentSize();
        auto decorations = word_paragraph_decorations(*document);
        const auto line = document->begin().layout()->lineAt(0);

        const qreal unselected =
            (line.cursorToX(5, QTextLine::Leading) + line.cursorToX(5, QTextLine::Trailing)) / 2;
        const auto bounds = document->documentLayout()->blockBoundingRect(document->begin());
        check(decorations.size() == 4, "mixed-direction selection retains two separate visual spans");
        for (const auto& item : decorations)
        {
            const auto stroke = item.toMap();
            const auto left = stroke.value("x").toReal() - bounds.x();
            check(unselected < left || unselected > left + stroke.value("width").toReal(),
                "double underline never crosses the unselected RTL glyph");
        }
        check(format_word_document(*document, 0, 5, "script", 1), "double-line text supports superscript");
        const auto raised = word_paragraph_decorations(*document);

        check(!raised.isEmpty() &&
                raised.front().toMap().value("y").toReal() + 3 <
                    decorations.front().toMap().value("y").toReal(),
            "double line follows the actual shaped superscript baseline");
        check(format_word_document(*document, 0, 5, "script", -1), "double-line text supports subscript");
        const auto lowered = word_paragraph_decorations(*document);
        check(!lowered.isEmpty() &&
                lowered.front().toMap().value("y").toReal() >
                    decorations.front().toMap().value("y").toReal() + 3,
            "double line follows the actual shaped subscript baseline");
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
            QUrl::fromLocalFile(source.filePath("tests/word-lines.qml")));
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
        cursor.insertText("target keep");
        page->setProperty("group", "start");
        page->setProperty("section", "emphasis");
        QCoreApplication::processEvents();
        for (const QString action : {QString("underlineStyle"), QString("strikeStyle")})
        {
            QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 6));
            auto* control = find_item(qobject_cast<QQuickItem*>(page.get()), "word" + action);
            check(control && QMetaObject::invokeMethod(control, "activated", Q_ARG(int, 2)),
                "actual child combo emits double through the parent selection handler");
            check(bridge.inspect(1).value(action) == "double" && bridge.inspect(8).value(action) == "none",
                "parent signal edits only selected characters through WordBridge");
        }
        check(bridge.copySelection(0, 6, false) && bridge.paste(7, 11) &&
                bridge.inspect(8).value("underlineStyle") == "double" &&
                bridge.inspect(8).value("strikeStyle") == "double",
            "internal rich clipboard keeps both double lines");
        bridge.undo();
        check(wrapper->textDocument()->toPlainText() == "target keep" &&
                bridge.inspect(8).value("underlineStyle") == "none",
            "rich paste undo restores text and line styles");
        check(bridge.copyFormat(1) && bridge.pasteFormat(7, 11) &&
                bridge.inspect(8).value("strikeStyle") == "double",
            "public format brush retains double variant");
        bridge.undo();
        AutomationBridge automation;
        automation.registerModule("word", &bridge);
        automation.setUiRoot(page.get());
        OfficeAiToolbox toolbox(output);
        toolbox.query("office_load_group", {{"group", "word"}});
        for (const QString action : {QString("underlineStyle"), QString("strikeStyle")})
        {
            const auto schema = toolbox.query("office_schema", {{"module", "word"}, {"name", action}});
            check(QJsonDocument(schema).toJson().contains("double"),
                "actual AI schema publishes double variant");
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
                        QJsonObject{{"start", 7}, {"end", 11}, {"action", action}, {"value", "single"}}}},
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
                    bridge.inspect(8).value(action) == "single" &&
                    bridge.inspect(1).value(action) == "double",
                "real local AI named action shares selection semantics without touching adjacent double "
                "lines");
        }
        QTemporaryDir saved_directory;
        const auto destination = saved_directory.filePath("word-double-lines.docx");
        check(bridge.saveTo(QUrl::fromLocalFile(destination)), "public save accepts a new local destination");
        check(wait_for(
                  [&]()
        {
            return !bridge.locked();
        }) && !bridge.modified(),
            "actual bridge saves line styles");
        const auto reopened = load_word_file(destination.toStdString());
        check(reopened.success && reopened.document.paragraphs[0].runs[0].double_underline &&
                reopened.document.paragraphs[0].runs[0].double_strike,
            "stored DOCX reopens with both double variants");
        QFile artifact(destination);
        QSaveFile evidence(QDir(output).filePath("word-double-lines.docx"));
        check(artifact.open(QIODevice::ReadOnly) && evidence.open(QIODevice::WriteOnly) &&
                evidence.write(artifact.readAll()) > 0 && evidence.commit(),
            "retain real bridge output for independent OOXML readback");
    }
}

void qml_register_types_Mirrorfly_Native();
int run_word_decoration_tests(int argc, char* argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    qml_register_types_Mirrorfly_Native();
    const QString output = qEnvironmentVariable("MIRRORFLY_WORD_LINE_OUTPUT",
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
    transactions();
    geometry();
    mixed_direction();
    tab_and_typing();
    frontend(engine, output);
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_decoration_tests(argc, argv);
}
