#include "word_bridge.hpp"
#include "word_document.hpp"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextLayout>
#include <QTextTable>
#include <QThreadPool>

#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

namespace
{
    int failures = 0;
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << message << '\n';
        }
    }
    void wait(mirrorfly::WordBridge& bridge)
    {
        QElapsedTimer timer;
        timer.start();
        while (bridge.locked() && timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        for (int pass = 0; pass < 5; ++pass)
        {
            QCoreApplication::processEvents();
        }
        check(!bridge.locked(), "asynchronous Word operation completes");
    }

    QQuickItem* find_item(QQuickItem* item, const QString& name)
    {
        if (!item || item->objectName() == name)
            return item;
        for (auto* child : item->childItems())
            if (auto* found = find_item(child, name))
                return found;
        return nullptr;
    }

    void loading_cases(QQmlEngine& engine)
    {
        using namespace mirrorfly;
        const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        const auto path = source.filePath("tests/fixtures/word-python-docx.docx");
        std::set<WordLoadStage> stages;
        const auto loaded = load_word_file(path.toStdString(),
            [&](WordLoadStage stage, std::size_t completed, std::size_t total)
        {
            check(!total || completed <= total, "storage progress stays within its actual work count");
            stages.insert(stage);
        });
        check(loaded.success && stages.size() == 4, "Word storage reports reading through parsing");
        QTemporaryDir saved_directory;
        const auto saved_path = saved_directory.filePath("progress.docx");
        std::size_t previous_part = 0;
        bool committed = false;
        check(save_word_file(saved_path.toStdString(), loaded.document, {},
                  [&](OfficeSaveStage stage, std::size_t completed, std::size_t total)
        {
            check(completed <= total, "save progress stays within actual work counts");
            if (stage == OfficeSaveStage::Writing)
            {
                check(completed >= previous_part, "package entry progress never moves backward");
                previous_part = completed;
            }
            if (stage == OfficeSaveStage::Committing && completed == total)
                committed = QFile::exists(saved_path);
        }).success &&
                committed && previous_part > 0,
            "saving reports complete only after the target exists");
        check(save_word_file(saved_directory.filePath("observer.docx").toStdString(), loaded.document, {},
                  [](auto, auto, auto)
        {
            throw std::runtime_error("observer");
        }).success,
            "save observer exceptions do not break atomic output");
        check(load_word_file(path.toStdString(),
                  [](auto, auto, auto)
        {
            throw std::runtime_error("observer failure");
        }).success,
            "progress observer failures do not change document validity");
        WordBridge bridge;
        bool started = false, ready = false;
        qreal previous = 0;
        QObject::connect(&bridge, &WordBridge::loadStarted, &bridge, [&]()
        {
            started = bridge.locked() && bridge.loadingProgress() == 0 && !bridge.loadingStage().isEmpty();
        });
        QObject::connect(&bridge, &WordBridge::loadingChanged, &bridge, [&]()
        {
            check(bridge.loadingProgress() >= previous && bridge.loadingProgress() <= 1,
                "bridge progress is monotonic and bounded within one load");
            previous = bridge.loadingProgress();
        });
        QObject::connect(&bridge, &WordBridge::loadReady, &bridge, [&]()
        {
            ready = true;
        });
        check(bridge.requestOpen(QUrl::fromLocalFile(path)) && started && !ready,
            "loading starts synchronously before the worker and before document activation");
        wait(bridge);
        check(!ready && bridge.loadingProgress() < 1 && bridge.active(),
            "background preparation alone does not reveal an unattached editor");
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 720; textFormat: TextEdit.RichText }", QUrl{});
        std::unique_ptr<QObject> item(component.create());
        check(item != nullptr, "loading completion test editor constructs");
        if (item)
            bridge.loadEditor(item->property("textDocument").value<QQuickTextDocument*>());
        check(ready && bridge.loadingProgress() == 1 && bridge.statistics().value("paragraphs").toInt() > 1,
            "loading reaches completion only after the prepared document is attached");
        ready = false;
        previous = 0;
        check(bridge.requestOpen(QUrl::fromLocalFile(source.filePath("missing-word.docx"))),
            "a failed open still enters the real loading pipeline");
        wait(bridge);
        check(!ready && !bridge.message().isEmpty() && bridge.active() &&
                bridge.documentName() == QStringLiteral("word-python-docx.docx"),
            "failed loading preserves the previous document and never reports ready");
    }

    void layout_handoff_case(QQmlEngine& engine)
    {
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 960; height: 720; "
                          "textFormat: TextEdit.RichText; wrapMode: TextEdit.Wrap }",
            QUrl{});
        std::unique_ptr<QObject> item(component.create());
        check(item != nullptr, "layout handoff editor constructs");
        if (!item)
            return;
        auto* wrapper = item->property("textDocument").value<QQuickTextDocument*>();
        mirrorfly::WordDocument source;
        source.paragraphs.resize(200);
        for (auto& paragraph : source.paragraphs)
            paragraph.runs = {{"Prepared text with the destination editor's wrapping and font metrics."}};
        const auto option = wrapper->textDocument()->defaultTextOption();
        auto prepared =
            mirrorfly::create_word_document(source, wrapper->textDocument()->textWidth(), true, option);
        const auto prepared_option = prepared->defaultTextOption();
        check(prepared_option.wrapMode() == option.wrapMode() &&
                prepared_option.useDesignMetrics() == option.useDesignMetrics() &&
                prepared_option.textDirection() == option.textDirection() &&
                prepared_option.alignment() == option.alignment(),
            "background preparation uses the destination editor's layout options from the outset");
        const auto size = prepared->documentLayout()->documentSize();
        int layouts = 0;
        QObject::connect(prepared->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            item.get(), [&](const QSizeF& value)
        {
            // Qt may deliver a queued initial size notification even when no size has changed.
            if (value != size)
                ++layouts;
        });
        prepared->setParent(wrapper);
        wrapper->setTextDocument(prepared.release());
        QCoreApplication::processEvents();
        const auto actual = wrapper->textDocument()->defaultTextOption();
        check(layouts == 0 && wrapper->textDocument()->documentLayout()->documentSize() == size &&
                actual.wrapMode() == option.wrapMode() &&
                actual.useDesignMetrics() == option.useDesignMetrics() &&
                actual.textDirection() == option.textDirection() && actual.alignment() == option.alignment(),
            "attaching a prepared document retains matching Quick text options and document geometry");
    }
    void cases(QQmlEngine& engine)
    {
        using namespace mirrorfly;
        const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        const auto styled =
            load_word_file(source.filePath("tests/fixtures/word-table-styles.docx").toStdString());
        check(styled.success && styled.document.tables.size() == 1, "independent table style fixture loads");
        if (styled.success && styled.document.tables.size() == 1)
        {
            auto rendered = create_word_document(styled.document, 700);
            const auto header = rendered->find(QStringLiteral("表头甲"));
            const auto band = rendered->find(QStringLiteral("数据甲1"));
            const auto direct = rendered->find(QStringLiteral("数据乙1"));
            const auto format = inspect_word_document(*rendered, header.selectionStart());
            check(format.value("size").toInt() == 18 && format.value("bold").toBool() &&
                    format.value("color").toString() == "#ffffff" &&
                    format.value("cellFill").toString() == "#4f81bd" &&
                    inspect_word_document(*rendered, band.selectionStart()).value("cellFill").toString() ==
                        "#95b3d7" &&
                    inspect_word_document(*rendered, direct.selectionStart()).value("cellFill").toString() ==
                        "#fff2cc",
                "GUI inspection sees table font, conditional theme fill and direct cell override");
            QImage pixels(900, 1200, QImage::Format_ARGB32_Premultiplied);
            pixels.fill(Qt::white);
            QPainter painter(&pixels);
            rendered->drawContents(&painter);
            painter.end();
            int blue = 0;
            for (int y = 0; y < pixels.height(); ++y)
                for (int x = 0; x < pixels.width(); ++x)
                    if (pixels.pixelColor(x, y) == QColor("#4f81bd"))
                        ++blue;
            check(blue > 1000, "offscreen document paint includes inherited table header shading");
            check(format_word_document(*rendered, header.selectionStart(), header.selectionEnd(), "size", 22),
                "shared editor formatting overrides inherited size");
            const auto extracted = extract_word_document(*rendered);
            const auto saved = serialize_word(extracted.document);
            const auto reopened = parse_word(saved.parts);
            check(extracted.success && saved.success && reopened.success &&
                    reopened.document.paragraphs[1].runs[0].size == 22 &&
                    reopened.document.paragraphs[1].runs[0].bold &&
                    reopened.document.tables[0].cells[3].background == "#95B3D7" &&
                    reopened.document.tables[0].cells[4].background == "#FFF2CC",
                "Qt edit/extract/save/reopen retains conditional and direct table style layers");
        }
        const auto independent =
            load_word_file(source.filePath("tests/fixtures/word-python-docx.docx").toStdString());
        check(independent.success && independent.document.paragraphs.size() == 7,
            "independently generated DOCX loads with table paragraphs flattened in order");
        if (independent.success && independent.document.paragraphs.size() == 7)
        {
            const auto& run = independent.document.paragraphs[1].runs[0];
            check(run.text == "中英混排 — Office 2026" && run.font == "Arial" &&
                    run.east_asia_font == "宋体" && run.size == 16 && run.bold && run.italic && run.underline,
                "independent run properties and Unicode are read");
            check(independent.document.paragraphs[4].runs[0].text == "验证" &&
                    independent.document.paragraphs[6].runs[0].text == "尾段",
                "preview keeps body and table reading order without importing header text");
        }
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
    statistics: backend.statistics
    onLoadRequested: function(document) { backend.loadEditor(document); }
    onInspectRequested: function(position) { selection = backend.inspect(position); }
    onFormatRequested: function(start, end, action, value) { backend.format(start, end, action, value); }
    onParagraphRequested: function(start, end) { finishParagraph(backend.insertParagraph(start, end)); }
    onTemplateRequested: function(position, kind) { backend.insertTemplate(position, kind); }
    Connections
    {
        target: backend
        function onEditorChanged() { refreshSelection(); }
    }
}
)qml",
            QUrl::fromLocalFile(source.filePath("tests/word-contract.qml")));
        std::unique_ptr<QObject> page(component.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&bridge)}, {"theme", theme}, {"width", 1040}, {"height", 720}}));
        check(page != nullptr, "Word page constructs without a window");
        if (!page)
        {
            std::cerr << component.errorString().toStdString();
            return;
        }
        wait(bridge);
        auto* editor_item = page->findChild<QQuickItem*>("wordEditor");
        check(editor_item && editor_item->width() > 600, "document retains a readable writing area");
        if (!editor_item)
        {
            return;
        }
        check(!editor_item->flags().testFlag(QQuickItem::ItemObservesViewport),
            "Word retains text nodes so backward scrolling cannot omit earlier content");
        check(!editor_item->flags().testFlag(QQuickItem::ItemHasContents),
            "the input editor cannot rebuild a full-document text scene graph behind the viewport");
        const auto wrapper = editor_item->property("textDocument").value<QQuickTextDocument*>();
        check(wrapper && wrapper->textDocument(), "Word editor uses a public Qt document");
        if (!wrapper)
        {
            return;
        }
        QTextCursor cursor(wrapper->textDocument());
        cursor.insertText(QStringLiteral("项目计划\n第一阶段：完成验证。\n第二阶段：交付。"));
        check(bridge.modified(), "typing marks the document modified");
        check(bridge.format(0, 4, "heading", 1), "heading action succeeds");
        check(bridge.format(0, 4, "font", QStringLiteral("Arial")), "installed font action succeeds");
        check(bridge.format(0, 4, "size", 24), "font size action succeeds");
        check(bridge.inspect(2).value("size").toInt() == 24 && bridge.inspect(2).value("bold").toBool(),
            "formatting reaches the live document");
        const auto format_revision = bridge.revision();
        const auto format_read = bridge.readContent({{"view", "format"}, {"index", 0}, {"offset", 2}});
        check(format_read.value("ok").toBool() && format_read.value("position").toInt() == 2 &&
                format_read.value("format").toMap() == bridge.inspect(2) &&
                format_read.value("textComplete").toBool() && format_read.value("start").toInt() == 0 &&
                format_read.value("end").toInt() == wrapper->textDocument()->begin().text().size() &&
                format_read.value("textPreview").toString() == wrapper->textDocument()->begin().text() &&
                bridge.revision() == format_revision,
            "Word public format read shares live inspection without editing the document");
        check(!bridge.readContent({{"view", "format"}, {"index", 99}}).value("ok").toBool() &&
                !bridge.readContent({{"view", "format"}, {"index", 0}, {"offset", 5}}).value("ok").toBool(),
            "Word format reads reject invalid paragraphs and offsets");
        for (const qreal zoom : {0.75, 1.0, 1.25, 1.5, 2.0})
        {
            page->setProperty("zoom", zoom);
            QCoreApplication::processEvents();
            for (const int position : {1, 6, 11, 20})
            {
                const auto block = wrapper->textDocument()->findBlock(position);
                const auto bounds = wrapper->textDocument()->documentLayout()->blockBoundingRect(block);
                const auto line = block.layout()->lineForTextPosition(position - block.position());
                const QPointF document_point(bounds.x() + line.cursorToX(position - block.position()),
                    bounds.y() + line.y() + line.height() / 2);
                int actual = -1;
                check(QMetaObject::invokeMethod(editor_item, "positionAt", Q_RETURN_ARG(int, actual),
                          Q_ARG(qreal, document_point.x()), Q_ARG(qreal, document_point.y())) &&
                        actual == position,
                    "Word input hit testing uses the same glyph positions as the public document layout");
                const auto scene_point = editor_item->mapToScene(document_point);
                check(QLineF(editor_item->mapFromScene(scene_point), document_point).length() < 0.001,
                    "zoomed Word pointer coordinates round-trip without a second scale transform");
            }
        }
        page->setProperty("zoom", 1.0);
        page->setProperty("selection", QVariantMap{{"size", 10.5}, {"characterSpacing", 1.5}});
        QCoreApplication::processEvents();
        const auto* size_tool = page->findChild<QObject*>("wordFontSize");
        const auto* spacing_tool = page->findChild<QObject*>("wordCharacterSpacing");
        check(size_tool && size_tool->property("value").toInt() == 105 && spacing_tool &&
                spacing_tool->property("value").toInt() == 15,
            "font and character spacing tools retain fractional point values");
        bridge.undo();
        check(bridge.inspect(2).value("size").toInt() == 22, "one format action is one undo step");
        bridge.redo();
        check(bridge.format(0, 4, "list", 2) && bridge.format(0, 4, "listLevel", 1) &&
                bridge.format(0, 4, "leftIndent", 36) && bridge.format(0, 4, "firstLineIndent", -18) &&
                bridge.format(0, 4, "spaceBefore", 9) && bridge.format(0, 4, "spaceAfter", 12),
            "list, indentation and paragraph spacing actions succeed through the public bridge");
        check(bridge.format(0, 4, "strike", true) && bridge.format(0, 4, "script", 1) &&
                bridge.format(0, 4, "color", "#123456") && bridge.format(0, 4, "highlight", "#FFEEDD") &&
                bridge.format(0, 4, "paragraphFill", "#CCDDEE") &&
                bridge.format(0, 4, "paragraphBorder", "#445566") &&
                bridge.format(0, 4, "characterBorder", "#667788") &&
                bridge.format(0, 4, "characterSpacing", 1.5),
            "extended text and paragraph actions reach editor");
        check(bridge.inspect(2).value("strike").toBool() && bridge.inspect(2).value("script").toInt() == 1 &&
                bridge.paragraphDecorations().size() == 2,
            "new format inspection and decoration geometry available");
        const auto paragraph_style = bridge.inspect(2);
        check(paragraph_style.value("list").toInt() == 2 && paragraph_style.value("listLevel").toInt() == 1 &&
                paragraph_style.value("leftIndent").toInt() == 36 &&
                paragraph_style.value("firstLineIndent").toInt() == -18 &&
                paragraph_style.value("spaceBefore").toInt() == 9 &&
                paragraph_style.value("spaceAfter").toInt() == 12,
            "inspection exposes the live paragraph structure without private document access");
        const auto match = bridge.find(QStringLiteral("验证"), 0, false);
        check(match.contains("start"), "search locates Unicode text");
        const bool replaced = bridge.replace(match.value("start").toInt(), match.value("end").toInt(),
            QStringLiteral("验证"), QStringLiteral("评审"));
        check(replaced, "replace changes only an expected selection");
        check(!bridge.replace(0, 4, QStringLiteral("wrong"), QStringLiteral("lost")),
            "stale replacement selection is rejected");

        QTemporaryDir temporary;
        const auto path = temporary.filePath("draft.docx");
        check(QMetaObject::invokeMethod(editor_item, "select", Q_ARG(int, 4), Q_ARG(int, 1)),
            "select backward before saving");
        bridge.save();
        check(editor_item->property("cursorPosition").toInt() == 1 &&
                editor_item->property("selectionStart").toInt() == 1 &&
                editor_item->property("selectionEnd").toInt() == 4 &&
                editor_item->property("readOnly").toBool(),
            "opening the save dialog protects edits without moving the cursor or losing selection");
        bridge.selectSaveFile(QUrl::fromLocalFile(path));
        wait(bridge);
        check(editor_item->property("cursorPosition").toInt() == 1 &&
                editor_item->property("selectionStart").toInt() == 1 &&
                editor_item->property("selectionEnd").toInt() == 4 &&
                !editor_item->property("readOnly").toBool(),
            "saving restores editing access and preserves reversed selection direction");
        check(QFile::exists(path) && !bridge.modified() && !bridge.readOnly(),
            "new Word document saves and becomes clean");
        const auto loaded = load_word_file(path.toStdString());
        check(loaded.success && loaded.document.paragraphs[0].runs[0].size == 24 &&
                loaded.document.paragraphs[0].list == WordListKind::Numbered &&
                loaded.document.paragraphs[0].list_level == 1 &&
                loaded.document.paragraphs[0].left_indent == 36 &&
                loaded.document.paragraphs[0].first_line_indent == -18 &&
                loaded.document.paragraphs[0].space_before == 9 &&
                loaded.document.paragraphs[0].space_after == 12,
            "saved DOCX contains real list and paragraph formatting, not just text");
        const QDir output(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY));
        check(save_word_file(output.filePath("word-interop.docx").toStdString(), loaded.document).success,
            "interoperability fixture is written for independent inspection");

        cursor = QTextCursor(wrapper->textDocument());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral(" 未保存"));
        const auto snapshot = bridge.snapshot();
        check(snapshot.value("plainText").toString().contains(QStringLiteral("未保存")) &&
                !snapshot.value("truncated").toBool() && snapshot.value("active").toBool() &&
                snapshot.value("modified").toBool() && !snapshot.value("readOnly").toBool() &&
                snapshot.value("paragraphs").toInt() == bridge.statistics().value("paragraphs").toInt() &&
                snapshot.value("revision").toInt() == bridge.revision(),
            "public snapshot returns a bounded body and bridge state");
        bridge.requestHome();
        check(bridge.locked() && bridge.active(), "home waits for unsaved confirmation");
        bridge.resolveUnsaved("cancel");
        check(bridge.active() && bridge.modified() && !bridge.locked(), "cancel preserves the draft");
        // Failure after a discard decision must still preserve the old document.
        bridge.requestOpen(QUrl::fromLocalFile(temporary.filePath("missing.docx")));
        bridge.resolveUnsaved("discard");
        wait(bridge);
        check(
            bridge.active() && bridge.modified() && wrapper->textDocument()->toPlainText().contains("未保存"),
            "failed replacement preserves old content and dirty state");
        QFile external(path);
        check(external.open(QIODevice::Append), "external mutation fixture opens");
        external.write("changed");
        external.close();
        bridge.save();
        wait(bridge);
        check(bridge.modified() && bridge.message().contains("其他程序"),
            "external disk conflict protects both versions");
        bridge.clearMessage();
        check(bridge.message().isEmpty() && bridge.modified(),
            "dismissing a Word failure clears the notice without discarding the draft");
        const auto import_path = temporary.filePath("import.docx");
        check(save_word_file(import_path.toStdString(), loaded.document).success, "import fixture saves");
        {
            WordBridge abandoned;
            check(abandoned.requestOpen(QUrl::fromLocalFile(import_path)),
                "background load starts before bridge disposal");
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        bridge.requestOpen(QUrl::fromLocalFile(import_path));
        bridge.resolveUnsaved("discard");
        wait(bridge);
        check(bridge.readOnly() && !bridge.modified(), "all imported DOCX files initially open read-only");
        check(!bridge.createEditableCopyTo(QUrl::fromLocalFile(import_path)) && bridge.readOnly(),
            "direct Word copy refuses to overwrite its source");
        check(!bridge.format(0, 1, "bold", false), "public edit interface enforces read-only state");
        bridge.requestEditableCopy();
        bridge.selectSaveFile(QUrl::fromLocalFile(import_path));
        check(bridge.readOnly() && !bridge.locked(), "editable copy cannot overwrite the source");
        bool copy_failed = false;
        const auto failure_connection =
            QObject::connect(&bridge, &WordBridge::copyCompleted, &bridge, [&](bool success)
        {
            copy_failed = !success;
        });
        bridge.requestEditableCopy();
        bridge.selectSaveFile(QUrl::fromLocalFile(temporary.filePath("missing/copy.docx")));
        wait(bridge);
        check(copy_failed && bridge.readOnly() && bridge.loadingProgress() < 1 && !bridge.message().isEmpty(),
            "failed Word copy ends loading without exposing editing or reporting 100 percent");
        QObject::disconnect(failure_connection);
        const auto copy = temporary.filePath("copy.docx");
        editor_item->setProperty("cursorPosition", 2);
        int copy_started = 0, copy_finished = 0;
        qreal copy_progress = 0;
        const auto started_connection = QObject::connect(&bridge, &WordBridge::loadStarted, &bridge, [&]()
        {
            ++copy_started;
            check(bridge.locked() && bridge.readOnly() && bridge.loadingProgress() == 0,
                "copy animation starts before work and leaves the source read-only");
        });
        const auto progress_connection = QObject::connect(&bridge, &WordBridge::loadingChanged, &bridge, [&]()
        {
            check(bridge.loadingProgress() >= copy_progress, "copy progress is monotonic");
            copy_progress = bridge.loadingProgress();
        });
        const auto finished_connection =
            QObject::connect(&bridge, &WordBridge::copyCompleted, &bridge, [&](bool success)
        {
            ++copy_finished;
            check(success && QFile::exists(copy) && !bridge.readOnly() && !bridge.locked(),
                "copy completion is delivered only after atomic save and editor activation");
        });
        check(bridge.createEditableCopyTo(QUrl::fromLocalFile(copy)),
            "direct Word copy starts without opening a save dialog");
        check(copy_started == 1 && copy_finished == 0, "copy creation does not block on the file worker");
        wait(bridge);
        check(copy_finished == 1 && copy_progress == 1, "copy loading completes at 100 percent exactly once");
        QObject::disconnect(started_connection);
        QObject::disconnect(progress_connection);
        QObject::disconnect(finished_connection);
        check(!bridge.readOnly() && QFile::exists(copy) && QFile::exists(import_path),
            "copy becomes editable only after save succeeds");
        check(editor_item->property("cursorPosition").toInt() == 2,
            "creating an editable copy retains the reader's current position");
        bridge.saveAs();
        bridge.selectSaveFile(QUrl::fromLocalFile(import_path));
        check(bridge.message().contains("源文件"), "source path protection remains after conversion");
        bridge.requestHome();
        check(!bridge.active(), "clean Word document returns home");
        for (int pass = 0; pass < 5; ++pass)
        {
            QCoreApplication::processEvents();
        }
        bridge.requestNew();
        bridge.resolveUnsaved("discard");
        wait(bridge);
        check(bridge.insertText(0, 0, QStringLiteral("可撤销正文")),
            "public text insertion accepts a bounded Unicode edit");
        check(wrapper->textDocument()->toPlainText() == QStringLiteral("可撤销正文"),
            "public text insertion updates the editor");
        bridge.undo();
        check(wrapper->textDocument()->toPlainText().isEmpty(), "public text insertion is one undoable edit");
        check(!bridge.insertText(0, 0, QString(maximum_word_text_bytes + 1, QChar('x'))) &&
                wrapper->textDocument()->toPlainText().isEmpty(),
            "public text insertion rejects an oversized edit without partial changes");
        cursor = QTextCursor(wrapper->textDocument());
        cursor.insertText(QStringLiteral("Heading"));
        editor_item->setProperty("cursorPosition", 7);
        const bool heading_applied = QMetaObject::invokeMethod(
            page.get(), "applyFormat", Q_ARG(QVariant, "heading"), Q_ARG(QVariant, 1));
        check(heading_applied, "page applies heading through its public signal");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(editor_item, &enter);
        QKeyEvent typed(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("body"));
        QCoreApplication::sendEvent(editor_item, &typed);
        const auto typed_document = extract_word_document(*wrapper->textDocument());
        check(typed_document.success && typed_document.document.paragraphs.size() == 2 &&
                typed_document.document.paragraphs[1].heading == 0 &&
                !typed_document.document.paragraphs[1].runs.empty() &&
                typed_document.document.paragraphs[1].runs[0].text == "body" &&
                typed_document.document.paragraphs[1].runs[0].size == 12 &&
                !typed_document.document.paragraphs[1].runs[0].bold,
            "typing after a heading Enter uses body formatting in the actual Qt Quick control");

        bridge.requestNew();
        bridge.resolveUnsaved("discard");
        wait(bridge);
        check(bridge.insertTemplate(0, "meeting"), "meeting template inserts through the public bridge");
        auto template_document = extract_word_document(*wrapper->textDocument());
        check(template_document.success && template_document.document.paragraphs.size() == 10 &&
                template_document.document.paragraphs[5].list == WordListKind::Bullet &&
                template_document.document.paragraphs[7].list == WordListKind::Numbered &&
                template_document.document.paragraphs[9].runs[0].text.find("[待填写]") != std::string::npos,
            "meeting template uses real lists and explicit placeholder fields");
        bridge.undo();
        check(wrapper->textDocument()->toPlainText().isEmpty(), "template insertion is one undoable edit");
        bridge.redo();
        check(wrapper->textDocument()->toPlainText().contains(QStringLiteral("会议纪要")),
            "template insertion can be redone");
        check(save_word_file(
                  output.filePath("word-workflows-interop.docx").toStdString(), template_document.document)
                  .success,
            "list template fixture is written for independent inspection");

        bridge.requestNew();
        bridge.resolveUnsaved("discard");
        wait(bridge);
        cursor = QTextCursor(wrapper->textDocument());
        cursor.insertText("alpha alpha\nbeta");
        check(
            bridge.format(0, 5, "size", 10.5) && bridge.format(0, 5, "strike", true) && bridge.copyFormat(5),
            "format brush captures preceding selected run at its end boundary");
        check(bridge.pasteFormat(12, 16) && bridge.inspect(13).value("strike").toBool() &&
                bridge.inspect(13).value("size").toDouble() == 10.5,
            "format brush applies to target without copying text");
        bridge.undo();
        check(!bridge.inspect(13).value("strike").toBool(), "format brush is one undo step");
        check(
            bridge.replaceAll("alpha", "word") && wrapper->textDocument()->toPlainText() == "word word\nbeta",
            "replace all uses stable positions");
        bridge.undo();
        check(wrapper->textDocument()->toPlainText() == "alpha alpha\nbeta",
            "replace all undo restores every match");
        check(!bridge.replaceAll("alpha", QString(maximum_word_paragraph_bytes, 'x')) &&
                wrapper->textDocument()->toPlainText() == "alpha alpha\nbeta",
            "oversized replacement fails atomically");
        check(bridge.format(0, 5, "characterBorder", "#123456") && bridge.copySelection(0, 5, false) &&
                bridge.paste(12, 16) && wrapper->textDocument()->toPlainText() == "alpha alpha\nalpha" &&
                bridge.inspect(13).value("strike").toBool() &&
                bridge.inspect(13).value("size").toDouble() == 10.5 &&
                bridge.inspect(13).value("characterBorder").toString() == "#123456",
            "rich clipboard preserves character style and custom border");
        bridge.undo();
        check(wrapper->textDocument()->toPlainText() == "alpha alpha\nbeta" &&
                !bridge.inspect(13).value("strike").toBool(),
            "rich paste is a single reversible transaction");
        check(bridge.pastePlain(12, 16) && !bridge.inspect(13).value("strike").toBool(),
            "plain paste keeps destination style");
        bridge.undo();
        auto* rich = new QMimeData;
        rich->setHtml("<p><b>external</b></p>");
        QGuiApplication::clipboard()->setMimeData(rich);
        check(bridge.paste(12, 16) && bridge.inspect(13).value("bold").toBool(),
            "external basic HTML formatting imports");
        bridge.undo();
        auto* unsafe = new QMimeData;
        unsafe->setHtml("<p><img src='https://example.invalid/not-loaded.png'></p>");
        QGuiApplication::clipboard()->setMimeData(unsafe);
        check(!bridge.paste(12, 16) && wrapper->textDocument()->toPlainText() == "alpha alpha\nbeta",
            "unsupported clipboard objects fail atomically without loading resources");
        bridge.requestNew();
        bridge.resolveUnsaved("discard");
        wait(bridge);
        cursor = QTextCursor(wrapper->textDocument());
        cursor.insertText("中文");
        cursor.insertText(QString::fromUcs4(U"😀"));
        const auto whole = bridge.readContent({{"view", "content"}}).value("items").toList().front().toMap();
        check(whole.value("textComplete").toBool() && whole.value("end").toInt() == 4 &&
                bridge.readContent({{"view", "text"}, {"index", 0}}).value("textComplete").toBool() &&
                !bridge.readContent({{"view", "text"}, {"index", 0}, {"offset", 2}})
                    .value("textComplete")
                    .toBool(),
            "complete previews preserve UTF-16 and a final suffix is not labelled a full paragraph");
        check(!bridge.readContent({{"view", "format"}, {"index", 0}, {"offset", 3}}).value("ok").toBool() &&
                bridge.readContent({{"view", "format"}, {"index", 0}, {"offset", 2}}).value("ok").toBool(),
            "Word format offsets respect UTF-16 surrogate boundaries");
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(QString(239, 'a') + QString::fromUcs4(U"😀") + 'z');
        cursor.endEditBlock();
        const auto preview =
            bridge.readContent({{"view", "content"}}).value("items").toList().front().toMap();
        const auto long_format = bridge.readContent({{"view", "format"}, {"index", 0}});
        check(!preview.value("textComplete").toBool() &&
                preview.value("textPreview").toString().size() == 239 &&
                preview.value("end").toInt() == 242 && !long_format.value("textComplete").toBool() &&
                long_format.value("textPreview") == preview.value("textPreview") &&
                bridge.readContent({{"view", "text"}, {"index", 0}}).value("textComplete").toBool(),
            "bounded previews cannot claim completeness or split a surrogate at the character limit");
        wrapper->textDocument()->undo();
        cursor.setPosition(4);
        cursor.deletePreviousChar();
        check(bridge.format(0, 2, "rubyAuto", true) &&
                bridge.inspect(1).value("ruby") == QStringLiteral("zhōng"),
            "phonetic guide reaches live editor through public command");
        check(bridge.format(0, 2, "align", 4) && bridge.inspect(1).value("align") == 4,
            "distributed alignment reaches live editor through public command");
        const auto phonetic = extract_word_document(*wrapper->textDocument());
        check(phonetic.success &&
                save_word_file(output.filePath("word-phonetic-interop.docx").toStdString(), phonetic.document)
                    .success,
            "phonetic DOCX fixture available for independent XML reader");
        bridge.requestNew();
        bridge.resolveUnsaved("discard");
        wait(bridge);
        cursor = QTextCursor(wrapper->textDocument());
        cursor.insertText(QString(maximum_word_paragraph_bytes + 1, QChar('x')));
        QCoreApplication::processEvents();
        check(wrapper->textDocument()->toPlainText().isEmpty() && bridge.message().contains("8 KiB"),
            "continuous typing beyond a paragraph budget is reverted without leaving invalid state");
        cursor = QTextCursor(wrapper->textDocument());
        const QString prefix(maximum_word_paragraph_bytes, QChar('x'));
        cursor.insertText(prefix);
        cursor.insertText("y");
        check(wrapper->textDocument()->toPlainText() == prefix,
            "rejected typing preserves earlier valid input even when Qt merges undo commands");
        auto protected_parts = serialize_word(WordDocument{}).parts;
        protected_parts.back().bytes =
            R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
<w:p><w:bookmarkStart w:id="1" w:name="protected"/><w:r><w:t>Locked</w:t></w:r><w:bookmarkEnd w:id="1"/></w:p>
<w:p><w:r><w:t>Plain</w:t></w:r></w:p><w:sectPr/></w:body></w:document>)xml";
        const auto protected_source = parse_word(protected_parts);
        const auto protected_path = temporary.filePath("protected.docx");
        check(protected_source.success &&
                save_word_file(protected_path.toStdString(), protected_source.document).success,
            "protected structure fixture saves");
        bridge.requestOpen(QUrl::fromLocalFile(protected_path));
        bridge.resolveUnsaved("discard");
        wait(bridge);
        const auto protected_copy = temporary.filePath("protected-copy.docx");
        bridge.requestEditableCopy();
        bridge.selectSaveFile(QUrl::fromLocalFile(protected_copy));
        wait(bridge);
        const auto before_split = wrapper->textDocument()->toPlainText();
        cursor = QTextCursor(wrapper->textDocument());
        cursor.setPosition(2);
        cursor.insertBlock();
        check(wrapper->textDocument()->toPlainText() == before_split &&
                bridge.message().contains("结构修改已撤销"),
            "native Enter cannot leave an unsavable split through a bookmark");
        cursor = QTextCursor(wrapper->textDocument());
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText("Third");
        check(wrapper->textDocument()->blockCount() == 3 &&
                bridge.format(cursor.position() - 5, cursor.position(), "size", 10.5) &&
                bridge.copyFormat(cursor.position()),
            "ordinary Enter remains editable and format brush handles a newly created paragraph");
        bridge.save();
        wait(bridge);
        const auto structural_saved = load_word_file(protected_copy.toStdString());
        check(!bridge.modified() && structural_saved.success &&
                structural_saved.document.paragraphs.size() == 3 &&
                structural_saved.document.paragraphs.back().runs.front().text == "Third",
            "live structural edits save through the bridge and reopen");
        auto image_parts = serialize_word(WordDocument{}).parts;
        image_parts.back().bytes = R"xml(
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:v="urn:schemas-microsoft-com:vml"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body>
<w:p><w:r><w:t>Before </w:t></w:r><w:r><w:pict><v:shape style="width:20pt;height:10pt">
<v:imagedata r:id="preview"/></v:shape></w:pict></w:r><w:r><w:t> after</w:t></w:r></w:p>
<w:p><w:r><w:t>Plain</w:t></w:r></w:p></w:body></w:document>)xml";
        for (auto& part : image_parts)
            if (part.path == "word/_rels/document.xml.rels")
                part.bytes.insert(part.bytes.find("</Relationships>"),
                    "<Relationship Id='preview' "
                    "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/image' "
                    "Target='media/preview.png'/>");
        image_parts.push_back({"word/media/preview.png", "undecodable preview is still a protected object"});
        const auto image_source = parse_word(image_parts);
        const auto image_path = temporary.filePath("image-source.docx");
        check(image_source.success && image_source.document.images.size() == 1 &&
                save_word_file(image_path.toStdString(), image_source.document).success,
            "protected picture fixture saves");
        bridge.requestOpen(QUrl::fromLocalFile(image_path));
        wait(bridge);
        const auto image_copy = temporary.filePath("image-copy.docx");
        bridge.requestEditableCopy();
        bridge.selectSaveFile(QUrl::fromLocalFile(image_copy));
        wait(bridge);
        auto* image_document = wrapper->textDocument();
        cursor = QTextCursor(image_document);
        cursor.insertText("Valid prefix ");
        const auto accepted_text = image_document->toPlainText();
        const auto picture_position = accepted_text.indexOf(QChar::ObjectReplacementCharacter);
        check(picture_position >= 0, "image occurrence has a visible protected character");
        if (picture_position >= 0)
        {
            check(!bridge.insertText(picture_position, picture_position + 1, "replacement") &&
                    image_document->toPlainText() == accepted_text,
                "replace and paste preflight reject replacing a protected picture");
            cursor.setPosition(picture_position);
            cursor.deleteChar();
            check(image_document->toPlainText() == accepted_text && bridge.message().contains("原文档图片"),
                "native Delete restores a picture immediately without losing accepted typing");
            cursor = QTextCursor(image_document);
            cursor.setPosition(picture_position + 1);
            cursor.deletePreviousChar();
            check(image_document->toPlainText() == accepted_text,
                "native Backspace cannot leave an unsaveable missing picture");
            bridge.redo();
            check(image_document->toPlainText() == accepted_text,
                "redo cannot bypass protected-image validation");
            check(bridge.format(0, image_document->characterCount() - 1, "bold", true),
                "formatting a selection containing an unchanged picture is still allowed");
            bridge.save();
            wait(bridge);
            const auto image_saved = load_word_file(image_copy.toStdString());
            check(!bridge.modified() && image_saved.success && image_saved.document.images.size() == 1,
                "accepted typing and formatting save after rejected image deletion");
        }

        bridge.requestNew();
        bridge.resolveUnsaved("discard");
        wait(bridge);
        check(bridge.insertText(0, 0, "AA BB\nTail") && bridge.format(3, 5, "bold", true),
            "selection direction fixture prepares distinct character styles");
        check(QMetaObject::invokeMethod(editor_item, "select", Q_ARG(int, 5), Q_ARG(int, 3)) &&
                page->property("selection").toMap().value("bold").toBool(),
            "backward selection inspects the first selected character instead of the preceding space");
        page->setProperty("group", "paragraph");
        page->setProperty("section", "align");
        check(bridge.format(3, 5, "lineSpacing", QVariantMap{{"rule", 1}, {"value", 18.75}}),
            "fixed line spacing applies through public bridge");
        QCoreApplication::processEvents();
        auto* rule_tool = page->findChild<QObject*>("wordLineSpacingRule");
        auto* line_tool = page->findChild<QQuickItem*>("wordLineSpacingValue");
        check(rule_tool && line_tool && rule_tool->property("currentIndex").toInt() == 1 &&
                line_tool->property("value").toInt() == 1875,
            "line-spacing controls reflect the actual fixed rule and fractional point value");
        if (line_tool)
        {
            line_tool->forceActiveFocus();
            line_tool->setProperty("value", 1925);
            check(QMetaObject::invokeMethod(line_tool, "valueModified") && line_tool->hasFocus() &&
                    editor_item->property("selectionStart").toInt() == 3 &&
                    editor_item->property("selectionEnd").toInt() == 5 &&
                    bridge.inspect(4).value("lineSpacingPoints").toDouble() == 19.25,
                "editing a numeric tool retains its focus and the text selection");
            bridge.undo();
            QCoreApplication::processEvents();
            check(line_tool->property("value").toInt() == 1875,
                "undo refreshes the numeric toolbar value without moving the cursor");
        }
        page->setProperty("section", "indent");
        QCoreApplication::processEvents();
        auto* right_tool = find_item(qobject_cast<QQuickItem*>(page.get()), "wordParagraph_rightIndent");
        check(right_tool && bridge.format(3, 5, "rightIndent", 12.75) &&
                right_tool->property("value").toInt() == 1275,
            "right-indent editing and inspection share fractional point semantics");
        check(!bridge.format(3, 5, "firstLineIndent", -18) && bridge.message().contains("悬挂"),
            "invalid hanging indent explains how to correct the left indentation");
        check(bridge.format(3, 5, "leftIndent", 36) && bridge.message().isEmpty(),
            "successful correction clears the previous format failure");
        auto table_parts = serialize_word(WordDocument{}).parts;
        table_parts.back().bytes =
            R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
