#include "markdown_block_container_tests.hpp"
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
#include <QTextTable>
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
    std::vector<std::string> block_shape(const QString& source)
    {
        std::vector<std::string> result;
        for (const auto& block : mirrorfly::markdown_blocks(source.toUtf8().toStdString()))
        {
            auto value = block.kind + ":" + block.language + ":" + block.text + ":";
            for (const auto& node : block.containers)
                value += node.kind + std::to_string(node.identity) + "/" + node.opening + "/";
            result.push_back(value);
        }
        return result;
    }
}
bool test_markdown_block_containers(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    bool passed = true;
    QJsonArray cases;
    const QString fence(3, u'\x60');
    const QStringList originals{
        "- parent\n\n  " + fence + "cpp\n  int n = 1;\n  " + fence + "\n\n  after\n\n- tail\n",
        "> - parent\n>\n>   " + fence + "cpp\n>   x\n>   " + fence + "\n>\n>   after\n>\n> - tail\n",
        "- parent\n\n  > " + fence + "cpp\n  > x\n  > " + fence + "\n\n  after\n\n- tail\n",
        "- parent\n\n  > | A | B |\n  > | --- | :---: |\n  > | a | b |\n  >\n  > after\n\n- tail\n",
        "- parent\n\n  > - nested\n  >\n  >   " + fence + "cpp\n  >   x\n  >   " + fence +
            "\n  >\n  >   | A |\n  >   | --- |\n  >   | a |\n  >\n  >   ---\n  >\n  >   next\n\n- tail\n",
        "before\n\n" + fence + "cpp\nx\n" + fence + "\n\n" + fence + "cpp\ny\n" + fence + "\n\nafter\n",
        "- parent\n\n  ---\n\n  after\n\n- tail\n",
        "100) parent\n\n     > " + fence + "\n     > x\n     > " + fence + "\n\n     after\n\n101) tail\n",
        "> - | A |\n>   | --- |\n>   | a |\n", "-\n  " + fence + "cpp\n  x\n  " + fence + "\n- tail\n"};
    for (const auto& original : originals)
    {
        const auto imported = tools.loadDocument(wrapper, original, true, theme);
        if (!imported.value("valid").toBool())
            std::cerr << "mixed import " << imported.value("error").toString().toStdString() << '\n';
        passed = check(imported.value("valid").toBool(),
                     "mixed block containers import through the public visual interface") &&
            passed;
        auto* document = wrapper->textDocument();
        if (original.startsWith("-\n"))
        {
            passed = check(document->begin().textList() != nullptr,
                         "a code-only first list item retains an actual visible list marker") &&
                passed;
        }
        const auto raw = document->toRawText();
        const QString selected = raw.contains("parent") ? "parent"
            : raw.contains("before")                    ? "before"
            : raw.contains("tail")                      ? "tail"
                                                        : "A";
        const int position = raw.indexOf(selected);
        const auto edited = tools.applyEdit(wrapper, position, position + 1, "italic");
        if (!edited.value("valid").toBool())
        {
            std::cerr << "mixed edit source=" << original.toStdString()
                      << " error=" << edited.value("error").toString().toStdString() << '\n';
        }
        passed = check(position >= 0 && edited.value("valid").toBool(),
                     "actual parent paragraph style edits a compound block container") &&
            passed;
        const auto saved = tools.sourceText(wrapper);
        const bool compatible = tools.canSave() && block_shape(original) == block_shape(saved);
        if (!compatible)
            std::cerr << "mixed original=" << original.toStdString() << "saved=" << saved.toStdString()
                      << '\n';
        passed =
            check(compatible, "code/table/rule content and ordered ownership survive actual serialization") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == original,
                     "compound block style undo restores original source") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == saved, "compound block redo retains ownership") && passed;
        tools.loadDocument(wrapper, saved, true, theme);
        passed = check(wrapper->textDocument()->toRawText() == raw &&
                         block_shape(tools.sourceText(wrapper)) == block_shape(original),
                     "compound block source and visible text survive actual save-reopen") &&
            passed;
        cases.append(QJsonObject{{"original", original}, {"source", saved}, {"selected", selected}});
    }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("block-containers.json"));
        const auto bytes = QJsonDocument(cases).toJson(QJsonDocument::Compact);
        passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                     "export real compound block saves for independent CommonMark/GFM inspection") &&
            passed;
    }
    return passed;
}

