#include "automation_bridge.hpp"
#include "office_ai_toolbox.hpp"
#include "word_bridge.hpp"
#include "word_document.hpp"
#include <mirrorfly/automation.hpp>

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QThreadPool>
#include <iostream>

namespace
{
    using namespace mirrorfly;
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
        QElapsedTimer clock;
        clock.start();
        while (!ready() && clock.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return ready();
    }

    QByteArray bytes(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }

    QJsonObject execute(OfficeAiToolbox& toolbox, const QString& tool, const QJsonObject& arguments)
    {
        toolbox.beginResponse(QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot()))
                                  .object()
                                  .value("revision")
                                  .toString(),
            1);
        QJsonObject result;
        bool finished = false;
        toolbox.execute(tool, arguments, [&](const QJsonObject& value)
        {
            result = value;
            finished = true;
        });
        check(wait_for(
                  [&]()
        {
            return finished;
        }),
            "local AI action completes without a window or network");
        return result;
    }

    void unicode_case(QQmlEngine& engine, const QString& output)
    {
        const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        const auto original = source.filePath("tests/fixtures/word-unicode.docx");
        const auto original_bytes = bytes(original);
        const QString first = QStringLiteral("中文 A🦋B𠀀 末尾é");
        const QString target = QStringLiteral("🦋B𠀀");
        const QString whole = first + QStringLiteral("\n保留段落 Latin");
        WordBridge bridge;
        check(!original_bytes.isEmpty() && bridge.requestOpen(QUrl::fromLocalFile(original)) &&
                wait_for(
                    [&]()
        {
            return !bridge.locked();
        }),
            "independent Unicode fixture imports");
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
TextEdit
{
    width: 720
    textFormat: TextEdit.RichText
    function automationReady() { return true; }
    function automationState() { return {module: "word", pendingInput: false}; }
}
)qml",
            QUrl{});
        std::unique_ptr<QObject> item(component.create());
        check(item != nullptr, "isolated editor constructs");
        if (!item)
            return;
        auto* wrapper = item->property("textDocument").value<QQuickTextDocument*>();
        bridge.loadEditor(wrapper);
        auto* document = wrapper->textDocument();
        check(document->toPlainText() == whole && bridge.readOnly(),
            "import preserves supplementary characters and decomposed accents in a protected source");
        QTemporaryDir directory;
        const auto destination = directory.filePath("word-unicode-edited.docx");
        check(bridge.createEditableCopyTo(QUrl::fromLocalFile(destination)) &&
                wait_for(
                    [&]()
        {
            return !bridge.locked();
        }) && !bridge.readOnly(),
            "Unicode editing uses the public independent-copy workflow");
        document = wrapper->textDocument();
        AutomationBridge automation;
        automation.registerModule("word", &bridge);
        automation.setUiRoot(item.get());
        OfficeAiToolbox toolbox(directory.path());
        check(toolbox.query("office_load_group", {{"group", "word"}}).value("ok").toBool(),
            "AI Word group loads for the attached document");
        const auto content = toolbox.query("office_read", {{"module", "word"}, {"view", "content"}});
        const auto paragraph = content.value("items").toArray().first().toObject();
        check(paragraph.value("textComplete").toBool() && paragraph.value("textPreview") == first &&
                paragraph.value("end").toInt() == first.size(),
            "AI paragraph offsets use UTF-16 units");
        const auto found = execute(toolbox, "office_action",
            {{"op", "word.find"},
                {"args", QJsonObject{{"query", target}, {"from", 0}, {"backward", false}}}});
        const auto range = found.value("result").toObject();
        const int start = range.value("start").toInt(-1);
        const int end = range.value("end").toInt(-1);
        check(found.value("ok").toBool() && start == 4 && end == 9,
            "AI find returns whole supplementary characters and an exclusive end");
        for (const int offset : {5, 8})
            check(!toolbox
                      .query("office_read",
                          {{"module", "word"}, {"view", "format"}, {"index", 0}, {"offset", offset}})
                      .value("ok")
                      .toBool(),
                "format reads reject the middle of a surrogate pair");
        const auto format = [&](const QString& action, const QJsonValue& value)
        {
            return execute(toolbox, "office_action",
                {{"op", "word.format"},
                    {"args",
                        QJsonObject{{"start", start}, {"end", end}, {"action", action}, {"value", value}}}});
        };
        check(format("size", 12.5).value("ok").toBool() && format("bold", true).value("ok").toBool(),
            "AI applies half-point size and bold to the observed Unicode range");
        check(document->toPlainText() == whole && bridge.inspect(6).value("size").toDouble() == 12.5 &&
                bridge.inspect(6).value("bold").toBool() &&
                bridge.inspect(2).value("size").toDouble() == 10.5 &&
                !bridge.inspect(2).value("bold").toBool() &&
                bridge.inspect(11).value("size").toDouble() == 10.5,
            "formatting changes only the target, without normalizing the combining accent");
        check(execute(toolbox, "office_action", {{"op", "word.undo"}, {"args", QJsonObject{}}})
                    .value("ok")
                    .toBool() &&
                !bridge.inspect(6).value("bold").toBool() &&
                bridge.inspect(6).value("size").toDouble() == 12.5,
            "AI undo removes just the last character-format transaction");
        check(execute(toolbox, "office_action", {{"op", "word.redo"}, {"args", QJsonObject{}}})
                    .value("ok")
                    .toBool() &&
                bridge.inspect(6).value("bold").toBool(),
            "AI redo restores the selected Unicode style");
        document->documentLayout()->documentSize();
        const auto* layout = document->begin().layout();
        check(layout && layout->lineCount() > 0 && !layout->isValidCursorPosition(5) &&
                !layout->isValidCursorPosition(8) && layout->isValidCursorPosition(4) &&
                layout->isValidCursorPosition(9),
            "shaped layout preserves supplementary cursor boundaries");
        check(execute(toolbox, "office_save", {{"current", true}}).value("ok").toBool() && !bridge.modified(),
            "AI current save confirms the edited Unicode copy");
        const auto saved = load_word_file(destination.toStdString());
        auto reopened = create_word_document(saved.document);
        check(saved.success && reopened->toPlainText() == whole &&
                inspect_word_document(*reopened, 6).value("size").toDouble() == 12.5 &&
                inspect_word_document(*reopened, 6).value("bold").toBool(),
            "saved copy reopens with identical Unicode paragraphs and target style");
        check(bytes(original) == original_bytes, "editing never changes the imported source bytes");
        QSaveFile artifact(QDir(output).filePath("word-unicode-edited.docx"));
        const auto saved_bytes = bytes(destination);
        check(artifact.open(QIODevice::WriteOnly) && artifact.write(saved_bytes) == saved_bytes.size() &&
                artifact.commit(),
            "retain actual AI output for independent python-docx and ZIP inspection");
    }
}

int run_word_unicode_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const auto output = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY)).filePath("build");
    QQmlEngine engine;
    unicode_case(engine, output);
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_unicode_tests(argc, argv);
}
