#include "markdown_heading_tests.hpp"
#include "editor_tools.hpp"

#include <mirrorfly/markdown.hpp>

#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <iostream>

namespace
{
    bool check(bool value, const char* message)
    {
        if (!value)
            std::cerr << "FAIL: " << message << '\n';
        return value;
    }

    struct HeadingCase
    {
        QString before;
        QString prefix;
        QString body;
        QString after;
        QString initial;
    };
}

bool test_markdown_empty_headings(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    struct Case
    {
        QString before;
        QString prefix;
        QString heading;
        QString after;
    };
    const std::vector<Case> cases{{"", "", "#", ""}, {"", "", "##   ", "\n"}, {"", "", "###   ###", "\n"},
        {"#\n", "", "##", "\n###\ntext\n"}, {"", "> ", "##", "\n> body\n"}, {"", "- ", "##", "\n- tail\n"},
        {"", "> - ", "##", "\n> - tail\n"}, {"", "- > ", "##", "\n- tail\n"},
        {"- owner\n\n", "  ", "##", "\n\n- tail\n"}, {"- [x] owner\n\n", "  > ", "##", "\n\n- tail\n"}};
    QJsonArray corpus;
    for (const auto& fixture : cases)
        for (int level = 1; level <= 6; ++level)
        {
            const auto initial = fixture.before + fixture.prefix + fixture.heading + fixture.after;
            const auto expected = fixture.before + fixture.prefix + QString(level, '#') + ' ' + fixture.after;
            const auto plain_expected = fixture.before + fixture.prefix + fixture.after;
            const auto parsed = mirrorfly::markdown_paragraphs(initial.toUtf8().toStdString());
            const auto offset = (fixture.before + fixture.prefix).toUtf8().size();
            const auto found = std::find_if(parsed.begin(), parsed.end(), [&](const auto& paragraph)
            {
                return paragraph.heading && paragraph.start >= static_cast<std::size_t>(offset);
            });
            if (!check(found != parsed.end(), "empty heading is represented by public paragraph metadata"))
                return false;
            const auto index = static_cast<int>(found - parsed.begin());
            if (!check(tools.loadDocument(wrapper, initial, true, theme).value("valid").toBool(),
                    "empty heading source imports"))
                return false;
            auto* document = wrapper->textDocument();
            auto block = document->begin();
            for (int i = 0; i < index; ++i)
                block = block.next();
            if (!check(block.isValid() && block.text().isEmpty() &&
                        block.blockFormat().headingLevel() == found->heading_level,
                    "empty and consecutive headings retain distinct visual blocks"))
                return false;
            const int position = block.position();
            const auto original = document->toRawText();
            QTextCursor typing(block);
            typing.insertText("typed");
            const auto typed =
                mirrorfly::markdown_paragraphs(tools.sourceText(wrapper).toUtf8().toStdString());
            if (!check(tools.canSave() && typed.size() == parsed.size() &&
                        typed[static_cast<std::size_t>(index)].runs.size() == 1 &&
                        typed[static_cast<std::size_t>(index)].runs[0].style.text == "typed" &&
                        !typed[static_cast<std::size_t>(index)].runs[0].style.bold,
                    "typing into an imported empty heading preserves implicit rather than explicit bold"))
                return false;
            document->undo();

            tools.applyEdit(wrapper, position, position, "heading", {{"headingLevel", level}});
            const auto source = tools.sourceText(wrapper);
            if (!check(tools.canSave(), "empty heading style is serializable"))
                return false;
            document->undo();
            if (!check(tools.sourceText(wrapper) == initial, "empty heading undo restores original source"))
                return false;
            document->redo();
            tools.loadDocument(wrapper, tools.sourceText(wrapper), true, theme);
            document = wrapper->textDocument();
            if (!check(document->toRawText() == original &&
                        document->findBlock(position).blockFormat().headingLevel() == level,
                    "empty heading survives changed save and reopen"))
            {
                std::cerr << "EMPTY initial=" << initial.toStdString() << " saved=" << source.toStdString();
                return false;
            }
            tools.applyEdit(wrapper, position, position, "paragraph");
            const auto plain = tools.sourceText(wrapper);
            if (!check(tools.canSave() && document->findBlock(position).blockFormat().headingLevel() == 0,
                    "empty heading resets to editable body"))
                return false;
            const auto bytes = initial.toUtf8().toStdString();
            mirrorfly::MarkdownOptions options;
            options.heading_level = level;
            const auto edit =
                mirrorfly::make_markdown_edit(bytes, found->start, found->start, "heading", options);
            const auto reset = mirrorfly::make_markdown_edit(bytes, found->start, found->start, "paragraph");
            if (!check(edit.valid && reset.valid, "public empty-heading level/reset transactions are valid"))
            {
                std::cerr << "EMPTY source=" << bytes << " level=" << edit.valid << " reset=" << reset.valid;
                return false;
            }
            auto changed = bytes, restored = bytes;
            changed.replace(edit.start, edit.end - edit.start, edit.replacement);
            restored.replace(reset.start, reset.end - reset.start, reset.replacement);
            corpus.append(QJsonObject{{"initial", initial}, {"expected", expected}, {"source", source},
                {"plainExpected", plain_expected}, {"plain", plain},
                {"sourceEdit", QString::fromUtf8(changed)}, {"sourcePlain", QString::fromUtf8(restored)}});
        }
    QJsonArray entered;
    for (const auto& fixture : cases)
    {
        for (const bool empty : {false, true})
        {
            const auto initial =
                fixture.before + fixture.prefix + "## " + (empty ? "" : "head") + fixture.after;
            if (!tools.loadDocument(wrapper, initial, true, theme).value("valid").toBool())
                return false;
            auto* document = wrapper->textDocument();
            auto block = document->begin();
            while (block.isValid() &&
                (block.blockFormat().headingLevel() != 2 ||
                    (empty ? !block.text().isEmpty() : block.text() != "head")))
                block = block.next();
            if (!block.isValid())
                return false;
            const int end = block.position() + block.length() - 1;
            const auto result = tools.applyEdit(wrapper, end, end, "enter");
            if (!check(
                    result.value("handled").toBool(), "heading end/empty Enter is one handled transaction"))
                return false;
            QTextCursor cursor(document);
            cursor.setPosition(result.value("selectionEnd").toInt());
            if (!check(cursor.blockFormat().headingLevel() == 0 &&
                        cursor.charFormat().fontWeight() < QFont::DemiBold,
                    "heading Enter switches to body block and insertion font"))
                return false;
            cursor.insertText("typed");
            const auto saved = tools.sourceText(wrapper);
            if (!check(tools.canSave(), "heading Enter followed by native typing saves"))
                return false;
            cursor.document()->undo();
            cursor.document()->undo();
            if (!check(tools.sourceText(wrapper) == initial,
                    "Enter and following typing have separate undo steps"))
                return false;
            QString continuation = fixture.prefix;
            const bool head = continuation.contains("- ");
            continuation.replace("- ", "  ");
            const auto separator = head ? "\n" : "\n" + continuation + "\n";
            auto after = fixture.after;
            if (after.startsWith("\n> body"))
                after.prepend("\n>");
            const auto expected = fixture.before +
                (empty ? fixture.prefix : fixture.prefix + "## head" + separator + continuation) + "typed" +
                after;
            entered.append(QJsonObject{{"initial", initial}, {"expected", expected}, {"source", saved}});
        }
    }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        for (const auto& pair : {qMakePair(QString("empty-headings.json"), corpus),
                 qMakePair(QString("heading-enter.json"), entered)})
        {
            QFile file(QDir(output).filePath(pair.first));
            const auto bytes = QJsonDocument(pair.second).toJson(QJsonDocument::Compact);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
                return false;
        }
    }
    return true;
}