bool test_markdown_block_quotes(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    bool passed = true;
    QJsonArray cases;
    const QString body = "- parent\n\n  > ```cpp\n  > x\n  > ```\n  >\n"
                         "  > | A |\n  > | :---: |\n  > | a |\n  >\n  > ---\n\n  after\n\n";
    for (const bool quoted : {false, true})
    {
        QString original = quoted ? "> - first\n" : "- first\n";
        QString expected = original;
        const auto lines = body.split('\n');
        for (qsizetype index = 0; index + 1 < lines.size(); ++index)
        {
            const auto prefix = quoted ? QStringLiteral("> ") : QString{};
            original += prefix + lines[index] + '\n';
            expected += prefix + "> " + lines[index] + '\n';
        }
        original += quoted ? "> - tail\n" : "- tail\n";
        expected += quoted ? "> - tail\n" : "- tail\n";
        const auto imported = tools.loadDocument(wrapper, original, true, theme);
        passed = check(imported.value("valid").toBool(), "import mixed parent quote selection") && passed;
        auto* document = wrapper->textDocument();
        const auto raw = document->toRawText();
        const int parent = raw.indexOf("parent");
        const auto edited =
            tools.applyEdit(wrapper, parent, parent, "quoteSet", {{"quoteLevel", quoted ? 2 : 1}});
        const auto saved = tools.sourceText(wrapper);
        if (!edited.value("valid").toBool() || block_shape(saved) != block_shape(expected))
            std::cerr << "parent quote error=" << edited.value("error").toString().toStdString()
                      << " expected=" << expected.toStdString() << " saved=" << saved.toStdString() << '\n';
        passed = check(edited.value("valid").toBool() && tools.canSave() &&
                         block_shape(saved) == block_shape(expected),
                     "parent quote includes code/table/rule while retaining inner quote order") &&
            passed;
        document->undo();
        passed =
            check(tools.sourceText(wrapper) == original, "parent quote is one undo transaction") && passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == saved, "parent quote redo retains ownership") && passed;
        const auto reopened = tools.loadDocument(wrapper, saved, true, theme);
        passed = check(reopened.value("valid").toBool() && wrapper->textDocument()->toRawText() == raw,
                     "mixed parent quote survives actual save-reopen") &&
            passed;
        const int source_parent = original.indexOf("parent");
        mirrorfly::MarkdownOptions options;
        options.quote_level = quoted ? 2 : 1;
        const auto edit = mirrorfly::make_markdown_edit(
            original.toStdString(), source_parent, source_parent, "quoteSet", options);
        auto source_saved = original.toStdString();
        if (edit.valid)
            source_saved.replace(edit.start, edit.end - edit.start, edit.replacement);
        passed =
            check(edit.valid && block_shape(QString::fromStdString(source_saved)) == block_shape(expected),
                "public source parent quote agrees with actual visual ownership") &&
            passed;
        cases.append(QJsonObject{{"original", original}, {"source", saved}, {"expected", expected},
            {"sourceEdit", QString::fromStdString(source_saved)}});
    }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("block-quotes.json"));
        const auto bytes = QJsonDocument(cases).toJson(QJsonDocument::Compact);
        passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                     "export parent quote saves for independent verification") &&
            passed;
    }
    return passed;
}

