#include "automation_bridge.hpp"
#include "office_ai_toolbox.hpp"
#include "word_bridge.hpp"
#include "word_document.hpp"
#include "word_editor_document.hpp"
#include "word_tab_fixture.hpp"
#include "word_tabs.hpp"
#include "word_units.hpp"
#include <QPdfWriter>
#include <QTextLayout>
#include <QTextList>
#include <mirrorfly/automation.hpp>

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
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
    QVariantList stops(double position, const QString& alignment, const QString& leader)
    {
        return {QVariantMap{{"position", position}, {"alignment", alignment}, {"leader", leader}}};
    }
    void multiline_geometry(const QString& output)
    {
        for (const std::string alignment : {"left", "center", "right", "decimal"})
        {
            for (const std::string text : {"A\t12.34\nB\t56.78", "word word word B\t56.78"})
            {
                WordDocument source;
                source.paragraphs[0].runs = {{text}};
                source.paragraphs[0].runs[0].font = "Arial";
                source.paragraphs[0].left_indent = 18;
                source.paragraphs[0].tabs = {{72, alignment, "dot"}};
                auto document =
                    create_word_document(source, text.find('\n') != std::string::npos ? 400 : 140);
                document->documentLayout()->documentSize();
                const auto rendered = document->begin().text();
                const bool limited =
                    text.find('\n') != std::string::npos && (alignment == "center" || alignment == "right");
                const auto inspected = inspect_word_document(*document, 0);
                check(inspected.value("tabLayoutSupported").toBool() == !limited,
                    "soft breaks report the native center/right field-layout limitation");
                if (limited)
                {
                    check(inspected.value("tabLayoutReason").toString().contains("PDF") &&
                            inspected.value("tabLayoutReason").toString().contains("软换行"),
                        "the soft-break limitation explicitly covers preview and PDF layout");
                    check(format_word_document(*document, 0, 0, "tabStops", stops(90, "left", "dot")),
                        "limited tab combinations remain editable");
                    document->undo();
                    const auto restored = extract_word_document(*document).document;
                    check(inspect_word_document(*document, 0).value("tabStops").toList() ==
                            stops(72, QString::fromStdString(alignment), "dot"),
                        "undo preserves the original alignment and absolute tab positions");
                    const auto path = QDir(output).filePath(
                        QString("word-tabs-soft-break-%1.docx").arg(QString::fromStdString(alignment)));
                    check(save_word_file(path.toStdString(), restored).success,
                        "soft-break preview limitations preserve DOCX semantics");
                    const auto reloaded = load_word_file(path.toStdString());
                    check(reloaded.success && reloaded.document.paragraphs[0].runs[0].text == text &&
                            reloaded.document.paragraphs[0].tabs.size() == 1 &&
                            reloaded.document.paragraphs[0].tabs[0].position == 72 &&
                            reloaded.document.paragraphs[0].tabs[0].alignment == alignment,
                        "soft-break text and aligned tabs roundtrip through DOCX");
                }
                document->documentLayout()->documentSize();
                const auto* layout = document->begin().layout();
                check(
                    layout->lineCount() > 1, "tab layout covers explicit breaks and actual natural wrapping");
                int count = 0;
                for (int index = 0; index < layout->lineCount(); ++index)
                {
                    const auto line = layout->lineAt(index);
                    const int tab = rendered.indexOf('\t', line.textStart());
                    if (tab < 0 || tab >= line.textStart() + line.textLength())
                        continue;
                    ++count;
                    auto actual = line.cursorToX(tab + 1);
                    if (alignment == "right")
                        actual = line.cursorToX(tab + 6);
                    else if (alignment == "decimal")
                        actual = line.cursorToX(tab + 3);
                    else if (alignment == "center")
                        actual = (line.cursorToX(tab + 1) + line.cursorToX(tab + 6)) / 2;
                    if (!limited)
                        check(std::abs(actual - 96) <= 1,
                            "supported tab alignments preserve their absolute anchor after line wrapping");
                }
                check(count == (text.find('\n') != std::string::npos ? 2 : 1),
                    "every actual Tab on a multiline paragraph has an independently checked anchor");
            }
        }
        for (double first_indent : {12.0, -18.0})
        {
            WordDocument source;
            source.paragraphs[0].runs = {{"A\t12.34\nB\t56.78"}};
            source.paragraphs[0].left_indent = 18;
            source.paragraphs[0].tabs = {{72, "left", "dot"}};
            auto document = create_word_document(source, 400);
            check(format_word_document(*document, 0, 0, "firstLineIndent", first_indent),
                "first-line indentation remains editable without dropping tab metadata");
            const auto inspected = inspect_word_document(*document, 0);
            check(!inspected.value("tabLayoutSupported").toBool() &&
                    !inspected.value("tabLayoutReason").toString().isEmpty() &&
                    inspected.value("tabStops").toList() == stops(72, "left", "dot"),
                "first-line and hanging-indent combinations expose the known preview boundary accurately");
            document->undo();
            check(inspect_word_document(*document, 0).value("tabLayoutSupported").toBool(),
                "one undo restores supported ordinary multiline tab layout");
            document->redo();
            check(save_word_file(QDir(output)
                                     .filePath(QString("word-tabs-first-indent-%1.docx").arg(first_indent))
                                     .toStdString(),
                      extract_word_document(*document).document)
                      .success,
                "unsupported preview combinations still preserve text, indentation and tab positions in "
                "DOCX");
        }
    }
    void geometry(const QString& output)
    {
        const auto loaded = parse_word(word_tab_test::fixture());
        check(loaded.success, "inherited fixture parses");
        check(save_word_file(QDir(output).filePath("word-tabs-original.docx").toStdString(), loaded.document)
                  .success,
            "save source evidence");
        auto imported = create_word_document(loaded.document, 600);
        const int second = imported->findBlockByNumber(1).position();
        check(format_word_document(*imported, 0, second, "tabStops", stops(90, "right", "dot")),
            "explicit selected tab replacement succeeds");
        check(save_word_file(QDir(output).filePath("word-tabs-edited.docx").toStdString(),
                  extract_word_document(*imported).document)
                  .success,
            "save inherited tab edit");
        check(format_word_document(*imported, 0, second, "tabStops", QVariantList{}) &&
                inspect_word_document(*imported, 0).value("tabStops").toList().isEmpty(),
            "empty list clears custom stops");
        check(save_word_file(QDir(output).filePath("word-tabs-cleared.docx").toStdString(),
                  extract_word_document(*imported).document)
                  .success,
            "save inherited clear overrides");
        imported->undo();
        check(inspect_word_document(*imported, 0).value("tabStops").toList() == stops(90, "right", "dot"),
            "undo restores tab array and leaders");
        imported->redo();
        check(inspect_word_document(*imported, 0).value("tabStops").toList().isEmpty(), "redo clears tabs");
        const auto before = imported->toHtml();
        for (const QVariant invalid : {QVariant(true), QVariant(stops(0.01, "left", "none")),
                 QVariant(stops(90, "decimal", "fake")), QVariant(stops(90, "bar", "dot")),
                 QVariant(QVariantList{stops(90, "left", "none")[0], stops(90, "right", "none")[0]})})
            check(!format_word_document(*imported, 0, second, "tabStops", invalid) &&
                    imported->toHtml() == before,
                "invalid tab arrays reject without partial edits");
        for (const auto& alignment :
            {std::string("left"), std::string("center"), std::string("right"), std::string("decimal")})
            for (double indent : {0.0, 18.0})
            {
                WordDocument source;
                source.paragraphs[0].runs = {{"A\t12.34"}};
                source.paragraphs[0].runs[0].font = "Arial";
                source.paragraphs[0].left_indent = indent;
                source.paragraphs[0].tabs = {{72, alignment, "dot"}};
                auto document = create_word_document(source, 400);
                document->documentLayout()->documentSize();
                const auto line = document->begin().layout()->lineAt(0);
                auto actual = line.cursorToX(2);
                if (alignment == "right")
                    actual = line.cursorToX(7);
                if (alignment == "decimal")
                    actual = line.cursorToX(4);
                if (alignment == "center")
                    actual = (line.cursorToX(2) + line.cursorToX(7)) / 2;
                if (std::abs(actual - 96) > 1)
                    std::cerr << "tab geometry " << alignment << " indent=" << indent << " actual=" << actual
                              << '\n';
                check(std::abs(actual - 96) <= 1,
                    "tab alignment resolves to the explicit page-relative point position");
                const auto decorations = word_paragraph_decorations(*document);
                const auto gap = line.cursorToX(2) - line.cursorToX(1);
                const QFontMetricsF metrics(QFont("Arial", 12));
                check(gap < metrics.horizontalAdvance('.') + 2 || !decorations.isEmpty(),
                    "a gap fitting a full leader glyph and padding contains actual leader strokes");
                check(gap > 2 || decorations.isEmpty(),
                    "leaders suppress gaps occupied by aligned text and never overlap adjacent glyphs");
                if (!decorations.isEmpty())
                {
                    const auto values = decorations.front().toMap();
                    check(values.value("width").toDouble() <= gap + 0.1,
                        "leader drawing stays out of adjacent glyphs");
                }
            }
        WordDocument indented;
        indented.paragraphs.clear();
        for (double indent : {18.0, 30.0, 12.0})
        {
            WordParagraph paragraph;
            paragraph.runs = {{"A\t12.34"}};
            paragraph.runs[0].font = "Arial";
            paragraph.left_indent = indent;
            paragraph.tabs = {{72, "left", "none"}};
            indented.paragraphs.push_back(paragraph);
        }
        auto moved = create_word_document(indented, 400);
        const int unchanged = moved->findBlockByNumber(2).position();
        check(format_word_document(*moved, 0, unchanged, "leftIndent", 36),
            "changing left indent applies across selected paragraphs");
        moved->documentLayout()->documentSize();
        for (auto block = moved->begin(); block.isValid(); block = block.next())
        {
            check(std::abs(block.layout()->lineAt(0).cursorToX(2) - 96) <= 1 &&
                    inspect_word_tabs(block.blockFormat()) == stops(72, "left", "none") &&
                    word_pixels_to_points(block.blockFormat().leftMargin()) ==
                        (block.position() < unchanged ? 36 : 12),
                "indent edits preserve absolute stops and leave the unselected paragraph unchanged");
        }
        moved->undo();
        moved->documentLayout()->documentSize();
        check(word_pixels_to_points(moved->begin().blockFormat().leftMargin()) == 18 &&
                std::abs(moved->begin().layout()->lineAt(0).cursorToX(2) - 96) <= 1,
            "one undo restores both indent and native tab positioning");
        moved->redo();
        check(save_word_file(QDir(output).filePath("word-tabs-indent.docx").toStdString(),
                  extract_word_document(*moved).document)
                  .success,
            "save selected indent changes for independent absolute-stop verification");
        QTextDocument native;
        native.setDocumentMargin(0);
        native.setTextWidth(400);
        native.setDefaultFont(QFont("Arial", 12));
        QTextBlockFormat native_format;
        native_format.setLeftMargin(24);
        QTextOption::Tab native_stop;
        native_stop.position = 72;
        native_format.setTabPositions({native_stop});
        QTextCursor native_cursor(&native);
        native_cursor.setBlockFormat(native_format);
        native_cursor.insertText("A\t12.34");
        check(format_word_document(native, 0, 0, "leftIndent", 36),
            "third-party native tab formats accept the ordinary indent operation");
        native.documentLayout()->documentSize();
        check(std::abs(native.begin().layout()->lineAt(0).cursorToX(2) - 96) <= 1 &&
                inspect_word_tabs(native.begin().blockFormat()) == stops(72, "left", "none"),
            "native tabs recover their old absolute anchor before the new margin is merged");
        WordDocument source;
        source.paragraphs.clear();
        for (const std::string leader : {"dot", "hyphen", "underscore", "heavy", "middleDot"})
        {
            WordParagraph paragraph;
            paragraph.runs = {{"row\tend"}};
            paragraph.runs[0].font = "Arial";
            paragraph.tabs = {{108, "left", leader}};
            source.paragraphs.push_back(paragraph);
        }
        auto document = create_word_document(source, 400);
        const auto size = document->documentLayout()->documentSize();
        const auto decorations = word_paragraph_decorations(*document);
        check(decorations.size() == 5, "all five leader variants enter the shared viewport/PDF paint path");
        QImage image(QSize(410, int(size.height() + 30)), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter image_painter(&image);
        for (const auto& decoration : decorations)
            paint_word_decoration(image_painter, decoration.toMap());
        image_painter.end();
        int ink = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 30; x < 143; ++x)
                if (image.pixelColor(x, y) != QColor(Qt::white))
                    ++ink;
        check(ink > 100, "isolated leader raster contains actual strokes");
        QPdfWriter pdf(QDir(output).filePath("word-tab-leaders.pdf"));
        pdf.setResolution(96);
        QPainter painter(&pdf);
        document->drawContents(&painter);
        for (const auto& decoration : decorations)
            paint_word_decoration(painter, decoration.toMap());
        painter.end();
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
            QUrl::fromLocalFile(source.filePath("tests/word-tabs.qml")));
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
        cursor.insertText("first\t12.34\nkeep\tend");
        page->setProperty("group", "paragraph");
        page->setProperty("section", "tabs");
        QCoreApplication::processEvents();
        QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 11));
        auto* position = find_item(qobject_cast<QQuickItem*>(page.get()), "wordTabPosition");
        auto* leader = find_item(qobject_cast<QQuickItem*>(page.get()), "wordTabLeader");
        auto* set = find_item(qobject_cast<QQuickItem*>(page.get()), "wordSetTab");
        check(position && leader && set, "actual parent constructs tab child controls");
        if (!position || !leader || !set)
            return;
        position->setProperty("number", 90);
        leader->setProperty("currentIndex", 1);
        const bool clicked = QMetaObject::invokeMethod(set, "clicked");
        check(clicked && bridge.inspect(0).value("tabStops").toList() == stops(90, "left", "dot") &&
                bridge.inspect(13).value("tabStops").toList().isEmpty(),
            "actual child sets only the parent selection through the public bridge");
        AutomationBridge automation;
        automation.registerModule("word", &bridge);
        automation.setUiRoot(page.get());
        OfficeAiToolbox toolbox(output);
        toolbox.query("office_load_group", {{"group", "word"}});
        const auto schema = toolbox.query("office_schema", {{"module", "word"}, {"name", "tabStops"}});
        check(QJsonDocument(schema).toJson().contains("whole custom-stop array"),
            "AI schema states full replacement semantics");
        toolbox.beginResponse(QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot()))
                                  .object()
                                  .value("revision")
                                  .toString(),
            1);
        bool finished = false;
        QJsonObject result;
        toolbox.execute("office_action",
            {{"op", "word.format"},
                {"args",
                    QJsonObject{{"start", 0}, {"end", 11}, {"action", "tabStops"},
                        {"value",
                            QJsonArray{QJsonObject{
                                {"position", 108}, {"alignment", "decimal"}, {"leader", "underscore"}}}}}}},
            [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        const bool completed = wait_for([&]()
        {
            return finished;
        });
        check(completed && result.value("ok").toBool() &&
                bridge.inspect(0).value("tabStops").toList() == stops(108, "decimal", "underscore") &&
                wrapper->textDocument()->toPlainText() == "first\t12.34\nkeep\tend",
            "actual local AI edits stops without inserting fake tab text");
        QTemporaryDir directory;
        const auto destination = directory.filePath("word-tabs-ui.docx");
        check(bridge.saveTo(QUrl::fromLocalFile(destination)) &&
                wait_for(
                    [&]()
        {
            return !bridge.locked();
        }) && !bridge.modified(),
            "public bridge saves tab changes");
        QFile artifact(destination);
        QSaveFile evidence(QDir(output).filePath("word-tabs-ui.docx"));
        check(artifact.open(QIODevice::ReadOnly) && evidence.open(QIODevice::WriteOnly) &&
                evidence.write(artifact.readAll()) > 0 && evidence.commit(),
            "retain actual UI/AI file evidence");
        check(bridge.copySelection(0, 11, false) && bridge.paste(12, 20),
            "rich clipboard transfers tab metadata");
        check(bridge.inspect(13).value("tabStops").toList() == stops(108, "decimal", "underscore"),
            "rich clipboard preserves the explicit tab array");
        check(save_word_file(QDir(output).filePath("word-tabs-pasted.docx").toStdString(),
                  extract_word_document(*wrapper->textDocument()).document)
                  .success,
            "retain rich clipboard result for independent DOCX verification");
        bridge.undo();
        check(wrapper->textDocument()->toPlainText() == "first\t12.34\nkeep\tend" &&
                bridge.inspect(13).value("tabStops").toList().isEmpty(),
            "rich paragraph metadata paste is a single undo transaction");
        check(bridge.format(0, 11, "firstLineIndent", 12),
            "the public bridge accepts preserved tab and first-line metadata together");
        QMetaObject::invokeMethod(page.get(), "refreshSelection");
        QCoreApplication::processEvents();
        auto* warning = find_item(qobject_cast<QQuickItem*>(page.get()), "wordTabPreviewWarning");
        check(warning && warning->property("visible").toBool() &&
                !warning->property("text").toString().isEmpty(),
            "actual tab child presents the parent's explicit first-line preview limitation");
        bridge.undo();
    }
}

void qml_register_types_Mirrorfly_Native();
int run_word_tab_tests(int argc, char* argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    qml_register_types_Mirrorfly_Native();
    const QString output = qEnvironmentVariable("MIRRORFLY_WORD_TAB_OUTPUT",
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
    geometry(output);
    multiline_geometry(output);
    frontend(engine, output);
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_tab_tests(argc, argv);
}