bool test_markdown_headings(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    if (!test_markdown_empty_headings(tools, wrapper, theme))
        return false;
    const auto body = QString::fromUtf8(u8"**bold** *italic* ~~strike~~ [link](../x \"t\") `code` 🦋");
    const QString literal_code = "\n\n  ```cpp\n  x\n  ```\n\n- tail\n";
    const std::vector<HeadingCase> cases{{"", "", body, "\n", body + '\n'},
        {"", "> ", body, "\n", "> " + body + '\n'},
        {"", "- ", body, "\n- tail\n", "- " + body + "\n- tail\n"},
        {"", "> - ", body, "\n> - tail\n", "> - " + body + "\n> - tail\n"},
        {"", "- > ", body, "\n- tail\n", "- > " + body + "\n- tail\n"},
        {"- owner\n\n", "  ", body, "\n\n- tail\n", "- owner\n\n  " + body + "\n\n- tail\n"},
        {"- [x] task\n\n", "  ", body, "\n\n- [ ] tail\n", "- [x] task\n\n  " + body + "\n\n- [ ] tail\n"},
        {"", "", body, "\n", body + "\n===\n"}, {"", "> ", body, "\n", "> " + body + "\n> ---\n"},
        {"", "- ", body, "\n- tail\n", "- " + body + "\n  ===\n- tail\n"},
        {"", "", body, "\n", "## " + body + " ##\n"},
        {"", "", body, "\n", "**bold** *italic*\n~~strike~~ [link](../x \"t\") `code` 🦋\n===\n"},
        {"", "> ", body, "\n", "> **bold** *italic*\n> ~~strike~~ [link](../x \"t\") `code` 🦋\n> ---\n"},
        {"", "- ", body, literal_code, "- " + body + literal_code}};
    bool passed = true;
    QJsonArray corpus;
    for (const auto& fixture : cases)
        for (int level = 1; level <= 6; ++level)
        {
            const auto expected =
                fixture.before + fixture.prefix + QString(level, '#') + ' ' + fixture.body + fixture.after;
            const auto plain_expected = fixture.before + fixture.prefix + fixture.body + fixture.after;
            passed = check(tools.loadDocument(wrapper, fixture.initial, true, theme).value("valid").toBool(),
                         "all heading/container/Setext fixtures import") &&
                passed;
            auto* document = wrapper->textDocument();
            const auto original = document->toRawText();
            const int position = original.indexOf("bold");
            if (!check(position >= 0, "heading fixture target is present"))
                return false;
            passed = check(tools.applyEdit(wrapper, position, position, "heading", {{"headingLevel", level}})
                               .value("valid")
                               .toBool(),
                         "six heading levels use actual visual public transactions") &&
                passed;
            const auto source = tools.sourceText(wrapper);
            passed =
                check(tools.canSave() &&
                        tools.inspectDocument(wrapper, position).value("headingLevel").toInt() == level &&
                        document->findBlock(position).blockFormat().headingLevel() == level,
                    "heading context and live outline retain the requested level") &&
                passed;
            document->undo();
            passed = check(tools.sourceText(wrapper) == fixture.initial && document->toRawText() == original,
                         "one undo restores the whole heading transaction and original source") &&
                passed;
            document->redo();
            tools.loadDocument(wrapper, tools.sourceText(wrapper), true, theme);
            passed = check(wrapper->textDocument()->toRawText() == original,
                         "heading save-reopen keeps literal Unicode, code and links") &&
                passed;
            const int reopened_position = wrapper->textDocument()->toRawText().indexOf("bold");
            passed = check(tools.applyEdit(wrapper, reopened_position, reopened_position, "paragraph")
                               .value("valid")
                               .toBool(),
                         "paragraph restores only heading style in the same parent container") &&
                passed;
            const auto plain = tools.sourceText(wrapper);
            passed =
                check(tools.canSave() &&
                        tools.inspectDocument(wrapper, reopened_position).value("headingLevel").toInt() == 0,
                    "paragraph has no remaining implicit heading style") &&
                passed;
            mirrorfly::MarkdownOptions options;
            options.heading_level = level;
            const auto bytes = fixture.initial.toUtf8().toStdString();
            const auto caret = bytes.find("bold");
            const auto edit = mirrorfly::make_markdown_edit(bytes, caret, caret, "heading", options);
            auto source_edit = bytes;
            if (edit.valid)
                source_edit.replace(edit.start, edit.end - edit.start, edit.replacement);
            passed =
                check(edit.valid, "source heading transaction handles full parsed Setext/container range") &&
                passed;
            std::size_t reset_position = source_edit.size();
            for (const auto& item : mirrorfly::markdown_paragraphs(source_edit))
                if (item.heading_level == level)
                    reset_position = item.start;
            const auto reset =
                mirrorfly::make_markdown_edit(source_edit, reset_position, reset_position, "paragraph");
            auto restored = source_edit;
            if (reset.valid)
                restored.replace(reset.start, reset.end - reset.start, reset.replacement);
            passed =
                check(reset.valid, "public source paragraph removes heading without removing its owners") &&
                passed;
            corpus.append(
                QJsonObject{{"original", fixture.initial}, {"expected", expected}, {"source", source},
                    {"sourceEdit", QString::fromUtf8(source_edit)}, {"plainExpected", plain_expected},
                    {"plain", plain}, {"sourcePlain", QString::fromUtf8(restored)}, {"level", level}});
            if (!passed)
            {
                std::cerr << "heading case level=" << level << " original=" << bytes
                          << " visual=" << source.toStdString() << " source=" << source_edit << '\n';
                return false;
            }
        }
    tools.loadDocument(wrapper, "a\\\nb\n", true, theme);
    passed =
        check(!tools.inspectDocument(wrapper, 0).value("canStyleHeading").toBool() &&
                !tools.applyEdit(wrapper, 0, 0, "heading", {{"headingLevel", 2}}).value("valid").toBool() &&
                tools.sourceText(wrapper) == "a\\\nb\n",
            "hard-break paragraphs reject heading conversion atomically") &&
        passed;
    for (const QVariant& value : {QVariant(true), QVariant("2"), QVariant(2.5), QVariant(0), QVariant(7)})
    {
        tools.loadDocument(wrapper, "text\n", true, theme);
        passed = check(!tools.applyEdit(wrapper, 0, 0, "heading", {{"headingLevel", value}})
                             .value("valid")
                             .toBool() &&
                         !wrapper->textDocument()->isUndoAvailable(),
                     "invalid heading levels do not create partial edits or undo entries") &&
            passed;
    }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    tools.loadDocument(wrapper, "- [x] task\n", true, theme);
    passed =
        check(!tools.inspectDocument(wrapper, 0).value("canStyleHeading").toBool() &&
                !tools.applyEdit(wrapper, 0, 0, "heading", {{"headingLevel", 2}}).value("valid").toBool() &&
                tools.sourceText(wrapper) == "- [x] task\n" && !wrapper->textDocument()->isUndoAvailable() &&
                !mirrorfly::make_markdown_edit("- [x] task\n", 6, 6, "heading").valid,
            "a task checkbox head cannot be falsely serialized as an ATX heading") &&
        passed;
    tools.loadDocument(wrapper, "---\n", true, theme);
    const auto separator = tools.sourceText(wrapper);
    int separator_position = -1;
    for (auto block = wrapper->textDocument()->begin(); block.isValid(); block = block.next())
        if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
            separator_position = block.position();
    passed = check(separator_position >= 0 &&
                     !tools.inspectDocument(wrapper, separator_position).value("canStyleHeading").toBool() &&
                     !tools
                         .applyEdit(wrapper, separator_position, separator_position, "heading",
                             {{"headingLevel", 2}})
                         .value("valid")
                         .toBool() &&
                     !tools.applyEdit(wrapper, separator_position, separator_position, "paragraph")
                         .value("valid")
                         .toBool() &&
                     tools.sourceText(wrapper) == separator && !wrapper->textDocument()->isUndoAvailable(),
                 "heading and paragraph cannot mutate a separator into an ambiguous block") &&
        passed;
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("heading-levels.json"));
        const auto bytes = QJsonDocument(corpus).toJson(QJsonDocument::Compact);
        passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                     "export all 84 heading/container visual and source transactions") &&
            passed;
    }
    return passed;
}