bool test_markdown_block_moves(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    bool passed = true;
    QJsonArray cases;
    const QString fence(3, u'\x60');
    const QStringList bodies{"- parent\n\n  " + fence + "cpp\n  x\n  " + fence + "\n\n  after\n\n",
        "- parent\n\n  | A | B |\n  | --- | :---: |\n  | **a** | `b` |\n\n  after\n\n",
        "- parent\n\n  ---\n\n  after\n\n",
        "- parent\n\n  > " + fence + "cpp\n  > x\n  > " + fence +
            "\n  >\n  > | A |\n  > | :---: |\n  > | a |\n  >\n  > ---\n\n  after\n\n",
        "- parent\n\n  > - [x] child\n  >\n  >   " + fence + "cpp\n  >   x\n  >   " + fence +
            "\n  >\n  >   | A |\n  >   | --- |\n  >   | a |\n  >\n  >   ---\n  >\n  >   next\n\n  after\n\n",
        "-\n  " + fence + "cpp\n  x\n  " + fence + "\n"};
    for (const auto& body : bodies)
        for (const bool quoted : {false, true})
        {
            QString original = quoted ? "> - first\n" : "- first\n";
            QString expected = original;
            const auto lines = body.split('\n');
            for (qsizetype index = 0; index + 1 < lines.size(); ++index)
            {
                const auto prefix = quoted ? QStringLiteral("> ") : QString{};
                original += prefix + lines[index] + '\n';
                expected += prefix + "  " + lines[index] + '\n';
            }
            original += quoted ? "> - tail\n" : "- tail\n";
            expected += quoted ? "> - tail\n" : "- tail\n";
            if (body.startsWith("-\n"))
                expected.replace(quoted ? ">   -\n>     " : "  -\n    ", quoted ? ">   - " : "  - ");
            const auto imported = tools.loadDocument(wrapper, original, true, theme);
            passed = check(imported.value("valid").toBool(), "mixed subtree move imports editable source") &&
                passed;
            auto* document = wrapper->textDocument();
            const auto raw = document->toRawText();
            if (!imported.value("valid").toBool())
                continue;
            int parent = raw.indexOf("parent");
            if (parent < 0)
                for (auto block = document->begin().next(); block.isValid(); block = block.next())
                    if (block.text().isEmpty() && block.textList())
                    {
                        parent = block.position();
                        break;
                    }
            const auto state = tools.inspectDocument(wrapper, parent);
            const auto edited = tools.applyEdit(wrapper, parent, parent, "listIndent");
            if (!edited.value("valid").toBool())
                std::cerr << "mixed move original=" << original.toStdString()
                          << " error=" << edited.value("error").toString().toStdString() << '\n';
            passed =
                check(parent >= 0 && state.value("canIndentList").toBool() && edited.value("valid").toBool(),
                    "actual parent indent accepts all owned mixed leaves and an empty head") &&
                passed;
            const auto moved = tools.sourceText(wrapper);
            if (block_shape(moved) != block_shape(expected))
            {
                std::cerr << "mixed move expected=" << expected.toStdString()
                          << " saved=" << moved.toStdString() << '\n';
            }
            passed = check(tools.canSave() && block_shape(moved) == block_shape(expected),
                         "mixed leaves move under the preceding sibling without changing literals") &&
                passed;
            document->undo();
            passed =
                check(tools.sourceText(wrapper) == original, "one undo restores the entire mixed subtree") &&
                passed;
            document->redo();
            passed =
                check(tools.sourceText(wrapper) == moved, "mixed subtree redo preserves every owned leaf") &&
                passed;
            tools.loadDocument(wrapper, moved, true, theme);
            if (wrapper->textDocument()->toRawText() != raw)
                std::cerr << "reopen raw old=" << raw.toStdString()
                          << " new=" << wrapper->textDocument()->toRawText().toStdString() << '\n';
            const auto outdent = tools.applyEdit(wrapper, parent, parent, "listOutdent");
            if (!outdent.value("valid").toBool() ||
                block_shape(tools.sourceText(wrapper)) != block_shape(original))
                std::cerr << "outdent " << outdent.value("error").toString().toStdString()
                          << " result=" << tools.sourceText(wrapper).toStdString() << " parent=" << parent
                          << '\n';
            passed = check(wrapper->textDocument()->toRawText() == raw && outdent.value("valid").toBool() &&
                             block_shape(tools.sourceText(wrapper)) == block_shape(original),
                         "mixed subtree outdent after actual save-reopen restores ownership") &&
                passed;
            const auto source_start = original.indexOf(body.startsWith("- parent") ? "parent" : "-\n");
            const auto source_edit = mirrorfly::make_markdown_edit(
                original.toStdString(), source_start, source_start, "listIndent");
            auto source_saved = original.toStdString();
            if (source_edit.valid)
                source_saved.replace(
                    source_edit.start, source_edit.end - source_edit.start, source_edit.replacement);
            passed = check(source_edit.valid &&
                             block_shape(QString::fromStdString(source_saved)) == block_shape(expected),
                         "public source list transaction agrees with mixed subtree ownership") &&
                passed;
            cases.append(QJsonObject{{"original", original}, {"source", moved}, {"expected", expected},
                {"restored", tools.sourceText(wrapper)},
                {"sourceEdit", QString::fromStdString(source_saved)}});
        }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("block-moves.json"));
        const auto bytes = QJsonDocument(cases).toJson(QJsonDocument::Compact);
        passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                     "export twelve actual mixed subtree moves for independent verification") &&
            passed;
    }
    return passed;
}
