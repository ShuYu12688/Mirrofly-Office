#include "markdown_ordered_tests.hpp"
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
    bool same_markers(const QString& original, const QString& saved)
    {
        const auto first = mirrorfly::markdown_paragraphs(original.toUtf8().toStdString());
        const auto second = mirrorfly::markdown_paragraphs(saved.toUtf8().toStdString());
        if (first.size() != second.size())
            return false;
        for (std::size_t index = 0; index < first.size(); ++index)
        {
            if (first[index].containers.size() != second[index].containers.size())
                return false;
            for (std::size_t node = 0; node < first[index].containers.size(); ++node)
            {
                const auto& a = first[index].containers[node];
                const auto& b = second[index].containers[node];
                if (a.kind != b.kind || a.identity != b.identity || a.ordered != b.ordered ||
                    a.ordinal != b.ordinal || a.delimiter != b.delimiter || a.tight != b.tight)
                    return false;
            }
        }
        return true;
    }
}
bool test_markdown_ordered_lists(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    bool passed = true;
    QJsonArray cases;
    for (const QString& original : {QStringLiteral("3) first\n4) second\n"),
             QStringLiteral("> 100) first\n>\n>      continued\n>\n> 101) second\n"),
             QStringLiteral("8) first\n9) second\n10) third\n"),
             QStringLiteral("- parent\n\n  2) first\n  3) second\n"), QStringLiteral("+ first\n+ second\n"),
             QStringLiteral("* first\n* second\n"),
             QStringLiteral("1) first\n2) second\n\n1. third\n2. fourth\n"),
             QStringLiteral("0) first\n1) second\n"), QStringLiteral("999999999) first\n999999999) second\n"),
             QStringLiteral("1) first\n7) second\n3) third\n"),
             QStringLiteral("- parent\n\n  - first\n  - second\n"), QStringLiteral("3) first\n\n4) second\n"),
             QStringLiteral("- first\n+ second\n* third\n"),
             QStringLiteral("999999999) first\n999999999) second\n\n           continued\n"),
             QStringLiteral("003) first\n004) second\n")})
    {
        passed = check(tools.loadDocument(wrapper, original, true, theme).value("valid").toBool(),
                     "public visual import accepts CommonMark marker and starting-number variants") &&
            passed;
        auto* document = wrapper->textDocument();
        const auto raw = document->toRawText();
        const auto metadata = mirrorfly::markdown_paragraphs(original.toUtf8().toStdString());
        for (const auto& paragraph : metadata)
        {
            if (paragraph.containers.empty() || !paragraph.containers.back().ordered)
                continue;
            QString text;
            for (const auto& run : paragraph.runs)
                text += QString::fromStdString(run.style.text);
            const auto block = document->findBlock(raw.indexOf(text));
            if (!block.textList())
                continue;
            const auto& item = paragraph.containers.back();
            const auto expected = QString::number(item.ordinal) + QChar::fromLatin1(item.delimiter);
            passed = check(block.textList()->itemText(block) == expected,
                         "actual visible list markers retain imported semantic numbers and delimiters") &&
                passed;
        }
        passed = check(tools.applyEdit(wrapper, 0, 1, "italic").value("valid").toBool(),
                     "a real character edit leaves all ordered-list marker semantics intact") &&
            passed;
        const auto saved = tools.sourceText(wrapper);
        const bool compatible = tools.canSave() && same_markers(original, saved);
        if (!compatible)
            std::cerr << "ordered original=" << original.toStdString() << "saved=" << saved.toStdString();
        passed = check(compatible, "source save retains list starts, delimiter boundaries and ownership") &&
            passed;
        document->undo();
        passed =
            check(tools.sourceText(wrapper) == original, "marker edit undo restores exact source") && passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == saved, "marker edit redo restores serialized source") &&
            passed;
        tools.loadDocument(wrapper, saved, true, theme);
        passed = check(wrapper->textDocument()->toRawText() == raw &&
                         same_markers(original, tools.sourceText(wrapper)),
                     "ordered marker save-reopen preserves visible text and public semantics") &&
            passed;
        cases.append(QJsonObject{{"original", original}, {"source", saved}, {"style", true}});
    }
    const QString entered_original = "98) first\n99) tail\n";
    tools.loadDocument(wrapper, entered_original, true, theme);
    QTextCursor cursor(wrapper->textDocument());
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.beginEditBlock();
    cursor.insertBlock();
    cursor.insertText("new");
    cursor.endEditBlock();
    const auto entered = tools.sourceText(wrapper);
    const QString entered_expected = "98) first\n99) new\n100) tail\n";
    passed = check(tools.canSave() && same_markers(entered_expected, entered),
                 "native Enter keeps the parenthesized delimiter across a digit-width change") &&
        passed;
    wrapper->textDocument()->undo();
    passed = check(tools.sourceText(wrapper) == entered_original,
                 "native ordered Enter undoes as one transaction") &&
        passed;
    wrapper->textDocument()->redo();
    passed =
        check(tools.sourceText(wrapper) == entered, "native ordered Enter redo retains marker semantics") &&
        passed;
    cases.append(QJsonObject{{"original", entered_expected}, {"source", entered}, {"style", false}});
    for (const QString& outer : {QStringLiteral("1."), QStringLiteral("1)")})
    {
        const auto original = outer + " outer\n\n   7) child\n   8) next\n";
        tools.loadDocument(wrapper, original, true, theme);
        auto* document = wrapper->textDocument();
        const auto raw = document->toRawText();
        passed = check(tools.applyEdit(wrapper, raw.indexOf("child"), raw.indexOf("next") + 4, "listOutdent")
                           .value("valid")
                           .toBool(),
                     "public outdent moves ordered siblings through the normal parent transaction") &&
            passed;
        const auto moved = tools.sourceText(wrapper);
        const QString expected =
            outer == "1." ? "1. outer\n\n7) child\n8) next\n" : "1) outer\n2) child\n3) next\n";
        const bool compatible = tools.canSave() && same_markers(expected, moved);
        if (!compatible)
            std::cerr << "ordered outdent expected=" << expected.toStdString()
                      << "saved=" << moved.toStdString();
        passed = check(compatible,
                     "outdent merges only compatible ordered delimiters and keeps sequence starts") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == original,
                     "ordered sibling outdent restores original source on undo") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == moved,
                     "ordered sibling outdent redo preserves delimiter boundaries") &&
            passed;
        tools.loadDocument(wrapper, moved, true, theme);
        passed = check(wrapper->textDocument()->toRawText() == raw &&
                         same_markers(expected, tools.sourceText(wrapper)),
                     "outdented ordered lists retain visible text and semantics on actual reopen") &&
            passed;
        cases.append(QJsonObject{{"original", expected}, {"source", moved}, {"style", false}});
    }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("ordered-markers.json"));
        const auto bytes = QJsonDocument(cases).toJson(QJsonDocument::Compact);
        passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                     "export actual ordered-list saves for independent CommonMark verification") &&
            passed;
    }
    return passed;
}
