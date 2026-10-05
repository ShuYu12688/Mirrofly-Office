#include "markdown_import_tests.hpp"
#include "editor_tools.hpp"
#include "markdown_document.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocument>
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
}

bool test_markdown_import_boundaries(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    const QStringList accepted{"```html\n<b>literal</b>\n[^literal]: x\n```",
        "> ```html\n> <b>literal</b>\n> [^literal]: x\n> ```",
        "- ```html\n  <b>literal</b>\n  [^literal]: x\n  ```",
        "> - ```html\n>   <b>literal</b>\n>   [^literal]: x\n>   ```",
        "- > ```html\n  > <b>literal</b>\n  > [^literal]: x\n  > ```",
        "    <b>literal</b>\n    [^literal]: x", "\\<b>literal\\</b> and \\[^literal]",
        "`<b>literal</b> [^literal]: x`", "[link](../x \"title <b>\")",
        "A literal [^undefined] has no definition.", "ordinary " + QString("|").repeated(40),
        "| col |\n| --- |\n| " + QString("\\|").repeated(40) + " |"};
    QJsonArray corpus;
    for (const auto& body : accepted)
    {
        const auto source = body + "\n\ntail\n";
        if (!check(!mirrorfly::markdown_has_raw_html(source.toUtf8().toStdString()),
                "public raw HTML classification excludes code, escaped text and attributes"))
            return false;
        const auto loaded = tools.loadDocument(wrapper, source, true, theme);
        if (!check(loaded.value("valid").toBool(), "valid literal Markdown remains visually editable"))
        {
            std::cerr << source.toStdString() << loaded.value("error").toString().toStdString();
            return false;
        }
        const int position = wrapper->textDocument()->toRawText().lastIndexOf("tail");
        if (!check(position >= 0 &&
                    tools.applyEdit(wrapper, position, position + 4, "italic").value("valid").toBool(),
                "normal parent-facing transaction edits following text"))
            return false;
        const auto saved = tools.sourceText(wrapper);
        if (!check(tools.canSave(), "literal code and pipe text save after a visual edit"))
            return false;
        const auto raw = wrapper->textDocument()->toRawText();
        wrapper->textDocument()->undo();
        if (!check(tools.sourceText(wrapper) == source, "literal import edit undo retains original source"))
            return false;
        wrapper->textDocument()->redo();
        if (!check(tools.loadDocument(wrapper, saved, true, theme).value("valid").toBool() &&
                    wrapper->textDocument()->toRawText() == raw,
                "literal Markdown save-reopen retains exact visible text"))
            return false;
        corpus.append(
            QJsonObject{{"initial", source}, {"source", saved}, {"expected", body + "\n\n*tail*\n"}});
    }
    const QStringList rejected{"text <b>HTML</b>", "![<b>x</b>](x.png)", "<!-- comment -->",
        "<!DOCTYPE html>", "> <div>HTML</div>", "```html\n<b>literal</b>\n```\n\n<b>real</b>",
        "[^n]: definition\n\ntext[^n]", "> [^n]: definition\n> text[^n]", "- [^n]: definition\n  text[^n]",
        "- - [^n]: definition\n    text[^n]", "---\nname: value\n---\nbody"};
    for (const auto& source : rejected)
        if (!check(!tools.loadDocument(wrapper, source, true, theme).value("valid").toBool() &&
                    tools.sourceText(wrapper) == source && !wrapper->textDocument()->isUndoAvailable(),
                "actual unsupported markup preserves the original source without editable replacement"))
            return false;
    const auto table = "|" + QString(" a |").repeated(33) + "\n|" + QString(" --- |").repeated(33) + "\n";
    if (!check(!mirrorfly::markdown_support_error(table).isEmpty(), "actual oversized table still rejects"))
        return false;
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("literal-imports.json"));
        const auto bytes = QJsonDocument(corpus).toJson(QJsonDocument::Compact);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return false;
    }
    return true;
}
