#include "markdown_code_span_tests.hpp"
#include "editor_tools.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>
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
bool test_markdown_code_spans(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    const QStringList cases{"`a\nb` after\n", "> `a\r\n> b` after\r\n", "- `a\n  b` after\n- sibling\n",
        "> - `a\n>   b` after\n> - sibling\n", "- > `a\n  > b` after\n- sibling\n",
        "[`a\nb`](../target \"title\") after\n", "**`a\nb`** after\n", "~~`a\nb`~~ after\n",
        "`` ` tick\nmore ` `` after\n", "` a  b ` after\n", "`    lead` after\n", "before `tail    `\n",
        "`    `\n", "`\t leading\t` after\n", "`  ` after\n", "`a\tb` after\n",
        "`<b>\n![x](remote) [^n]` after\n", "`a &amp;\n\\*b*` after\n", "## `a  b` after\n",
        "| `a\\|b` |\n| --- |\n| after |\n", "- owner\n\n  `a\n  b` after\n\n- sibling\n",
        QString::fromUtf8(u8"🦋前 **[`甲\r\n乙`](../目标 \"t\")** after\n")};
    bool passed = true;
    QJsonArray corpus;
    for (const auto& source : cases)
    {
        const auto bytes = source.toUtf8().toStdString();
        const auto spans = mirrorfly::markdown_code_spans(bytes);
        if (!check(spans.size() == 1, "public parser reports exactly one rendered code span"))
            return false;
        const auto& span = spans[0];
        passed = check(span.start < span.end && span.end <= bytes.size() && bytes[span.start] == '`' &&
                         bytes[span.end - 1] == '`',
                     "code metadata bounds include complete delimiters") &&
            passed;
        const auto loaded = tools.loadDocument(wrapper, source, true, theme);
        if (!check(loaded.value("valid").toBool(),
                "soft multiline code and apparent HTML import as literal text"))
        {
            std::cerr << source.toStdString() << loaded.value("error").toString().toStdString() << '\n';
            return false;
        }
        auto* document = wrapper->textDocument();
        const auto raw = document->toRawText();
        const auto text = QString::fromStdString(span.text);
        const int position = raw.indexOf(text);
        if (!check(position >= 0 && tools.inspectDocument(wrapper, position).value("inInlineCode").toBool(),
                "visual code context exposes normalized code text at the caret"))
        {
            std::cerr << source.toStdString() << " expected=" << span.text << " raw=" << raw.toStdString()
                      << '\n';
            return false;
        }
        const auto removed = tools.applyEdit(wrapper, position, position, "removeInlineCode");
        passed = check(removed.value("valid").toBool(),
                     "caret removes the existing span without inserting placeholder") &&
            passed;
        const auto plain = tools.sourceText(wrapper);
        passed =
            check(tools.canSave() && mirrorfly::markdown_code_spans(plain.toUtf8().toStdString()).empty(),
                "removing code retains literal text without reparsing as markup") &&
            passed;
        document->undo();
        passed = check(document->toRawText() == raw && tools.sourceText(wrapper) == source,
                     "one undo restores original literal code and source") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == plain, "code removal redo retains source") && passed;
        passed = check(tools.loadDocument(wrapper, plain, true, theme).value("valid").toBool() &&
                         wrapper->textDocument()->toRawText() == raw,
                     "plain literal saves and reopens with identical characters") &&
            passed;
        passed = check(tools.applyEdit(wrapper, position, position + text.size(), "inlineCode")
                           .value("valid")
                           .toBool(),
                     "reapplying code retains surrounding inline and parent styles") &&
            passed;
        const auto restored = tools.sourceText(wrapper);
        const auto edit = mirrorfly::make_markdown_edit(bytes, span.start, span.end, "removeInlineCode");
        if (!check(edit.valid, "source span removal is exposed through the core transaction"))
            return false;
        auto source_plain = bytes;
        source_plain.replace(edit.start, edit.end - edit.start, edit.replacement);
        corpus.append(QJsonObject{{"original", source}, {"plain", plain}, {"restored", restored},
            {"sourcePlain", QString::fromStdString(source_plain)}, {"code", text}});
    }
    tools.loadDocument(wrapper, "**before**  \nafter\n", true, theme);
    auto* document = wrapper->textDocument();
    const auto initial = document->toRawText();
    passed = check(tools.applyEdit(wrapper, 0, initial.size(), "inlineCode").value("valid").toBool(),
                 "paragraph-internal visual newline converts to the CommonMark code-space literal") &&
        passed;
    const auto normalized = tools.sourceText(wrapper);
    passed = check(tools.canSave() && !document->toRawText().contains(QChar::LineSeparator) &&
                     document->toRawText() == "before after",
                 "visual code normalization keeps text and removes hard-break state") &&
        passed;
    document->undo();
    passed =
        check(document->toRawText() == initial, "code normalization and formatting undo together") && passed;
    corpus.append(QJsonObject{{"breakSource", normalized}});
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("code-spans.json"));
        passed = check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(corpus).toJson()) > 0,
                     "export actual code-span operations for independent verification") &&
            passed;
    }
    return passed;
}
