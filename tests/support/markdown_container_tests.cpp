#include "markdown_container_tests.hpp"
#include "editor_tools.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <iostream>
#include <mirrorfly/markdown.hpp>
namespace
{
    bool check(bool value, const char* message)
    {
        if (!value)
            std::cerr << "FAIL: " << message << '\n';
        return value;
    }
    std::vector<std::string> shape(const QString& source)
    {
        std::vector<std::string> result;
        for (const auto& paragraph : mirrorfly::markdown_paragraphs(source.toUtf8().toStdString()))
        {
            std::string value = paragraph.heading ? "heading:" : "paragraph:";
            for (const auto& node : paragraph.containers)
                value += node.kind + std::to_string(node.identity) + "/";
            value += ":";
            for (const auto& run : paragraph.runs)
                value += run.style.text;
            result.push_back(value);
        }
        return result;
    }
}
bool test_markdown_containers(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    bool passed = true;
    QJsonArray cases;
    for (const QString& original : {QStringLiteral("- first\n\n  continuation **bold**\n\n- second\n"),
             QStringLiteral("- parent\n\n  > quote\n  >\n  > next\n\n  after\n\n- tail\n"),
             QStringLiteral("> - parent\n>\n>   continuation\n>\n> - tail\n"),
             QStringLiteral("- parent\n\n  - child\n\n    child more\n\n  parent more\n\n- tail\n"),
             QStringLiteral("3. parent\n\n   continuation\n\n4. tail\n"),
             QStringLiteral("- [x] task\n\n  continuation\n\n- [ ] tail\n"),
             QStringLiteral("- item\n\n  # Heading\n\n  later\n\n- tail\n"),
             QStringLiteral("- parent\n\n  > - nested\n  >\n  >   nested continuation\n\n- tail\n")})
    {
        passed = check(tools.loadDocument(wrapper, original, true, theme).value("valid").toBool(),
                     "multi-paragraph containers import through the public visual interface") &&
            passed;
        auto* document = wrapper->textDocument();
        const auto raw = document->toRawText();
        passed = check(tools.applyEdit(wrapper, 0, 1, "italic").value("valid").toBool(),
                     "actual character transaction edits a container without changing ownership") &&
            passed;
        const auto saved = tools.sourceText(wrapper);
        passed = check(tools.canSave() && shape(saved) == shape(original),
                     "nested quote/list order and repeated item identity survive the actual save") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == original,
                     "container style transaction undoes to the exact original source") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == saved,
                     "container style redo retains the same serialized ownership") &&
            passed;
        tools.loadDocument(wrapper, saved, true, theme);
        passed = check(wrapper->textDocument()->toRawText() == raw &&
                         shape(tools.sourceText(wrapper)) == shape(original),
                     "container save-reopen preserves visible text and semantic ownership") &&
            passed;
        cases.append(QJsonObject{{"original", original}, {"source", saved}});
    }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    const QString moving =
        "- first\n- parent\n\n  > quote\n\n  - child\n\n    continuation\n\n  after\n\n- tail\n";
    const QString expected =
        "- first\n  - parent\n\n    > quote\n\n    - child\n\n      continuation\n\n    after\n\n- tail\n";
    tools.loadDocument(wrapper, moving, true, theme);
    auto* document = wrapper->textDocument();
    const auto raw = document->toRawText();
    const int parent = raw.indexOf("parent");
    passed = check(tools.applyEdit(wrapper, parent, parent, "listIndent").value("valid").toBool(),
                 "parent indent moves continued paragraphs, child items and owned quote together") &&
        passed;
    const auto moved = tools.sourceText(wrapper);
    passed = check(tools.canSave() && shape(moved) == shape(expected) &&
                     document->findBlock(raw.indexOf("child")).textList()->format().indent() == 3 &&
                     document->findBlock(raw.indexOf("continuation")).blockFormat().indent() == 3 &&
                     document->findBlock(raw.indexOf("after")).blockFormat().indent() == 2,
                 "actual visual indentation matches serialized ownership for every continued block") &&
        passed;
    document->undo();
    passed =
        check(tools.sourceText(wrapper) == moving, "parent move has one complete undo transaction") && passed;
    document->redo();
    passed =
        check(tools.sourceText(wrapper) == moved, "parent move redo preserves all container descendants") &&
        passed;
    tools.loadDocument(wrapper, moved, true, theme);
    passed = check(wrapper->textDocument()->toRawText() == raw &&
                     tools.applyEdit(wrapper, parent, parent, "listOutdent").value("valid").toBool() &&
                     shape(tools.sourceText(wrapper)) == shape(moving),
                 "outdent after actual save-reopen restores continued paragraph and quote ownership") &&
        passed;
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("containers.json"));
        const auto bytes = QJsonDocument(cases).toJson(QJsonDocument::Compact);
        passed = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && passed;
        QFile move_file(QDir(output).filePath("container-moves.json"));
        const auto moves =
            QJsonDocument(QJsonArray{QJsonObject{{"original", moving}, {"source", moved},
                              {"expected", expected}, {"restored", tools.sourceText(wrapper)}}})
                .toJson(QJsonDocument::Compact);
        passed = move_file.open(QIODevice::WriteOnly) && move_file.write(moves) == moves.size() && passed;
    }
    tools.loadDocument(wrapper, "- original\n- tail\n", true, theme);
    QTextCursor inserted(wrapper->textDocument());
    inserted.movePosition(QTextCursor::EndOfBlock);
    inserted.beginEditBlock();
    inserted.insertBlock();
    inserted.insertText("new");
    inserted.endEditBlock();
    const auto entered = tools.sourceText(wrapper);
    const auto items = mirrorfly::markdown_paragraphs(entered.toStdString());
    passed =
        check(tools.canSave() && items.size() == 3 && items[1].containers.size() == 1 &&
                items[0].containers[0].identity != items[1].containers[0].identity,
            "native paragraph insertion creates a distinct list item instead of a continued paragraph") &&
        passed;
    wrapper->textDocument()->undo();
    passed = check(tools.sourceText(wrapper) == "- original\n- tail\n",
                 "native new list item restores original ownership with one undo") &&
        passed;
    wrapper->textDocument()->redo();
    passed =
        check(tools.sourceText(wrapper) == entered, "native new list item has stable source after redo") &&
        passed;
    tools.loadDocument(wrapper, entered, true, theme);
    passed = check(wrapper->textDocument()->toPlainText() == "original\nnew\ntail",
                 "native new list item survives actual save-reopen as three separate items") &&
        passed;
    if (!output.isEmpty())
    {
        QFile enter_file(QDir(output).filePath("container-enter.json"));
        const auto bytes =
            QJsonDocument(QJsonObject{{"source", entered}, {"expected", "- original\n- new\n- tail\n"}})
                .toJson(QJsonDocument::Compact);
        passed = enter_file.open(QIODevice::WriteOnly) && enter_file.write(bytes) == bytes.size() && passed;
    }
    tools.loadDocument(wrapper, "first\n\nparent\n\ntail\n", true, theme);
    const auto created_raw = wrapper->textDocument()->toRawText();
    passed = check(tools.applyEdit(wrapper, 0, created_raw.size(), "bullet").value("valid").toBool() &&
                     tools
                         .applyEdit(wrapper, created_raw.indexOf("parent"), created_raw.indexOf("parent"),
                             "listIndent")
                         .value("valid")
                         .toBool(),
                 "fresh list conversion and indentation use the same public transactions") &&
        passed;
    const auto created = tools.sourceText(wrapper);
    const auto created_items = mirrorfly::markdown_paragraphs(created.toStdString());
    passed = check(tools.canSave() && created_items.size() == 3 && created_items[1].list_depth == 2 &&
                     created_items[1].containers[0].identity == created_items[0].containers[0].identity &&
                     created_items[2].list_depth == 1,
                 "newly created lists retain their actual parent when source ownership starts empty") &&
        passed;
    if (!output.isEmpty())
    {
        QFile created_file(QDir(output).filePath("container-created.json"));
        const auto bytes =
            QJsonDocument(QJsonObject{{"source", created}, {"expected", "- first\n  - parent\n- tail\n"}})
                .toJson(QJsonDocument::Compact);
        passed =
            created_file.open(QIODevice::WriteOnly) && created_file.write(bytes) == bytes.size() && passed;
    }
    return passed;
}