<w:p><w:r><w:t>单元格样式编辑检查</w:t></w:r></w:p>
<w:tbl><w:tblPr><w:tblBorders><w:top w:val="single" w:sz="8"/><w:bottom w:val="single" w:sz="8"/><w:left w:val="single" w:sz="8"/><w:right w:val="single" w:sz="8"/><w:insideH w:val="single" w:sz="8"/><w:insideV w:val="single" w:sz="8"/></w:tblBorders></w:tblPr><w:tblGrid><w:gridCol w:w="4000"/><w:gridCol w:w="4000"/></w:tblGrid>
<w:tr><w:tc><w:tcPr><w:vMerge w:val="restart"/></w:tcPr><w:p><w:r><w:t>底色、垂直居中和独立内边距</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>未修改的右侧单元格</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:tc><w:tcPr><w:vMerge/></w:tcPr><w:p/></w:tc><w:tc><w:p><w:r><w:t>合并结构保持不变</w:t></w:r></w:p></w:tc></w:tr></w:tbl><w:sectPr/></w:body></w:document>)xml";
        const auto table_source = parse_word(table_parts);
        const auto table_path = output.filePath("word-cell-source.docx");
        check(table_source.success && save_word_file(table_path.toStdString(), table_source.document).success,
            "save independently verifiable cell style source");
        bridge.requestOpen(QUrl::fromLocalFile(table_path));
        bridge.resolveUnsaved("discard");
        wait(bridge);
        bridge.requestEditableCopy();
        bridge.selectSaveFile(QUrl::fromLocalFile(output.filePath("word-cell-interop.docx")));
        wait(bridge);
        auto* cell_document = wrapper->textDocument();
        QTextTable* cell_table = nullptr;
        for (auto* frame : cell_document->rootFrame()->childFrames())
            if (auto* table = qobject_cast<QTextTable*>(frame))
                cell_table = table;
        check(cell_table != nullptr, "cell editor table exists");
        if (cell_table)
        {
            const auto start = cell_table->cellAt(0, 0).firstCursorPosition().position();
            editor_item->setProperty("cursorPosition", start);
            page->setProperty("section", "cell");
            check(bridge.format(start, start, "cellFill", "#CCDDEE") &&
                    bridge.format(start, start, "cellAlign", 1) &&
                    bridge.format(start, start, "cellLeft", 18.5) &&
                    bridge.format(start, start, "cellTop", 12.25) &&
                    bridge.format(start, start, "cellRight", 9.75) &&
                    bridge.format(start, start, "cellBottom", 24.0),
                "cell formatting is exposed through the same public bridge as the toolbar");
            QCoreApplication::processEvents();
            const auto* align = find_item(qobject_cast<QQuickItem*>(page.get()), "wordCellAlignment");
            const auto* padding = find_item(qobject_cast<QQuickItem*>(page.get()), "wordParagraph_cellLeft");
            check(align && align->property("enabled").toBool() &&
                    align->property("currentIndex").toInt() == 1 && padding &&
                    padding->property("value").toInt() == 1850,
                "cell toolbar reads live vertical alignment and fractional padding");
            const auto* border_edge = find_item(qobject_cast<QQuickItem*>(page.get()), "wordCellBorderEdge");
            auto* border_style = find_item(qobject_cast<QQuickItem*>(page.get()), "wordCellBorderStyle");
            auto* border_width = find_item(qobject_cast<QQuickItem*>(page.get()), "wordCellBorderWidth");
            check(border_edge && border_style && border_width && border_edge->property("enabled").toBool(),
                "cell border toolbar is enabled by the shared capability");
            if (border_style && border_width)
            {
                border_style->setProperty("currentIndex", 2);
                QMetaObject::invokeMethod(border_style, "activated", Q_ARG(int, 2));
                QCoreApplication::processEvents();
                border_width->setProperty("value", 10);
                QMetaObject::invokeMethod(border_width, "valueModified");
                QCoreApplication::processEvents();
                const auto border = bridge.inspect(start)
                                        .value("cellBorders")
                                        .toMap()
                                        .value("source")
                                        .toMap()
                                        .value("left")
                                        .toMap();
                check(
                    border.value("style").toString() == "double" && border.value("width").toDouble() == 1.25,
                    "actual QML toolbar signals edit shared border contract with fractional points");
                bridge.undo();
                bridge.undo();
                QCoreApplication::processEvents();
            }
            bridge.save();
            wait(bridge);
            check(!bridge.modified(), "cell toolbar edits save successfully");
        }
    }
}

void qml_register_types_Mirrorfly_Native();

int run_word_ui_tests(int argc, char* argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    qml_register_types_Mirrorfly_Native();
    QStandardPaths::setTestModeEnabled(true);
    QQmlEngine engine;
    loading_cases(engine);
    layout_handoff_case(engine);
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& warnings)
    {
        for (const auto& warning : warnings)
        {
            ++failures;
            std::cerr << warning.toString().toStdString() << '\n';
        }
    });
    cases(engine);
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_ui_tests(argc, argv);
}
