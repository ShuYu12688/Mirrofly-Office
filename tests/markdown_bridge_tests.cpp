#include "editor_tools.hpp"
#include "markdown_document.hpp"
#include "support/markdown_block_container_tests.hpp"
#include "support/markdown_code_span_tests.hpp"
#include "support/markdown_container_tests.hpp"
#include "support/markdown_heading_tests.hpp"
#include "support/markdown_image_tests.hpp"
#include "support/markdown_import_tests.hpp"
#include "support/markdown_link_styles_tests.hpp"
#include "support/markdown_ordered_tests.hpp"
#include "support/markdown_paragraph_styles_tests.hpp"
#include "support/markdown_whitespace_tests.hpp"
#include <mirrorfly/markdown.hpp>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

#include <iostream>
#include <memory>

namespace
{

    QQuickItem* find_visual_item(QQuickItem* item, const QString& name)
    {
        if (!item)
            return nullptr;
        if (item->objectName() == name)
            return item;
        for (auto* child : item->childItems())
            if (auto* found = find_visual_item(child, name))
                return found;
        return nullptr;
    }

    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    QVariantMap test_theme()
    {
        return {{QStringLiteral("fontFamily"), QStringLiteral("Segoe UI")},
            {QStringLiteral("editorFontFamily"), QStringLiteral("Consolas")},
            {QStringLiteral("editorFontSize"), 16}, {QStringLiteral("accentSoft"), QStringLiteral("#EEE7E1")},
            {QStringLiteral("borderColor"), QStringLiteral("#D3CCC7")},
            {QStringLiteral("textPrimary"), QStringLiteral("#292521")},
            {QStringLiteral("accent"), QStringLiteral("#95664A")}};
    }

    QVariantMap full_test_theme()
    {
        QFile file(QDir(QStringLiteral(MIRRORFLY_TEST_SOURCE_DIRECTORY))
                .filePath(QStringLiteral("config/theme.json")));
        if (!file.open(QIODevice::ReadOnly))
        {
            return test_theme();
        }
        return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    }

    bool valid(const QVariantMap& value)
    {
        return value.value(QStringLiteral("valid")).toBool();
    }

    bool test_links(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        const QString source = QString::fromUtf8(u8"前 [**你好🦋** and ") + QChar(0x60) + "a[b]" +
            QChar(0x60) + "](../old \"old title\") 后\n";
        tools.loadDocument(wrapper, source, true, test_theme());
        auto* document = wrapper->textDocument();
        const auto raw = document->toRawText();
        const int position = raw.indexOf("a[b]");
        const QString url = QString::fromUtf8(u8"../路径 (草稿)/x?one=1&two=2");
        const QString title = QString::fromUtf8(u8"查看 \"草稿\" & 说明");
        bool passed =
            check(position >= 0 && tools.inspectDocument(wrapper, position).value("inLink").toBool() &&
                    tools.inspectDocument(wrapper, position).value("linkTitle") == "old title",
                "imported styled links including inline code expose address and tooltip metadata");
        const auto updated =
            tools.applyEdit(wrapper, position, position, "link", {{"url", url}, {"title", title}});
        const auto saved = tools.sourceText(wrapper);
        passed = check(valid(updated) && document->toRawText() == raw &&
                         tools.inspectDocument(wrapper, position).value("linkUrl") == url &&
                         tools.inspectDocument(wrapper, position).value("linkTitle") == title &&
                         tools.canSave() && saved.contains("**") && !saved.contains("MIRRORFLYLINK"),
                     "editing any link fragment expands the link and serializes styled labels safely") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == source && !document->isUndoAvailable(),
                     "one undo restores the exact original link source") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == saved, "one redo restores the entire link transaction") &&
            passed;
        tools.loadDocument(wrapper, saved, true, test_theme());
        document = wrapper->textDocument();
        passed = check(document->toRawText() == raw &&
                         tools.inspectDocument(wrapper, position).value("linkUrl") == url &&
                         tools.inspectDocument(wrapper, position).value("linkTitle") == title,
                     "escaped targets, titles and formatted text survive an independent Markdown reimport") &&
            passed;
        const int emphasized = raw.indexOf(QString::fromUtf8(u8"你好"));
        passed = check(valid(tools.applyEdit(wrapper, emphasized, emphasized + 4, "italic")) &&
                         tools.sourceText(wrapper).contains("**"),
                     "another visual transaction keeps link serialization valid") &&
            passed;
        const auto stable = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, stable, true, test_theme());
        document = wrapper->textDocument();
        passed = check(valid(tools.applyEdit(
                           wrapper, position, position, "link", {{"url", url}, {"title", title}})) &&
                         tools.sourceText(wrapper) == stable,
                     "saving imported styled links again does not drift or leak serialization markers") &&
            passed;
        passed =
            check(valid(tools.applyEdit(wrapper, position, position + 1, "unlink")) &&
                    document->toRawText() == raw &&
                    !tools.inspectDocument(wrapper, position).value("inLink").toBool() &&
                    tools.sourceText(wrapper).contains("**"),
                "unlinking one code fragment removes the whole anchor while retaining emphasis and code") &&
            passed;
        tools.loadDocument(wrapper, "plain\n\nsecond\n", true, test_theme());
        document = wrapper->textDocument();
        const auto before = tools.sourceText(wrapper);
        for (const auto& options : {QVariantMap{{"url", 1}}, QVariantMap{{"url", ""}},
                 QVariantMap{{"url", "a\nb"}}, QVariantMap{{"url", "x"}, {"title", true}}})
            passed = check(!valid(tools.applyEdit(wrapper, 0, 3, "link", options)) &&
                             tools.sourceText(wrapper) == before,
                         "invalid visual link parameters preserve source and undo state") &&
                passed;
        passed = check(!valid(tools.applyEdit(wrapper, 0, 8, "link", {{"url", "#a"}})) &&
                         !document->isUndoAvailable(),
                     "cross-paragraph link creation is rejected without an undo transaction") &&
            passed;
        tools.loadDocument(wrapper, "| A | B |\n| --- | --- |\n| label | body |\n", true, test_theme());
        document = wrapper->textDocument();
        const int cell = document->toRawText().indexOf("label");
        const QString table_url = "../a|b(x)";
        passed = check(valid(tools.applyEdit(
                           wrapper, cell, cell + 5, "link", {{"url", table_url}, {"title", "a|b"}})),
                     "visual links can be inserted into existing table cells") &&
            passed;
        const auto table_source = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, table_source, true, test_theme());
        document = wrapper->textDocument();
        const int imported_cell = document->toRawText().indexOf("label");
        passed = check(imported_cell >= 0 &&
                         tools.inspectDocument(wrapper, imported_cell).value("linkUrl") == table_url &&
                         tools.inspectDocument(wrapper, imported_cell).value("linkTitle") == "a|b" &&
                         tools.inspectDocument(wrapper, imported_cell).value("tableColumns").toInt() == 2,
                     "table link pipes do not become column separators after save and reimport") &&
            passed;
        const QString joined = QString(78, 'x') + "[label](../a)tail\n";
        tools.loadDocument(wrapper, joined, true, test_theme());
        document = wrapper->textDocument();
        const auto joined_raw = document->toRawText();
        passed = check(valid(tools.applyEdit(wrapper, 79, 79, "link",
                           {{"url", QString(2048, 'x')}, {"title", QString(256, 't')}})),
                     "long link targets are edited independently of label width") &&
            passed;
        const auto joined_saved = tools.sourceText(wrapper);
        passed =
            check(joined_saved.contains(QString(78, 'x') + "[label]") && joined_saved.contains(")tail"),
                "serialization keeps original adjacency in CommonMark source, not only in the Qt reader") &&
            passed;
        const QString image_source = "[![alt](pic.png \"image title\")](../page \"link title\")\n";
        const auto image_load = tools.loadDocument(wrapper, image_source, true, test_theme());
        passed = check(valid(image_load) && tools.sourceText(wrapper) == image_source,
                     "linked image documents preserve their original source before editing") &&
            passed;
        tools.loadDocument(wrapper, joined_saved, true, test_theme());
        passed = check(wrapper->textDocument()->toRawText() == joined_raw,
                     "long links adjacent to text do not gain whitespace through native line wrapping") &&
            passed;
        const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
        if (!output.isEmpty())
        {
            const QDir directory(output);
            for (const auto& fixture : {std::pair<QString, QString>{"styled.md", saved},
                     {"table.md", table_source}, {"adjacent.md", joined_saved}})
            {
                QFile file(directory.filePath(fixture.first));
                const auto bytes = fixture.second.toUtf8();
                passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                             "export actual serialized link fixtures for an independent parser") &&
                    passed;
            }
        }
        return passed;
    }

    bool test_resolved_links(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        const QString reference = QString::fromUtf8(
            u8"[**你好🦋**][r] [R][]\n\n[r]: ../路径?x=&copy;&#65; \"&ouml; &NotEqualTilde;\"\n");
        bool passed = check(valid(tools.loadDocument(wrapper, reference, true, test_theme())),
            "resolved references load into the visual editor through public core metadata");
        auto* document = wrapper->textDocument();
        const auto raw = document->toRawText();
        const auto position = raw.indexOf(QString::fromUtf8(u8"你好"));
        passed =
            check(position >= 0 && tools.sourceText(wrapper) == reference &&
                    tools.inspectDocument(wrapper, position).value("linkUrl") ==
                        QString::fromUtf8(u8"../路径?x=©A") &&
                    tools.inspectDocument(wrapper, position).value("linkTitle") == QString::fromUtf8(u8"ö ≂̸"),
                "source baseline remains exact while reference targets and full entities display "
                "correctly") &&
            passed;
        passed = check(valid(tools.applyEdit(wrapper, position, position, "link",
                           {{"url", "../changed"}, {"title", "updated"}})),
                     "the visual reference label updates through the existing whole-link transaction") &&
            passed;
        const auto reference_saved = tools.sourceText(wrapper);
        document->undo();
        passed = check(tools.sourceText(wrapper) == reference,
                     "reference undo restores shared definitions exactly") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == reference_saved,
                     "reference redo restores the serialized edit") &&
            passed;
        tools.loadDocument(wrapper, reference_saved, true, test_theme());
        document = wrapper->textDocument();
        const auto second = document->toRawText().indexOf('R');
        passed = check(document->toRawText() == raw && second >= 0 &&
                         tools.inspectDocument(wrapper, position).value("linkUrl") == "../changed" &&
                         tools.inspectDocument(wrapper, second).value("linkUrl") ==
                             QString::fromUtf8(u8"../路径?x=©A"),
                     "visual save-reopen preserves other reference targets and all label text") &&
            passed;
        const QString multiline = "> [first\n> **second**][r]\n\n[r]: ../x\n";
        tools.loadDocument(wrapper, multiline, true, test_theme());
        document = wrapper->textDocument();
        const auto multiline_raw = document->toRawText();
        const int multi_position = multiline_raw.indexOf("second");
        passed = check(multi_position >= 0 &&
                         tools.inspectDocument(wrapper, multi_position).value("inLink").toBool() &&
                         valid(tools.applyEdit(
                             wrapper, multi_position, multi_position, "link", {{"url", "../multi"}})),
                     "soft multiline reference labels remain one visual link in their quote") &&
            passed;
        const auto multiline_saved = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, multiline_saved, true, test_theme());
        passed = check(wrapper->textDocument()->toRawText() == multiline_raw &&
                         tools.inspectDocument(wrapper, multi_position).value("quoteLevel").toInt() == 1 &&
                         tools.inspectDocument(wrapper, multi_position).value("linkUrl") == "../multi",
                     "multiline link text, emphasis and quote ownership survive visual save-reopen") &&
            passed;
        const QString automatic =
            "<https://example.com/a> <a@example.com> https://example.com/x www.example.com\n";
        tools.loadDocument(wrapper, automatic, true, test_theme());
        document = wrapper->textDocument();
        const auto auto_raw = document->toRawText();
        const int email = auto_raw.indexOf("a@example.com");
        passed = check(email >= 0 &&
                         tools.inspectDocument(wrapper, email).value("linkUrl") == "mailto:a@example.com" &&
                         valid(tools.applyEdit(wrapper, email, email, "unlink")),
                     "automatic email links can be removed with their literal text retained") &&
            passed;
        const auto automatic_saved = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, automatic_saved, true, test_theme());
        passed = check(wrapper->textDocument()->toRawText() == auto_raw &&
                         !tools.inspectDocument(wrapper, email).value("inLink").toBool() &&
                         tools.inspectDocument(wrapper, 0).value("inLink").toBool(),
                     "saved unlinked automatic text does not recreate an anchor or unlink its neighbor") &&
            passed;
        const QString complex = "[**bold *nested* rest** ~~strike~~](../x \"tip\") tail\n";
        tools.loadDocument(wrapper, complex, true, test_theme());
        document = wrapper->textDocument();
        const auto complex_raw = document->toRawText();
        passed = check(valid(tools.applyEdit(wrapper, 0, 0, "link", {{"url", "../complex"}})),
                     "mixed nested emphasis link labels update without losing their display text") &&
            passed;
        const auto complex_saved = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, complex_saved, true, test_theme());
        document = wrapper->textDocument();
        QTextCursor nested(document);
        nested.setPosition(document->toRawText().indexOf("nested"));
        nested.setPosition(nested.position() + 6, QTextCursor::KeepAnchor);
        passed = check(document->toRawText() == complex_raw && nested.charFormat().fontItalic() &&
                         nested.charFormat().fontWeight() >= QFont::Bold,
                     "nested bold/italic link spans retain character styles through serialization") &&
            passed;
        tools.loadDocument(wrapper, "[left   right](../x)\n", true, test_theme());
        document = wrapper->textDocument();
        const auto white_raw = document->toRawText();
        const int spaces = white_raw.indexOf(' ');
        const int white_end = white_raw.indexOf("right");
        passed = check(spaces >= 0 && valid(tools.applyEdit(wrapper, spaces, white_end, "bold")),
                     "prepare a whitespace-only styled fragment inside an anchor") &&
            passed;
        const auto whitespace_saved = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, whitespace_saved, true, test_theme());
        passed =
            check(wrapper->textDocument()->toRawText() == white_raw && !whitespace_saved.contains("****"),
                "whitespace-only styled link fragments never emit empty emphasis markers") &&
            passed;
        tools.loadDocument(wrapper, "a\\@example\\.com\tbody\n", true, test_theme());
        document = wrapper->textDocument();
        const auto tab_raw = document->toRawText();
        const auto body = tab_raw.indexOf("body");
        passed = check(body >= 0 && valid(tools.applyEdit(wrapper, body, body + 4, "bold")),
                     "format text next to a literal escaped email and tab") &&
            passed;
        const auto tab_saved = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, tab_saved, true, test_theme());
        passed = check(wrapper->textDocument()->toRawText() == tab_raw &&
                         !tools.inspectDocument(wrapper, 0).value("inLink").toBool(),
                     "literal automatic-looking text and tabs remain intact when another style is saved") &&
            passed;
        for (const auto& guarded : {QStringLiteral("[caption](../x \"first\nsecond\")\n")})
            passed = check(!valid(tools.loadDocument(wrapper, guarded, true, test_theme())) &&
                             tools.sourceText(wrapper) == guarded &&
                             !valid(tools.applyEdit(wrapper, 0, 0, "bold")),
                         "unsupported multiline-title links retain source and reject visual "
                         "rewriting") &&
                passed;
        const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
        if (!output.isEmpty())
            for (const auto& fixture : {std::pair<QString, QString>{"reference.md", reference_saved},
                     {"multiline.md", multiline_saved}, {"automatic.md", automatic_saved},
                     {"complex.md", complex_saved}, {"whitespace.md", whitespace_saved},
                     {"tab.md", tab_saved}})
            {
                QFile file(QDir(output).filePath(fixture.first));
                const auto bytes = fixture.second.toUtf8();
                passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                             "export resolved link fixtures for the independent parser") &&
                    passed;
            }
        return passed;
    }

    bool test_thematic_breaks(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        bool passed = true;
        QJsonArray fixtures;
        for (const auto& source :
            {QStringLiteral(""), QStringLiteral("before\n"), QStringLiteral("> before\n> after\n"),
                QStringLiteral("# heading\n"), QStringLiteral("title\n=====\n"),
                QStringLiteral("- parent\n  - child\n- sibling\n\nplain\n"),
                QStringLiteral("- before\n\n1. after\n"), QStringLiteral("> before\n>\n> after\n"),
                QStringLiteral("100. before\n101. after\n"), QStringLiteral("[label](../a)\n"),
                QString::fromUtf8(u8"🦋前后\n")})
        {
            passed = check(valid(tools.loadDocument(wrapper, source, true, test_theme())),
                         "separator fixture loads as editable Markdown") &&
                passed;
            auto* document = wrapper->textDocument();
            const auto original = document->toRawText();
            passed = check(valid(tools.applyEdit(wrapper, 0, 0, "thematicBreak")),
                         "separator uses a reversible block transaction") &&
                passed;
            const auto saved = tools.sourceText(wrapper);
            passed =
                check(tools.canSave() && mirrorfly::markdown_thematic_break_count(saved.toStdString()) == 1,
                    "separator saves as one actual thematic-break block") &&
                passed;
            document->undo();
            passed = check(tools.sourceText(wrapper) == source && document->toRawText() == original,
                         "one undo restores exact separator source and text") &&
                passed;
            document->redo();
            passed = check(tools.sourceText(wrapper) == saved, "separator redo restores source") && passed;
            tools.loadDocument(wrapper, saved, true, test_theme());
            int rules = 0;
            for (auto block = wrapper->textDocument()->begin(); block.isValid(); block = block.next())
                if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
                    ++rules;
            passed =
                check(rules == 1 && tools.canSave(), "separator save-reopen retains its block semantics") &&
                passed;
            for (const auto& word :
                {"before", "after", "heading", "title", "parent", "child", "sibling", "plain", "label"})
                if (source.contains(word))
                    passed = check(wrapper->textDocument()->toRawText().contains(word),
                                 "separator preserves original captions and list items") &&
                        passed;
            if (source.contains("child"))
                passed = check(saved.indexOf("sibling") < saved.indexOf("- - -") &&
                                 saved.indexOf("plain") > saved.indexOf("- - -"),
                             "separator follows the complete list and preserves following text") &&
                    passed;
            fixtures.append(QJsonObject{{"source", saved}, {"original", source}});
        }
        for (const auto& source :
            {QStringLiteral("```\nbody\n```\n"), QStringLiteral("| A |\n| --- |\n| body |\n")})
        {
            tools.loadDocument(wrapper, source, true, test_theme());
            const int position = wrapper->textDocument()->toRawText().indexOf("body");
            passed =
                check(!valid(tools.applyEdit(wrapper, position, position, "thematicBreak")) &&
                        tools.sourceText(wrapper) == source && !wrapper->textDocument()->isUndoAvailable(),
                    "invalid separator regions reject without an undo transaction") &&
                passed;
        }
        const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
        if (!output.isEmpty())
        {
            QFile file(QDir(output).filePath("thematic-breaks.json"));
            const auto bytes = QJsonDocument(fixtures).toJson(QJsonDocument::Compact);
            passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                         "export actual separator saves for independent verification") &&
                passed;
        }
        return passed;
    }

    bool test_hard_breaks(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        bool passed = true;
        QJsonArray fixtures;
        for (const auto& source : {QStringLiteral("before  \nafter\n"), QStringLiteral("before\\\nafter\n"),
                 QStringLiteral("> **before**  \n> *after*\n"), QStringLiteral("- [x] before  \n  after\n"),
                 QStringLiteral("100. before  \n     after\n"), QStringLiteral("- > before  \n  > after\n"),
                 QStringLiteral("> - before  \n>   after\n"),
                 QStringLiteral("[**before**  \n*after*](../same)\n"),
                 QStringLiteral("> - [before\\\n>   after][r]\n\n[r]: ../same\n")})
        {
            passed = check(valid(tools.loadDocument(wrapper, source, true, test_theme())),
                         "paragraph/list/quote/link hard breaks load as editable content") &&
                passed;
            auto* document = wrapper->textDocument();
            const auto raw = document->toRawText();
            const int before = raw.indexOf("before");
            const int after = raw.indexOf("after");
            const auto expected = QStringLiteral("before") + QChar::LineSeparator + "after";
            passed = check(before >= 0 && after >= 0 && raw.contains(expected) &&
                             document->findBlock(before) == document->findBlock(after),
                         "hard breaks remain inside one paragraph rather than splitting into two blocks") &&
                passed;
            const auto state = tools.inspectDocument(wrapper, before);
            passed = check(valid(tools.applyEdit(wrapper, before, before + 1, "bold")) && tools.canSave(),
                         "editing another character retains hard-break serialization") &&
                passed;
            const auto saved = tools.sourceText(wrapper);
            document->undo();
            passed = check(tools.sourceText(wrapper) == source,
                         "hard-break source and container prefixes restore exactly on undo") &&
                passed;
            document->redo();
            passed = check(tools.sourceText(wrapper) == saved,
                         "hard-break saving follows the same redo transaction") &&
                passed;
            tools.loadDocument(wrapper, saved, true, test_theme());
            document = wrapper->textDocument();
            passed =
                check(document->toRawText() == raw &&
                        tools.inspectDocument(wrapper, before).value("quoteLevel") ==
                            state.value("quoteLevel") &&
                        tools.inspectDocument(wrapper, before).value("inList") == state.value("inList") &&
                        mirrorfly::markdown_hard_breaks(saved.toUtf8().toStdString()).size() == 1 &&
                        !saved.contains("MIRRORFLY"),
                    "save-reopen retains one hard break, list/quote ownership and literal text") &&
                passed;
            passed =
                check(valid(tools.applyEdit(wrapper, before, before + 1, "bold")) &&
                        mirrorfly::markdown_hard_breaks(tools.sourceText(wrapper).toStdString()).size() ==
                            1 &&
                        wrapper->textDocument()->toRawText() == raw && tools.canSave(),
                    "reversing character formatting retains paragraph hard-break semantics") &&
                passed;
            fixtures.append(QJsonObject{{"source", saved}, {"text", "before\nafter"},
                {"links", source.contains("../same") ? 1 : 0}, {"breaks", 1}});
        }
        const QString source = "beforeafter\n";
        tools.loadDocument(wrapper, source, true, test_theme());
        auto* document = wrapper->textDocument();
        passed = check(valid(tools.applyEdit(wrapper, 6, 6, "hardBreak")) && tools.canSave() &&
                         document->toRawText() == QStringLiteral("before") + QChar::LineSeparator + "after",
                     "explicit hardBreak uses a public reversible paragraph-internal edit") &&
            passed;
        const auto saved = tools.sourceText(wrapper);
        document->undo();
        passed =
            check(tools.sourceText(wrapper) == source, "one undo removes an explicit hard break") && passed;
        document->redo();
        passed =
            check(tools.sourceText(wrapper) == saved, "one redo restores an explicit hard break") && passed;
        tools.loadDocument(wrapper, "[beforeafter](../same)\n", true, test_theme());
        passed = check(valid(tools.applyEdit(wrapper, 6, 6, "hardBreak")) && tools.canSave(),
                     "a hard break can be inserted inside an existing linked caption") &&
            passed;
        const auto linked = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, linked, true, test_theme());
        passed =
            check(valid(tools.applyEdit(wrapper, 9, 9, "link", {{"url", "../changed"}})) &&
                    tools.inspectDocument(wrapper, 1).value("linkUrl") == "../changed" && tools.canSave(),
                "whole-link editing expands across the paragraph-internal hard break") &&
            passed;
        fixtures.append(QJsonObject{
            {"source", tools.sourceText(wrapper)}, {"text", "before\nafter"}, {"links", 1}, {"breaks", 1}});
        for (const auto& guarded : {QStringLiteral("# title\n"), QStringLiteral("`code`\n"),
                 QStringLiteral("| A |\n| --- |\n| body |\n")})
        {
            tools.loadDocument(wrapper, guarded, true, test_theme());
            const int position =
                wrapper->textDocument()->toRawText().indexOf(guarded.startsWith('#') ? "title"
                        : guarded.startsWith('|')                                    ? "body"
                                                                                     : "code");
            passed =
                check(position >= 0 &&
                        !valid(tools.applyEdit(wrapper, position + 1, position + 1, "hardBreak")) &&
                        tools.sourceText(wrapper) == guarded && !wrapper->textDocument()->isUndoAvailable(),
                    "code, headings and table cells reject paragraph hard breaks without mutation") &&
                passed;
        }
        tools.loadDocument(wrapper, "plain\n", true, test_theme());
        passed =
            check(valid(tools.applyEdit(wrapper, 5, 5, "hardBreak")) &&
                    tools.sourceText(wrapper) == "plain\n" && !tools.canSave(),
                "an unfinished terminal hard break retains valid source until continuation text is typed") &&
            passed;
        document = wrapper->textDocument();
        QTextCursor continuation(document);
        continuation.movePosition(QTextCursor::End);
        continuation.insertText("after");
        const auto continued = tools.sourceText(wrapper);
        passed =
            check(tools.canSave() && mirrorfly::markdown_hard_breaks(continued.toStdString()).size() == 1,
                "typing continuation text makes a terminal editing break saveable") &&
            passed;
        for (const auto& tail :
            {QStringLiteral("# heading"), QStringLiteral("> quote"), QStringLiteral("- item"),
                QStringLiteral("1. item"), QStringLiteral("***"), QStringLiteral("~~~ language")})
        {
            tools.loadDocument(wrapper, "before" + tail + "\n", true, test_theme());
            const auto raw = wrapper->textDocument()->toRawText();
            passed =
                check(valid(tools.applyEdit(wrapper, 6, 6, "hardBreak")) && tools.canSave(),
                    "new visual hard-break lines escape block openers instead of changing the block type") &&
                passed;
            const auto tail_saved = tools.sourceText(wrapper);
            tools.loadDocument(wrapper, tail_saved, true, test_theme());
            passed =
                check(wrapper->textDocument()->toRawText() == raw.left(6) + QChar::LineSeparator + raw.mid(6),
                    "heading, quote, list and fence-looking text remains literal after hard-break "
                    "save-reopen") &&
                passed;
            fixtures.append(QJsonObject{
                {"source", tail_saved}, {"text", "before\n" + tail}, {"links", 0}, {"breaks", 1}});
        }
        for (const int position : {0, 6, 10})
        {
            tools.loadDocument(wrapper, "before`code`after\n", true, test_theme());
            const auto raw = wrapper->textDocument()->toRawText();
            passed = check(valid(tools.applyEdit(wrapper, position, position, "hardBreak")),
                         "leading and inline-code boundary breaks remain paragraph edits") &&
                passed;
            const auto boundary_saved = tools.sourceText(wrapper);
            passed = check(tools.canSave() &&
                             mirrorfly::markdown_hard_breaks(boundary_saved.toStdString()).size() == 1,
                         "boundary breaks serialize as one explicit Markdown break") &&
                passed;
            tools.loadDocument(wrapper, boundary_saved, true, test_theme());
            QTextCursor code_character(wrapper->textDocument());
            const int code_position = position <= 6 ? 7 : 6;
            code_character.setPosition(code_position);
            code_character.setPosition(code_position + 1, QTextCursor::KeepAnchor);
            passed = check(wrapper->textDocument()->toRawText() ==
                                 raw.left(position) + QChar::LineSeparator + raw.mid(position) &&
                             code_character.charFormat().fontFixedPitch(),
                         "boundary save-reopen preserves inline-code formatting and text") &&
                passed;
        }
        tools.loadDocument(wrapper, "[beforeafter](../same)\n", true, test_theme());
        passed = check(valid(tools.applyEdit(wrapper, 6, 6, "hardBreak")) &&
                         valid(tools.applyEdit(wrapper, 7, 7, "hardBreak")) && tools.canSave(),
                     "consecutive hard breaks inside a link remain one paragraph and one anchor") &&
            passed;
        const auto repeated = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, repeated, true, test_theme());
        passed =
            check(wrapper->textDocument()->toRawText() ==
                        QStringLiteral("before") + QString(QChar::LineSeparator).repeated(2) + "after" &&
                    tools.canSave(),
                "consecutive linked hard breaks survive save-reopen without empty paragraph splitting") &&
            passed;
        fixtures.append(
            QJsonObject{{"source", repeated}, {"text", "before\n\nafter"}, {"links", 1}, {"breaks", 2}});
        const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
        if (!output.isEmpty())
        {
            QFile file(QDir(output).filePath("hard-breaks.json"));
            const auto bytes = QJsonDocument(fixtures).toJson(QJsonDocument::Compact);
            passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                         "export actual hard-break saves for independent CommonMark/GFM verification") &&
                passed;
        }
        return passed;
    }

    bool test_load_unicode_undo(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        int notifications = 0;
        const auto connection = QObject::connect(&tools, &mirrorfly::EditorTools::documentEdited,
            [&notifications](QQuickTextDocument*)
        {
            ++notifications;
        });
        const QString source = QString::fromUtf8(u8"# 标题\n\n前\u00A0后\n\n🦋中\n");
        const auto loaded = tools.loadDocument(wrapper, source, true, test_theme());
        auto* document = wrapper->textDocument();
        bool passed = check(valid(loaded) && tools.sourceText(wrapper) == source && notifications == 0 &&
                !document->isUndoAvailable() && document->begin().blockFormat().headingLevel() == 1,
            "loading rendered Markdown retains the original source and starts with a clean undo stack");
        const int start = document->toRawText().indexOf(QString::fromUtf8(u8"🦋中"));
        const auto before = document->toRawText();
        const auto edit = tools.applyEdit(wrapper, start, start + 3, QStringLiteral("bold"));
        QTextCursor selection(document);
        selection.setPosition(start);
        selection.setPosition(start + 3, QTextCursor::KeepAnchor);
        const auto serialized = tools.sourceText(wrapper);
        QTextDocument restored;
        const auto restore_error = mirrorfly::load_markdown_document(restored, serialized, test_theme());
        QTextCursor restored_selection(&restored);
        restored_selection.setPosition(start);
        restored_selection.setPosition(start + 3, QTextCursor::KeepAnchor);
        passed = check(valid(edit) && document->toRawText() == before &&
                         selection.charFormat().fontWeight() >= QFont::Bold && notifications > 0 &&
                         restore_error.isEmpty() && restored.toRawText() == before &&
                         restored_selection.charFormat().fontWeight() >= QFont::Bold &&
                         serialized.contains(QChar(0x00A0)),
                     "format-only changes notify the caller and preserve NBSP elsewhere in the document") &&
            passed;
        const auto invalid = tools.applyEdit(wrapper, start + 1, start + 3, QStringLiteral("italic"));
        passed = check(!valid(invalid) && tools.sourceText(wrapper) == serialized,
                     "a UTF-16 selection cannot split a supplementary character") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == source && !document->isUndoAvailable(),
                     "one undo restores exact original Markdown, including whitespace and NBSP") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == serialized,
                     "one redo reapplies a visual formatting transaction") &&
            passed;
        document->undo();
        const int paragraph = document->toRawText().indexOf(QString::fromUtf8(u8"前"));
        passed = check(valid(tools.applyEdit(wrapper, paragraph, paragraph, QStringLiteral("heading"),
                           {{QStringLiteral("headingLevel"), 2}})) &&
                         tools.inspectDocument(wrapper, paragraph)
                                 .value(QStringLiteral("outline"))
                                 .toList()
                                 .size() == 2,
                     "heading-only format edits immediately update the public outline") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == source,
                     "heading transactions also restore original source in a single undo") &&
            passed;
        QObject::disconnect(connection);
        return passed;
    }

    bool test_code_frames(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        const QString source =
            QString::fromUtf8(u8"开头\n\n````cpp\nint x;\n```\n~~~\n尾\n````\n\n## 结束\n");
        const auto load_result = tools.loadDocument(wrapper, source, true, test_theme());
        bool passed = check(
            valid(load_result), "fenced code containing both marker styles loads into a visual document");
        auto* document = wrapper->textDocument();
        const int start = document->toRawText().indexOf(QStringLiteral("int x;"));
        if (!check(start >= 0, "the imported code body remains present"))
        {
            return false;
        }
        QTextCursor first(document);
        first.setPosition(start);
        QTextCursor last(document);
        last.setPosition(document->toRawText().indexOf(QString::fromUtf8(u8"尾")));
        passed = check(first.currentFrame() == last.currentFrame() &&
                         first.currentFrame() != document->rootFrame() &&
                         tools.inspectDocument(wrapper, start).value(QStringLiteral("inCode")).toBool(),
                     "all code lines share one contiguous native frame") &&
            passed;
        passed = check(valid(tools.applyEdit(wrapper, start, start, QStringLiteral("codeLanguage"),
                           {{QStringLiteral("language"), QStringLiteral("python\ninjected")}})),
                     "code language changes are a public visual transaction") &&
            passed;
        const auto changed = tools.sourceText(wrapper);
        passed = check(tools.canSave() && changed.contains(QStringLiteral("````python\n")) &&
                         changed.contains(QString::fromUtf8(u8"int x;\n```\n~~~\n尾")) &&
                         !changed.contains(QStringLiteral("injected")),
                     "export grows fences around embedded backticks and filters language newlines") &&
            passed;
        auto imported = mirrorfly::create_editor_document();
        mirrorfly::load_markdown_document(*imported, changed, test_theme());
        passed = check(imported->toRawText().contains(QStringLiteral("```")) &&
                         imported->toRawText().contains(QStringLiteral("~~~")),
                     "generated Markdown can be read back without truncating code content") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == source && !document->isUndoAvailable(),
                     "one undo restores the code language and exact original fences") &&
            passed;
        const auto exit = tools.applyEdit(wrapper, start, start, QStringLiteral("exitCode"));
        passed = check(valid(exit) &&
                         !tools.inspectDocument(wrapper, exit.value(QStringLiteral("selectionEnd")).toInt())
                             .value(QStringLiteral("inCode"))
                             .toBool(),
                     "exit code returns a cursor outside the frame") &&
            passed;
        const int following = document->toRawText().indexOf(QString::fromUtf8(u8"结束"));
        passed = check(document->findBlock(following).blockFormat().headingLevel() == 2,
                     "leaving a code frame preserves the existing heading that follows it") &&
            passed;
        return passed;
    }

    bool test_strike_roundtrip(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        const QString source = QString::fromUtf8(u8"保留 🦋过期 保留\n");
        tools.loadDocument(wrapper, source, true, test_theme());
        auto* document = wrapper->textDocument();
        const int start = document->toRawText().indexOf(QString::fromUtf8(u8"🦋过期"));
        bool passed = check(valid(tools.applyEdit(wrapper, start, start + 4, "strike")),
            "strikethrough is a published editor transaction");
        const auto saved = tools.sourceText(wrapper);
        QTextDocument reopened;
        mirrorfly::load_markdown_document(reopened, saved, test_theme());
        QTextCursor cursor(&reopened);
        const int imported = reopened.toRawText().indexOf(QString::fromUtf8(u8"🦋过期"));
        cursor.setPosition(imported);
        cursor.setPosition(imported + 4, QTextCursor::KeepAnchor);
        passed = check(saved.contains("~~") && reopened.toRawText() == document->toRawText() &&
                         cursor.charFormat().fontStrikeOut(),
                     "saved strikethrough reparses as a character style") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == source, "one undo restores exact unformatted original") &&
            passed;
        document->redo();
        passed = check(valid(tools.applyEdit(wrapper, start, start + 4, "strike")) &&
                         !tools.sourceText(wrapper).contains("~~"),
                     "repeating visual strikethrough removes the style") &&
            passed;
        return passed;
    }

    bool test_table_alignment(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        bool passed = true;
        for (const auto& source :
            {QString("| A | B | C | D |\n| --- | :--- | :---: | ---: |\n| a | b | c | d |\n"),
                QString("| A | B | C | D |\n| --- | :--- | :---: | ---: |\n"),
                QString("| | | | |\n| --- | :--- | :---: | ---: |\n| | | | |\n")})
        {
            passed = check(valid(tools.loadDocument(wrapper, source, true, test_theme())),
                         "load aligned, header-only and empty-cell tables") &&
                passed;
            auto* document = wrapper->textDocument();
            QTextTable* table = nullptr;
            for (auto* frame : document->rootFrame()->childFrames())
                if ((table = qobject_cast<QTextTable*>(frame)))
                    break;
            if (!table)
                return false;
            const QStringList expected{"default", "left", "center", "right"};
            const Qt::Alignment flags[] = {Qt::AlignLeft, Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
            for (int column = 0; column < table->columns(); ++column)
                for (int row = 0; row < table->rows(); ++row)
                {
                    const auto cell = table->cellAt(row, column).firstCursorPosition();
                    passed =
                        check(cell.blockFormat().alignment() == flags[column] &&
                                tools.inspectDocument(wrapper, cell.position()).value("tableAlignment") ==
                                    expected[column],
                            "header and body cells share imported column alignment") &&
                        passed;
                }
            const auto before = tools.sourceText(wrapper);
            const auto raw = document->toRawText();
            const int position = table->cellAt(0, 2).firstPosition();
            passed = check(!valid(tools.applyEdit(
                               wrapper, position, position, "tableAlign", {{"alignment", "justify"}})) &&
                             !valid(tools.applyEdit(wrapper, position, position, "tableAlign",
                                 {{"alignment", "right"}, {"column", 8}})) &&
                             tools.sourceText(wrapper) == before,
                         "invalid column edits preserve the entire document") &&
                passed;
            passed = check(valid(tools.applyEdit(
                               wrapper, position, position, "tableAlign", {{"alignment", "right"}})) &&
                             document->toRawText() == raw &&
                             tools.inspectDocument(wrapper, position).value("tableAlignment") == "right",
                         "current-column alignment changes formatting without changing cell text") &&
                passed;
            document->undo();
            passed = check(tools.inspectDocument(wrapper, position).value("tableAlignment") == "center",
                         "one undo restores the column metadata and formatting") &&
                passed;
            document->redo();
            const auto saved = tools.sourceText(wrapper);
            const auto metadata = mirrorfly::markdown_tables(saved.toUtf8().toStdString());
            passed =
                check(metadata.size() == 1 && metadata[0].rows == table->rows() &&
                        metadata[0].columns.size() == 4 && metadata[0].columns[0].alignment == "default" &&
                        metadata[0].columns[1].alignment == "left" &&
                        metadata[0].columns[2].alignment == "right" &&
                        metadata[0].columns[3].alignment == "right",
                    "serialization preserves default vs explicit left and header-only row counts") &&
                passed;
            QTextDocument reopened;
            mirrorfly::load_markdown_document(reopened, saved, test_theme());
            passed = check(reopened.toRawText() == raw &&
                             mirrorfly::serialize_markdown_document(reopened).source == saved,
                         "aligned and empty-cell tables retain text and stable source after reopen") &&
                passed;
            passed = check(valid(tools.applyEdit(wrapper, position, position, "rowAdd")),
                         "row insertion keeps table formatting editable") &&
                passed;
            const auto inserted = table->cellAt(1, 2).firstCursorPosition();
            passed = check(inserted.blockFormat().alignment() == Qt::AlignRight,
                         "new body rows inherit the column alignment") &&
                passed;
        }
        const QString multiple = "| A |\n| ---: |\n| one |\n\n| B |\n| :---: |\n| two |\n";
        tools.loadDocument(wrapper, multiple, true, test_theme());
        const auto multiple_saved = tools.sourceText(wrapper);
        const auto multiple_metadata = mirrorfly::markdown_tables(multiple_saved.toUtf8().toStdString());
        passed =
            check(multiple_metadata.size() == 2 && multiple_metadata[0].columns[0].alignment == "right" &&
                    multiple_metadata[1].columns[0].alignment == "center",
                "independent tables retain their own column alignment") &&
            passed;
        return passed;
    }

    bool test_inline_code_roundtrip(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        bool passed = true;
        for (const auto& body :
            {QString::fromUtf8(u8"🦋a`b``c"), QString("`x`"), QString(" x "), QString("   "),
                QString("a\\|b"), QString("a\\\\|b"), QString("<tag> **literal** [x] ![x]\\ |")})
        {
            // Load literal text into a Markdown editor without letting markers alter the chosen sample.
            tools.loadDocument(wrapper, QStringLiteral("before sample after\n"), true, test_theme());
            auto* document = wrapper->textDocument();
            QTextCursor literal(document);
            literal.setPosition(7);
            literal.setPosition(13, QTextCursor::KeepAnchor);
            literal.insertText(body);
            const auto raw = document->toRawText();
            const int end = 7 + static_cast<int>(body.size());
            const auto edit = tools.applyEdit(wrapper, 7, end, "inlineCode");
            const auto saved = tools.sourceText(wrapper);
            QTextDocument reopened;
            mirrorfly::load_markdown_document(reopened, saved, test_theme());
            QTextCursor code(&reopened);
            code.setPosition(7);
            code.setPosition(end, QTextCursor::KeepAnchor);
            passed = check(valid(edit) && document->toRawText() == raw && reopened.toRawText() == raw &&
                             code.charFormat().fontFixedPitch() &&
                             mirrorfly::markdown_support_error(saved).isEmpty(),
                         "visual inline code roundtrips literal markers, Unicode and spaces") &&
                passed;
            document->undo();
            QTextCursor restored(document);
            restored.setPosition(7);
            restored.setPosition(end, QTextCursor::KeepAnchor);
            passed =
                check(!restored.charFormat().fontFixedPitch(), "single undo removes inline style") && passed;
            document->redo();
            passed = check(valid(tools.applyEdit(wrapper, 7, end, "inlineCode")),
                         "second inline-code gesture removes the style") &&
                passed;
            restored.setPosition(7);
            restored.setPosition(end, QTextCursor::KeepAnchor);
            passed =
                check(!restored.charFormat().fontFixedPitch(), "inline-code removal restores body font") &&
                passed;
        }
        tools.loadDocument(wrapper, "first\n\nsecond\n", true, test_theme());
        const auto before = tools.sourceText(wrapper);
        passed = check(!valid(tools.applyEdit(wrapper, 0, 10, "inlineCode")) &&
                         tools.sourceText(wrapper) == before,
                     "cross-paragraph inline-code edits preserve the untouched draft") &&
            passed;
        const std::vector<QString> table_bodies = {QStringLiteral("a|b`c"), QStringLiteral("a\\|b"),
            QStringLiteral("a\\\\|b"), QStringLiteral("a\\\\\\|b|c"), QStringLiteral(" x|y ")};
        for (const auto& body : table_bodies)
        {
            tools.loadDocument(wrapper, "| Key | Value |\n| --- | --- |\n| x | y |\n", true, test_theme());
            auto* document = wrapper->textDocument();
            QTextTable* table = nullptr;
            for (auto* frame : document->rootFrame()->childFrames())
                if ((table = qobject_cast<QTextTable*>(frame)))
                    break;
            if (!table)
                return false;
            auto cell = table->cellAt(1, 0).firstCursorPosition();
            cell.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cell.insertText(body);
            const int start = table->cellAt(1, 0).firstCursorPosition().position();
            passed = check(valid(tools.applyEdit(
                               wrapper, start, start + static_cast<int>(body.size()), "inlineCode")),
                         "inline code stays within one table cell") &&
                passed;
            const auto saved = tools.sourceText(wrapper);
            QTextDocument reopened;
            mirrorfly::load_markdown_document(reopened, saved, test_theme());
            QTextTable* imported = nullptr;
            for (auto* frame : reopened.rootFrame()->childFrames())
                if ((imported = qobject_cast<QTextTable*>(frame)))
                    break;
            passed = check(imported && imported->columns() == 2 &&
                             imported->cellAt(1, 0).firstCursorPosition().block().text() == body,
                         "code cell roundtrips pipes, backticks, literal slashes and boundary spaces") &&
                passed;
            if (imported)
            {
                const auto second = mirrorfly::serialize_markdown_document(reopened);
                QTextDocument again;
                mirrorfly::load_markdown_document(again, second.source, test_theme());
                passed = check(second.valid && again.toRawText() == reopened.toRawText(),
                             "a second save/reopen does not consume literal code-cell slashes") &&
                    passed;
            }
        }
        return passed;
    }

    bool test_insert_code_signal_path(
        mirrorfly::EditorTools& tools, QObject* editor, QQuickTextDocument* wrapper)
    {
        bool callback_active = false;
        bool reentered = false;
        int notifications = 0;
        QString synchronized_source;
        QVariantMap synchronized_state;
        const auto connection = QObject::connect(&tools, &mirrorfly::EditorTools::documentEdited,
            [&tools, &callback_active, &reentered, &notifications, &synchronized_source, &synchronized_state](
                QQuickTextDocument* document)
        {
            reentered = reentered || callback_active;
            callback_active = true;
            ++notifications;
            synchronized_source = tools.sourceText(document);
            synchronized_state = tools.inspectDocument(document, 0);
            callback_active = false;
        });
        bool passed = check(valid(tools.loadDocument(wrapper, {}, true, test_theme())),
            "a plain paragraph is ready for the toolbar code action");
        const auto edit = tools.applyEdit(
            wrapper, 0, 0, QStringLiteral("code"), {{QStringLiteral("language"), QStringLiteral("cpp")}});
        passed = check(valid(edit) && notifications == 1 && !reentered &&
                         synchronized_source.contains(QStringLiteral("```cpp")) &&
                         !synchronized_source.contains(QString::fromUtf8(u8"代码内容")) &&
                         synchronized_state.value(QStringLiteral("canSave")).toBool(),
                     "toolbar code insertion completes through the real synchronous signal path") &&
            passed;
        const bool selection_applied = QMetaObject::invokeMethod(editor, "select",
            Q_ARG(int, edit.value(QStringLiteral("selectionStart")).toInt()),
            Q_ARG(int, edit.value(QStringLiteral("selectionEnd")).toInt()));
        QCoreApplication::processEvents();
        passed =
            check(selection_applied, "QML applies the returned code selection after the signal callback") &&
            passed;
        auto* document = wrapper->textDocument();
        QTextCursor code(document);
        code.setPosition(edit.value(QStringLiteral("selectionStart")).toInt());
        auto* frame = code.currentFrame();
        passed = check(frame != document->rootFrame() && frame->frameFormat().border() == 0,
                     "production code frames keep a continuous background without the unsafe ordinary-frame "
                     "border") &&
            passed;
        code.insertText(QStringLiteral("return 0;"));
        passed = check(tools.sourceText(wrapper).contains(QStringLiteral("return 0;")),
                     "an initially empty code frame accepts and serializes direct input") &&
            passed;
        const auto exited =
            tools.applyEdit(wrapper, code.position(), code.position(), QStringLiteral("exitCode"));
        passed = check(valid(exited) &&
                         !tools.inspectDocument(wrapper, exited.value(QStringLiteral("selectionEnd")).toInt())
                             .value(QStringLiteral("inCode"))
                             .toBool(),
                     "an edited empty code frame can be exited before saving") &&
            passed;
        document->undo();
        document->undo();
        document->undo();
        passed = check(tools.sourceText(wrapper).isEmpty() && !document->isUndoAvailable(),
                     "code exit, input, and insertion undo cleanly back to the empty source") &&
            passed;
        QObject::disconnect(connection);
        return passed;
    }

    bool test_code_insert_positions(
        mirrorfly::EditorTools& tools, QObject* editor, QQuickTextDocument* wrapper)
    {
        struct Case
        {
            QString source;
            QString first;
            QString last;
        };
        const std::vector<Case> cases = {
            {QStringLiteral("first line\nsecond line\n"), QStringLiteral("first"),
                QStringLiteral("second line")},
            {QStringLiteral("# Heading\n\nparagraph\n"), QStringLiteral("Heading"),
                QStringLiteral("Heading")},
            {QStringLiteral("- first\n- second\n"), QStringLiteral("first"), QStringLiteral("second")},
            {QStringLiteral("# Heading\n\nparagraph\n"), QStringLiteral("Heading"),
                QStringLiteral("paragraph")},
        };
        bool passed = true;
        for (const auto& current : cases)
        {
            passed = check(valid(tools.loadDocument(wrapper, current.source, true, test_theme())),
                         "load a structured selection before inserting code") &&
                passed;
            auto* document = wrapper->textDocument();
            const int start = document->toRawText().indexOf(current.first);
            const int end = document->toRawText().indexOf(current.last) + current.last.size();
            const auto edit = tools.applyEdit(wrapper, start, end, QStringLiteral("code"),
                {{QStringLiteral("language"), QStringLiteral("text")}});
            passed = check(valid(edit), "insert code at the selected structured content") && passed;
            if (valid(edit))
            {
                passed = check(QMetaObject::invokeMethod(editor, "select",
                                   Q_ARG(int, edit.value(QStringLiteral("selectionStart")).toInt()),
                                   Q_ARG(int, edit.value(QStringLiteral("selectionEnd")).toInt())),
                             "select the inserted structured code content") &&
                    passed;
                QCoreApplication::processEvents();
                passed = check(tools.canSave() && tools.sourceText(wrapper).contains(QStringLiteral("```")),
                             "serialize the inserted structured code content") &&
                    passed;
            }
        }
        return passed;
    }

    bool test_explicit_block_input(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        struct Case
        {
            QString input;
            int heading_level;
            QTextListFormat::Style list_style;
            QTextBlockFormat::MarkerType marker;
        };
        const std::vector<Case> cases = {
            {QString::fromUtf8(u8"# 标题"), 1, QTextListFormat::ListStyleUndefined,
                QTextBlockFormat::MarkerType::NoMarker},
            {QStringLiteral("> quoted"), 0, QTextListFormat::ListStyleUndefined,
                QTextBlockFormat::MarkerType::NoMarker},
            {QStringLiteral("> > quoted"), 0, QTextListFormat::ListStyleUndefined,
                QTextBlockFormat::MarkerType::NoMarker},
            {QStringLiteral("- bullet"), 0, QTextListFormat::ListDisc,
                QTextBlockFormat::MarkerType::NoMarker},
            {QStringLiteral("1. ordered"), 0, QTextListFormat::ListDecimal,
                QTextBlockFormat::MarkerType::NoMarker},
            {QStringLiteral("- [x] complete"), 0, QTextListFormat::ListDisc,
                QTextBlockFormat::MarkerType::Checked},
        };
        bool passed = true;
        for (const auto& current : cases)
        {
            passed = check(valid(tools.loadDocument(wrapper, {}, true, test_theme())),
                         "load an empty visual document before typing a block marker") &&
                passed;
            auto* document = wrapper->textDocument();
            QTextCursor typing(document);
            typing.insertText(current.input);
            int notifications = 0;
            QVariantMap published_state;
            const auto connection = QObject::connect(&tools, &mirrorfly::EditorTools::documentEdited,
                [&tools, &notifications, &published_state](QQuickTextDocument* edited)
            {
                ++notifications;
                tools.sourceText(edited);
                published_state = tools.inspectDocument(edited, 0);
            });
            const auto converted =
                tools.applyEdit(wrapper, typing.position(), typing.position(), QStringLiteral("enter"));
            QObject::disconnect(connection);
            const auto first = document->begin();
            passed = check(valid(converted) && converted.value(QStringLiteral("handled")).toBool() &&
                             notifications == 1,
                         "Enter converts a recognized Markdown block marker in one published transaction") &&
                passed;
            passed = check(first.blockFormat().headingLevel() == current.heading_level,
                         "the typed block receives the expected heading level") &&
                passed;
            if (current.input.startsWith(u'>'))
            {
                passed = check(first.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) ==
                                 current.input.count(u'>'),
                             "a typed quote marker becomes a visual quote") &&
                    passed;
            }
            if (current.list_style != QTextListFormat::ListStyleUndefined)
            {
                passed = check(first.textList() != nullptr &&
                                 first.textList()->format().style() == current.list_style &&
                                 first.blockFormat().marker() == current.marker,
                             "typed list and task markers preserve their explicit semantics") &&
                    passed;
            }
            if (current.heading_level > 0)
            {
                passed = check(published_state.value(QStringLiteral("outline")).toList().size() == 1,
                             "a typed heading updates the published outline before Enter returns") &&
                    passed;
            }
            document->undo();
            passed = check(document->toRawText() == current.input,
                         "one undo restores the typed marker before conversion") &&
                passed;
        }

        tools.loadDocument(wrapper, {}, true, test_theme());
        auto* document = wrapper->textDocument();
        QTextCursor formatted(document);
        formatted.insertText(QStringLiteral("# "));
        QTextCharFormat emphasis;
        emphasis.setFontWeight(QFont::Bold);
        formatted.insertText(QStringLiteral("formatted"), emphasis);
        const auto converted =
            tools.applyEdit(wrapper, formatted.position(), formatted.position(), QStringLiteral("enter"));
        QTextCursor preserved(document);
        preserved.setPosition(0);
        preserved.setPosition(QStringLiteral("formatted").size(), QTextCursor::KeepAnchor);
        passed = check(valid(converted) && preserved.charFormat().fontWeight() >= QFont::Bold,
                     "block marker conversion removes only the prefix and preserves inline formatting") &&
            passed;

        tools.loadDocument(wrapper, {}, true, test_theme());
        document = wrapper->textDocument();
        QTextCursor unsupported(document);
        unsupported.insertText(QStringLiteral("2. keep literal"));
        const auto ignored =
            tools.applyEdit(wrapper, unsupported.position(), unsupported.position(), QStringLiteral("enter"));
        passed = check(valid(ignored) && !ignored.value(QStringLiteral("handled")).toBool() &&
                         document->toRawText() == QStringLiteral("2. keep literal"),
                     "unsupported ordered starts remain literal for native Enter handling") &&
            passed;
        return passed;
    }

    std::unique_ptr<QObject> create_text_edit(QQmlEngine& engine)
    {
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.RichText }", QUrl{});
        return std::unique_ptr<QObject>(component.create());
    }

    bool test_document_lifetimes(QQmlEngine& engine)
    {
        bool passed = true;
        auto first_editor = create_text_edit(engine);
        auto second_editor = create_text_edit(engine);
        auto* first_wrapper = first_editor->property("textDocument").value<QQuickTextDocument*>();
        auto* second_wrapper = second_editor->property("textDocument").value<QQuickTextDocument*>();
        mirrorfly::EditorTools alternating_tools;
        for (int iteration = 0; iteration < 12; ++iteration)
        {
            auto* target = iteration % 2 == 0 ? first_wrapper : second_wrapper;
            QPointer<QTextDocument> replaced = target->textDocument();
            passed = check(valid(alternating_tools.loadDocument(
                               target, QString::number(iteration), true, test_theme())),
                         "alternate visual document loads between independent wrappers") &&
                passed;
            passed = check(replaced.isNull(), "a replaced target document is released immediately") && passed;
            const auto owned_documents =
                target->findChildren<QTextDocument*>(QString{}, Qt::FindDirectChildrenOnly);
            passed = check(owned_documents.size() == 1 && owned_documents.front() == target->textDocument(),
                         "each wrapper retains exactly one live custom document after reload") &&
                passed;
        }

        auto wrapper_first_editor = create_text_edit(engine);
        auto wrapper_first_tools = std::make_unique<mirrorfly::EditorTools>();
        auto* wrapper_first = wrapper_first_editor->property("textDocument").value<QQuickTextDocument*>();
        wrapper_first_tools->loadDocument(wrapper_first, QStringLiteral("alive"), true, test_theme());
        QPointer<QTextDocument> wrapper_first_document = wrapper_first->textDocument();
        QPointer<QQuickTextDocument> wrapper_guard = wrapper_first;
        wrapper_first_editor.reset();
        QCoreApplication::processEvents();
        passed = check(wrapper_guard.isNull() && wrapper_first_document.isNull(),
                     "destroying the QML wrapper first releases its parented document safely") &&
            passed;
        wrapper_first_tools.reset();

        auto tools_first_editor = create_text_edit(engine);
        auto* tools_first_wrapper = tools_first_editor->property("textDocument").value<QQuickTextDocument*>();
        auto tools_first = std::make_unique<mirrorfly::EditorTools>();
        tools_first->loadDocument(tools_first_wrapper, QStringLiteral("still editable"), true, test_theme());
        QPointer<QTextDocument> tools_first_document = tools_first_wrapper->textDocument();
        tools_first.reset();
        QTextCursor after_tools(tools_first_document);
        after_tools.movePosition(QTextCursor::End);
        after_tools.insertText(QStringLiteral("!"));
        passed = check(tools_first_document && tools_first_wrapper->textDocument() == tools_first_document &&
                         tools_first_document->toRawText().endsWith(u'!'),
                     "destroying EditorTools first leaves the wrapper-owned document usable") &&
            passed;
        tools_first_editor.reset();
        QCoreApplication::processEvents();
        passed = check(tools_first_document.isNull(),
                     "the remaining wrapper-owned document is released with its editor") &&
            passed;
        return passed;
    }

    bool test_text_editor_page_code_path(QQmlEngine& engine, mirrorfly::EditorTools& tools)
    {
        const QUrl ui_directory = QUrl::fromLocalFile(
            QDir(QStringLiteral(MIRRORFLY_TEST_SOURCE_DIRECTORY)).filePath(QStringLiteral("ui")) + u'/');
        const QString qml = QStringLiteral(R"(
import QtQuick
import "%1" as App
Item {
    width: 1120
    height: 720
    required property var tools
    required property var themeData
    property var currentDocument: null
    App.TextEditorPage
    {
        id: page
        objectName: "isolatedTextEditorPage"
        anchors.fill: parent
        theme: parent.themeData
        content: ""
        markdown: true
        onDocumentLoadRequested: function(document, source, markdown)
        {
            parent.currentDocument = document;
            page.finishDocumentLoad(tools.loadDocument(document, source, markdown, parent.themeData));
        }
        onDocumentStateRequested: function(document, position)
        {
            page.applyDocumentState(tools.inspectDocument(document, position));
        }
        onMarkdownActionRequested: function(action, document, start, end, options)
        {
            page.applyMarkdownEdit(tools.applyEdit(document, start, end, action, options));
        }
    }
    Connections
    {
        target: tools

        function onDocumentEdited(document)
        {
            tools.sourceText(document);
            tools.inspectDocument(document, 0);
            page.refreshDocumentState();
        }
    }
}
)")
                                .arg(ui_directory.toString());
        QQmlComponent component(&engine);
        component.setData(qml.toUtf8(), QUrl{});
        std::unique_ptr<QObject> root(
            component.createWithInitialProperties({{QStringLiteral("tools"), QVariant::fromValue(&tools)},
                {QStringLiteral("themeData"), full_test_theme()}}));
        if (!check(root != nullptr, "load the real TextEditorPage and MarkdownToolbar without a window"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        auto* page = root->findChild<QObject*>(QStringLiteral("isolatedTextEditorPage"));
        QObject* toolbar = nullptr;
        for (auto* child : root->findChildren<QObject*>())
        {
            if (QString::fromLatin1(child->metaObject()->className())
                    .contains(QStringLiteral("MarkdownToolbar")))
            {
                toolbar = child;
                break;
            }
        }
        bool passed = check(page != nullptr && toolbar != nullptr,
            "find the live page and toolbar objects in the isolated QML tree");
        const QVariant code_options = QVariantMap{{QStringLiteral("language"), QStringLiteral("cpp")}};
        const bool invoked = toolbar &&
            QMetaObject::invokeMethod(toolbar, "actionRequested", Q_ARG(QString, QStringLiteral("code")),
                Q_ARG(QVariant, code_options));
        QCoreApplication::processEvents();
        auto* wrapper = root->property("currentDocument").value<QQuickTextDocument*>();
        passed = check(invoked && wrapper != nullptr &&
                         tools.sourceText(wrapper).contains(QStringLiteral("```cpp")),
                     "the toolbar action completes through TextEditorPage and EditorTools") &&
            passed;
        auto* text_edit = wrapper ? wrapper->parent() : nullptr;
        const int code_position = text_edit ? text_edit->property("cursorPosition").toInt() : -1;
        QKeyEvent code_input(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
        if (text_edit)
        {
            QCoreApplication::sendEvent(text_edit, &code_input);
        }
        const auto code_block = wrapper ? wrapper->textDocument()->findBlock(code_position) : QTextBlock{};
        passed =
            check(text_edit != nullptr && code_block.isValid() && code_block.text() == QStringLiteral("x") &&
                    code_block.begin().fragment().charFormat().fontFixedPitch(),
                "the first real TextEdit keystroke in an empty code frame keeps code formatting") &&
            passed;

        if (wrapper)
        {
            tools.loadDocument(wrapper, {}, true, full_test_theme());
            QTextCursor typed(wrapper->textDocument());
            typed.insertText(QString::fromUtf8(u8"# 标题"));
            text_edit->setProperty("cursorPosition", typed.position());
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QCoreApplication::sendEvent(text_edit, &enter);
            const auto outline =
                page->property("documentState").toMap().value(QStringLiteral("outline")).toList();
            passed = check(wrapper->textDocument()->begin().blockFormat().headingLevel() == 1 &&
                             outline.size() == 1,
                         "a real unmodified Return converts typed heading syntax and updates the outline") &&
                passed;

            tools.loadDocument(wrapper, {}, true, full_test_theme());
            QTextCursor literal(wrapper->textDocument());
            literal.insertText(QStringLiteral("# keep literal"));
            text_edit->setProperty("cursorPosition", literal.position());
            QKeyEvent shifted_enter(
                QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier, QStringLiteral("\r"));
            QCoreApplication::sendEvent(text_edit, &shifted_enter);
            passed =
                check(wrapper->textDocument()->begin().blockFormat().headingLevel() == 0 &&
                        wrapper->textDocument()->toRawText().startsWith(QStringLiteral("# keep literal")),
                    "Shift+Enter keeps typed heading syntax literal for native handling") &&
                passed;

            tools.loadDocument(
                wrapper, QStringLiteral("| A |\n| --- |\n| value |\n"), true, full_test_theme());
            auto* table_document = wrapper->textDocument();
            QTextCursor table_cursor(table_document);
            table_cursor.setPosition(table_document->toRawText().indexOf(QStringLiteral("value")));
            auto* table = table_cursor.currentTable();
            if (table)
            {
                text_edit->setProperty("cursorPosition", table_cursor.position());
                page->setProperty("documentState", tools.inspectDocument(wrapper, table_cursor.position()));
                auto* alignment_control = root->findChild<QObject*>("markdownColumnAlignment");
                if (alignment_control)
                    alignment_control->setProperty("currentIndex", 3);
                const bool alignment_requested = alignment_control &&
                    QMetaObject::invokeMethod(alignment_control, "activated", Q_ARG(int, 3));
                passed =
                    check(alignment_requested &&
                            tools.inspectDocument(wrapper, table_cursor.position()).value("tableAlignment") ==
                                "right" &&
                            tools.sourceText(wrapper).contains("---:"),
                        "the actual column selector routes through toolbar and parent editing signals") &&
                    passed;
                table->appendRows(128 - table->rows());
                const auto last_cell = table->cellAt(table->rows() - 1, 0);
                text_edit->setProperty("cursorPosition", last_cell.firstPosition());
                const auto raw_before = table_document->toRawText();
                const int blocks_before = table_document->blockCount();
                QKeyEvent limited_enter(
                    QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                QCoreApplication::sendEvent(text_edit, &limited_enter);
                passed = check(limited_enter.isAccepted() && table->rows() == 128 &&
                                 table_document->blockCount() == blocks_before &&
                                 table_document->toRawText() == raw_before,
                             "a rejected Enter at the table row limit is consumed without native paragraph "
                             "insertion") &&
                    passed;
            }
            else
            {
                passed = check(false, "prepare a native table for the row-limit key regression") && passed;
            }
            tools.loadDocument(wrapper, "- parent\n- [ ] second\n- third\n", true, full_test_theme());
            auto* list_document = wrapper->textDocument();
            const int list_position = list_document->toRawText().indexOf("second");
            text_edit->setProperty("cursorPosition", list_position);
            page->setProperty("documentState", tools.inspectDocument(wrapper, list_position));
            auto* indent_button = root->findChild<QObject*>("markdownListIndent");
            const bool indent_requested =
                indent_button && QMetaObject::invokeMethod(indent_button, "clicked");
            passed = check(indent_requested &&
                             tools.inspectDocument(wrapper, list_position).value("listLevel").toInt() == 2,
                         "the actual list toolbar button routes through its parent to the public edit "
                         "interface") &&
                passed;
            page->setProperty("documentState", tools.inspectDocument(wrapper, list_position));
            auto* task_button = root->findChild<QObject*>("markdownTaskState");
            const bool task_requested = task_button && QMetaObject::invokeMethod(task_button, "clicked");
            passed = check(task_requested &&
                             tools.inspectDocument(wrapper, list_position).value("taskChecked").toBool(),
                         "the actual task button publishes explicit completion through the parent signal") &&
                passed;
            QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
            QCoreApplication::sendEvent(text_edit, &backtab);
            passed = check(backtab.isAccepted() &&
                             tools.inspectDocument(wrapper, list_position).value("listLevel").toInt() == 1,
                         "Shift+Tab uses the same parent list transaction as the toolbar") &&
                passed;
            tools.loadDocument(wrapper, "> parent\n> > child\n", true, full_test_theme());
            const int quote_position = wrapper->textDocument()->toRawText().indexOf("parent");
            text_edit->setProperty("cursorPosition", quote_position);
            page->setProperty("documentState", tools.inspectDocument(wrapper, quote_position));
            auto* quote_control = root->findChild<QObject*>("markdownQuoteLevel");
            if (quote_control)
                quote_control->setProperty("currentIndex", 2);
            const bool quote_requested =
                quote_control && QMetaObject::invokeMethod(quote_control, "activated", Q_ARG(int, 2));
            passed =
                check(quote_requested &&
                        tools.inspectDocument(wrapper, quote_position).value("quoteLevel").toInt() == 2 &&
                        tools.sourceText(wrapper).contains("> > > child"),
                    "the actual quote selector routes through toolbar and parent editing signals") &&
                passed;
            tools.loadDocument(wrapper, "label\n", true, full_test_theme());
            text_edit->setProperty("cursorPosition", 0);
            QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 5));
            page->setProperty("documentState", tools.inspectDocument(wrapper, 0));
            auto* address = root->findChild<QObject*>("markdownLinkAddress");
            auto* title = root->findChild<QObject*>("markdownLinkTitle");
            auto* apply_link = root->findChild<QObject*>("markdownLinkApply");
            if (address)
                address->setProperty("text", "../a (b)");
            if (title)
                title->setProperty("text", "link tooltip");
            const bool link_requested = apply_link && QMetaObject::invokeMethod(apply_link, "clicked");
            passed =
                check(link_requested && tools.inspectDocument(wrapper, 0).value("linkUrl") == "../a (b)" &&
                        tools.inspectDocument(wrapper, 0).value("linkTitle") == "link tooltip",
                    "actual link dialog fields and apply button route through parent editing signals") &&
                passed;
            tools.loadDocument(wrapper, "> - label\n", true, full_test_theme());
            text_edit->setProperty("cursorPosition", 0);
            QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 5));
            auto* image_address = root->findChild<QObject*>("markdownImageAddress");
            auto* image_alt = root->findChild<QObject*>("markdownImageAlt");
            auto* image_apply = root->findChild<QObject*>("markdownImageApply");
            if (image_address)
                image_address->setProperty("text", "../photo.png");
            if (image_alt)
                image_alt->setProperty("text", "photo caption");
            const bool image_requested = image_apply && QMetaObject::invokeMethod(image_apply, "clicked");
            const auto image_state = tools.inspectDocument(wrapper, 0);
            passed = check(image_requested && image_state.value("inImage").toBool() &&
                             image_state.value("imageUrl") == "../photo.png" &&
                             image_state.value("inList").toBool() &&
                             image_state.value("quoteLevel").toInt() == 1 && tools.canSave(),
                         "actual image dialog routes through parent transaction preserving quote/list "
                         "hierarchy") &&
                passed;
            tools.loadDocument(wrapper, "beforeafter\n", true, full_test_theme());
            text_edit->setProperty("cursorPosition", 6);
            page->setProperty("documentState", tools.inspectDocument(wrapper, 6));
            tools.loadDocument(wrapper, "> - **[`a\nb`](../x)** after\n> - tail\n", true, full_test_theme());
            const int inline_code_position = wrapper->textDocument()->toRawText().indexOf("a b");
            QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, inline_code_position + 1),
                Q_ARG(int, inline_code_position + 2));
            page->setProperty("documentState", tools.inspectDocument(wrapper, inline_code_position + 1));
            auto* code_button =
                find_visual_item(qobject_cast<QQuickItem*>(root.get()), "markdownInline-inlineCode");
            const bool code_requested = code_button &&
                code_button->property("text").toString() == QStringLiteral("取消行内代码") &&
                QMetaObject::invokeMethod(code_button, "clicked");
            const auto code_paragraphs =
                mirrorfly::markdown_paragraphs(tools.sourceText(wrapper).toUtf8().toStdString());
            passed =
                check(code_requested && tools.canSave() &&
                        mirrorfly::markdown_code_spans(tools.sourceText(wrapper).toUtf8().toStdString())
                            .empty() &&
                        code_paragraphs.size() == 2 && code_paragraphs[0].containers.size() == 2 &&
                        code_paragraphs[0].runs[0].style.bold && code_paragraphs[0].runs[0].url == "../x",
                    "actual code toolbar cancels the full span through parent while retaining outer "
                    "styles") &&
                passed;
            tools.loadDocument(wrapper, "`one` two\n", true, full_test_theme());
            QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 7));
            page->setProperty("documentState", tools.inspectDocument(wrapper, 0));
            const bool mixed_code_requested = code_button &&
                code_button->property("text").toString() == QStringLiteral("行内代码") &&
                QMetaObject::invokeMethod(code_button, "clicked");
            const auto mixed_code_spans =
                mirrorfly::markdown_code_spans(tools.sourceText(wrapper).toStdString());
            passed =
                check(mixed_code_requested && tools.canSave() && mixed_code_spans.size() == 1 &&
                        mixed_code_spans[0].text == "one two",
                    "mixed code/plain selection requests conversion instead of cancelling a partial span") &&
                passed;
            tools.loadDocument(wrapper, "# a**b**c\n", true, full_test_theme());
            QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 1));
            page->setProperty("documentState", tools.inspectDocument(wrapper, 0));
            auto* bold_button =
                find_visual_item(qobject_cast<QQuickItem*>(root.get()), "markdownInline-bold");
            const bool bold_requested = bold_button && QMetaObject::invokeMethod(bold_button, "clicked");
            const auto styled_heading = tools.sourceText(wrapper);
            const auto paragraphs = mirrorfly::markdown_paragraphs(styled_heading.toUtf8().toStdString());
            std::string bold_text;
            if (paragraphs.size() == 1)
                for (const auto& run : paragraphs[0].runs)
                    if (run.style.bold)
                        bold_text += run.style.text;
            passed =
                check(bold_requested && paragraphs.size() == 1 && paragraphs[0].heading && bold_text == "ab",
                    "actual bold button routes through parent selection and retains existing heading "
                    "styles") &&
                passed;
            for (const auto& action :
                {QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("strike")})
            {
                tools.loadDocument(wrapper, "> - L&#32;&#9;&#32;R\n> - tail\n", true, full_test_theme());
                const int whitespace_position = wrapper->textDocument()->toRawText().indexOf("L") + 1;
                QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, whitespace_position),
                    Q_ARG(int, whitespace_position + 3));
                page->setProperty("documentState", tools.inspectDocument(wrapper, whitespace_position));
                auto* whitespace_button =
                    find_visual_item(qobject_cast<QQuickItem*>(root.get()), "markdownInline-" + action);
                const bool whitespace_requested =
                    whitespace_button && QMetaObject::invokeMethod(whitespace_button, "clicked");
                const auto whitespace_paragraphs =
                    mirrorfly::markdown_paragraphs(tools.sourceText(wrapper).toUtf8().toStdString());
                std::string styled_whitespace;
                if (whitespace_paragraphs.size() == 2)
                    for (const auto& run : whitespace_paragraphs[0].runs)
                        if ((action == "bold" && run.style.bold) ||
                            (action == "italic" && run.style.italic) ||
                            (action == "strike" && run.style.strike))
                            styled_whitespace += run.style.text;
                passed =
                    check(whitespace_requested && tools.canSave() && styled_whitespace == " \t " &&
                            whitespace_paragraphs.size() == 2 &&
                            whitespace_paragraphs[0].containers.size() == 2,
                        "actual parent emphasis buttons retain a whitespace-only selection and its owners") &&
                    passed;
            }
            for (int level = 1; level <= 6; ++level)
            {
                tools.loadDocument(
                    wrapper, "> - **bold** [link](../x)\n>   ===\n> - tail\n", true, full_test_theme());
                const int heading_position = wrapper->textDocument()->toRawText().indexOf("bold");
                QMetaObject::invokeMethod(
                    text_edit, "select", Q_ARG(int, heading_position), Q_ARG(int, heading_position));
                page->setProperty("documentState", tools.inspectDocument(wrapper, heading_position));
                auto* menu_item = root->findChild<QObject*>(QString("markdownHeadingLevel%1").arg(level));
                if (!menu_item)
                {
                    auto* menu = root->findChild<QObject*>("markdownHeadingMenu");
                    QQuickItem* item = nullptr;
                    if (menu)
                        QMetaObject::invokeMethod(
                            menu, "itemAt", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, level));
                    menu_item = item;
                }
                const bool requested = menu_item && QMetaObject::invokeMethod(menu_item, "triggered");
                const auto heading_state = tools.inspectDocument(wrapper, heading_position);
                page->setProperty("documentState", heading_state);
                const auto headings = mirrorfly::markdown_paragraphs(tools.sourceText(wrapper).toStdString());
                passed = check(requested && heading_state.value("headingLevel").toInt() == level &&
                                 menu_item->property("checked").toBool() && headings.size() == 2 &&
                                 headings[0].heading_level == level && headings[0].containers.size() == 2 &&
                                 headings[0].containers[0].kind == "quote" &&
                                 headings[0].containers[1].kind == "listItem",
                             "actual six-level heading menu routes via parent and reflects container-local "
                             "level") &&
                    passed;
                auto* body_item = root->findChild<QObject*>("markdownHeadingLevel0");
                if (!body_item)
                {
                    auto* menu = root->findChild<QObject*>("markdownHeadingMenu");
                    QQuickItem* item = nullptr;
                    if (menu)
                        QMetaObject::invokeMethod(
                            menu, "itemAt", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, 0));
                    body_item = item;
                }
                passed =
                    check(body_item && QMetaObject::invokeMethod(body_item, "triggered") &&
                            tools.inspectDocument(wrapper, heading_position).value("headingLevel").toInt() ==
                                0,
                        "actual paragraph menu restores body through the same parent transaction") &&
                    passed;
            }
            for (const QString source : {QString("> - ##\n> - tail\n"), QString("> - ## head\n> - tail\n")})
            {
                tools.loadDocument(wrapper, source, true, full_test_theme());
                const int end = wrapper->textDocument()->begin().length() - 1;
                QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, end), Q_ARG(int, end));
                page->setProperty("documentState", tools.inspectDocument(wrapper, end));
                QKeyEvent enter_heading(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(text_edit, &enter_heading);
                const int position = text_edit->property("cursorPosition").toInt();
                passed =
                    check(enter_heading.isAccepted() &&
                            tools.inspectDocument(wrapper, position).value("headingLevel").toInt() == 0 &&
                            tools.canSave(),
                        "actual parent Return signal continues a heading as body without duplicate input") &&
                    passed;
            }
            tools.loadDocument(wrapper, "- first\n- parent\n\n  > quote\n\n  continuation\n\n- tail\n", true,
                full_test_theme());
            const int owned_parent = wrapper->textDocument()->toRawText().indexOf("parent");
            text_edit->setProperty("cursorPosition", owned_parent);
            page->setProperty("documentState", tools.inspectDocument(wrapper, owned_parent));
            passed = check(indent_button && indent_button->property("enabled").toBool() &&
                             QMetaObject::invokeMethod(indent_button, "clicked"),
                         "actual parent list-indent button accepts owned quote and continued paragraphs") &&
                passed;
            const auto moved_containers =
                mirrorfly::markdown_paragraphs(tools.sourceText(wrapper).toStdString());
            passed = check(moved_containers.size() == 5 && moved_containers[2].containers.size() == 3 &&
                             moved_containers[2].containers[0].kind == "listItem" &&
                             moved_containers[2].containers[1].kind == "listItem" &&
                             moved_containers[2].containers[2].kind == "quote",
                         "actual toolbar parent route preserves ordered descendant ownership after save") &&
                passed;
            tools.loadDocument(wrapper, "98) first\n99) second\n", true, full_test_theme());
            QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 1));
            page->setProperty("documentState", tools.inspectDocument(wrapper, 0));
            const bool ordered_requested = bold_button && QMetaObject::invokeMethod(bold_button, "clicked");
            const auto ordered_markers =
                mirrorfly::markdown_paragraphs(tools.sourceText(wrapper).toStdString());
            passed = check(ordered_requested && ordered_markers.size() == 2 &&
                             ordered_markers[0].containers[0].ordinal == 98 &&
                             ordered_markers[1].containers[0].ordinal == 99 &&
                             ordered_markers[0].containers[0].delimiter == ')' &&
                             ordered_markers[0].runs[0].style.bold,
                         "actual parent bold button preserves imported parenthesized numbering") &&
                passed;
            tools.loadDocument(wrapper,
                "- parent\n\n  > ```cpp\n  > x\n  > ```\n  >\n  > | A |\n  > | :---: |\n"
                "  > | a |\n  >\n  > ---\n\n- tail\n",
                true, full_test_theme());
            QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 1));
            page->setProperty("documentState", tools.inspectDocument(wrapper, 0));
            const bool mixed_requested = bold_button && QMetaObject::invokeMethod(bold_button, "clicked");
            const auto mixed_saved = tools.sourceText(wrapper);
            const auto mixed_leaves = mirrorfly::markdown_blocks(mixed_saved.toStdString());
            const auto mixed_paragraphs = mirrorfly::markdown_paragraphs(mixed_saved.toStdString());
            passed =
                check(mixed_requested && tools.canSave() && mixed_leaves.size() == 3 &&
                        mixed_paragraphs.size() == 2 && mixed_paragraphs[0].runs[0].style.bold &&
                        mixed_leaves[0].text == "x\n" && mixed_leaves[0].language == "cpp" &&
                        mixed_leaves[1].kind == "table" && mixed_leaves[2].kind == "thematicBreak" &&
                        mixed_leaves[0].containers.size() == 2 &&
                        mixed_leaves[0].containers[0].identity == mixed_paragraphs[0].containers[0].identity,
                    "actual parent bold route preserves mixed block ownership and code literals") &&
                passed;
            tools.loadDocument(wrapper,
                "- first\n- parent\n\n  > ```cpp\n  > x\n  > ```\n  >\n  > | A |\n  > | :---: |\n"
                "  > | a |\n  >\n  > ---\n\n- tail\n",
                true, full_test_theme());
            const int mixed_parent = wrapper->textDocument()->toRawText().indexOf("parent");
            text_edit->setProperty("cursorPosition", mixed_parent);
            page->setProperty("documentState", tools.inspectDocument(wrapper, mixed_parent));
            const bool mixed_move = indent_button && indent_button->property("enabled").toBool() &&
                QMetaObject::invokeMethod(indent_button, "clicked");
            const auto moved_leaves = mirrorfly::markdown_blocks(tools.sourceText(wrapper).toStdString());
            passed = check(mixed_move && tools.canSave() && moved_leaves.size() == 3 &&
                             moved_leaves[0].text == "x\n" && moved_leaves[0].containers.size() == 3 &&
                             moved_leaves[0].containers[0].kind == "listItem" &&
                             moved_leaves[0].containers[1].kind == "listItem" &&
                             moved_leaves[0].containers[2].kind == "quote",
                         "actual parent indent button carries mixed code/table/rule ownership") &&
                passed;
            tools.loadDocument(wrapper, "beforeafter\n", true, full_test_theme());
            text_edit->setProperty("cursorPosition", 6);
            page->setProperty("documentState", tools.inspectDocument(wrapper, 6));
            auto* rule_button = root->findChild<QObject*>("markdownThematicBreak");
            passed = check(rule_button && rule_button->property("enabled").toBool() &&
                             QMetaObject::invokeMethod(rule_button, "clicked") &&
                             mirrorfly::markdown_thematic_break_count(
                                 tools.sourceText(wrapper).toStdString()) == 1,
                         "actual separator button routes through the parent block transaction") &&
                passed;
            tools.loadDocument(wrapper, "beforeafter\n", true, full_test_theme());
            text_edit->setProperty("cursorPosition", 6);
            page->setProperty("documentState", tools.inspectDocument(wrapper, 6));
            auto* hard_break_button = root->findChild<QObject*>("markdownHardBreak");
            passed =
                check(hard_break_button && hard_break_button->property("enabled").toBool() &&
                        QMetaObject::invokeMethod(hard_break_button, "clicked") &&
                        mirrorfly::markdown_hard_breaks(tools.sourceText(wrapper).toStdString()).size() == 1,
                    "actual hard-break toolbar button uses the parent editing transaction") &&
                passed;
            wrapper->textDocument()->undo();
            text_edit->setProperty("cursorPosition", 6);
            QKeyEvent hard_enter(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier, QStringLiteral("\r"));
            QCoreApplication::sendEvent(text_edit, &hard_enter);
            passed =
                check(hard_enter.isAccepted() &&
                        mirrorfly::markdown_hard_breaks(tools.sourceText(wrapper).toStdString()).size() ==
                            1 &&
                        wrapper->textDocument()->blockCount() == 1,
                    "Shift+Enter routes through the same parent hardBreak action without native paragraph "
                    "insertion") &&
                passed;
            tools.loadDocument(
                wrapper, "[**label**](../selected \"selected title\")tail\n", true, full_test_theme());
            auto* dialog = root->findChild<QObject*>("markdownLinkDialog");
            for (const auto& selection : {std::pair<int, int>{0, 5}, {5, 0}})
            {
                QMetaObject::invokeMethod(
                    text_edit, "select", Q_ARG(int, selection.first), Q_ARG(int, selection.second));
                page->setProperty("documentState", tools.inspectDocument(wrapper, 5));
                const bool refreshed = QMetaObject::invokeMethod(toolbar, "contextRequested");
                const bool opened = dialog && QMetaObject::invokeMethod(dialog, "opened");
                passed = check(refreshed && opened &&
                                 page->property("documentState").toMap().value("inLink").toBool() &&
                                 address && address->property("text") == "../selected" && title &&
                                 title->property("text") == "selected title",
                             "forward/backward selection at adjacent plain text refreshes parent link "
                             "context before dialog prefill") &&
                    passed;
            }
            text_edit->setProperty("cursorPosition", 1);
            page->setProperty("documentState", tools.inspectDocument(wrapper, 1));
            auto* unlink = root->findChild<QObject*>("markdownUnlink");
            passed = check(unlink && unlink->property("enabled").toBool() &&
                             QMetaObject::invokeMethod(unlink, "clicked") &&
                             !tools.inspectDocument(wrapper, 1).value("inLink").toBool(),
                         "actual unlink button uses the same parent transaction path") &&
                passed;
            tools.loadDocument(wrapper, "[first](../same)[second](../same)\n", true, full_test_theme());
            for (const auto& selected : {std::pair<int, int>{0, 5}, {5, 11}})
            {
                QMetaObject::invokeMethod(
                    text_edit, "select", Q_ARG(int, selected.first), Q_ARG(int, selected.second));
                const bool refreshed = QMetaObject::invokeMethod(toolbar, "contextRequested");
                const bool opened = dialog && QMetaObject::invokeMethod(dialog, "opened");
                passed = check(refreshed && opened && address && address->property("text") == "../same",
                             "adjacent equal-target selections refresh the specific parent link context") &&
                    passed;
                if (address)
                    address->setProperty("text", selected.first == 0 ? "../first" : "../second");
                if (title)
                    title->setProperty("text", "");
                passed =
                    check(apply_link && QMetaObject::invokeMethod(apply_link, "clicked") &&
                            tools.inspectDocument(wrapper, 0).value("linkUrl") == "../first" &&
                            tools.inspectDocument(wrapper, 5).value("linkUrl") ==
                                (selected.first == 0 ? "../same" : "../second"),
                        "actual dialog actions update only one adjacent anchor through parent signals") &&
                    passed;
            }
        }
        return passed;
    }

    bool test_page_mode_switch(QQmlEngine& engine)
    {
        mirrorfly::EditorTools tools;
        const QUrl ui_directory = QUrl::fromLocalFile(
            QDir(QStringLiteral(MIRRORFLY_TEST_SOURCE_DIRECTORY)).filePath(QStringLiteral("ui")) + u'/');
        QQmlComponent component(&engine);
        component.setData(QStringLiteral(R"(
import QtQuick
import "%1" as App
App.TextEditorPage
{
    id: page
    width: 1120
    height: 720
    required property var tools
    required property var themeData
    property var currentDocument: null
    property int editNotifications: 0
    theme: themeData
    onDocumentLoadRequested: function(document, source, markdown)
    {
        currentDocument = document;
        finishDocumentLoad(tools.loadDocument(document, source, markdown, themeData));
    }
    onDocumentStateRequested: function(document, position)
    {
        applyDocumentState(tools.inspectDocument(document, position));
    }
    Connections
    {
        target: page.tools
        function onDocumentEdited(document)
        {
            ++page.editNotifications;
            page.content = page.tools.sourceText(document);
        }
    }
}
)")
                              .arg(ui_directory.toString())
                              .toUtf8(),
            QUrl{});
        std::unique_ptr<QObject> page(
            component.createWithInitialProperties({{QStringLiteral("tools"), QVariant::fromValue(&tools)},
                {QStringLiteral("themeData"), full_test_theme()}}));
        if (!check(page != nullptr, "create an isolated editor for document-mode switching"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        QCoreApplication::processEvents();
        bool passed = true;
        for (int iteration = 0; iteration < 6; ++iteration)
        {
            const bool markdown = iteration % 2 == 0;
            const auto source = markdown ? QString::fromUtf8(u8"# 课程笔记\n\n正文 **重点**\n")
                                         : QString::fromUtf8(u8"普通文本 <p>保留字面标签</p>\n第二行");
            page->setProperty("editNotifications", 0);
            page->setProperty("content", source);
            page->setProperty("revision", iteration + 1);
            page->setProperty("markdown", markdown);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            auto* wrapper = page->property("currentDocument").value<QQuickTextDocument*>();
            passed = check(wrapper && tools.sourceText(wrapper) == source &&
                             !wrapper->textDocument()->toRawText().contains(QStringLiteral("<!DOCTYPE")),
                         "switching editor modes never inserts Qt HTML into the document") &&
                passed;
            passed = check(page->property("editNotifications").toInt() == 0,
                         "mode changes do not emit user edits or replace the incoming source") &&
                passed;
        }
        return passed;
    }

    bool test_adjacent_code_frames(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        bool passed = true;
        for (const auto& separation : {QString{}, QStringLiteral("\n")})
        {
            const QString source = QStringLiteral("```cpp\nfirst();\n```\n") + separation +
                QStringLiteral("```cpp\nsecond();\n```\n");
            passed = check(valid(tools.loadDocument(wrapper, source, true, test_theme())),
                         "adjacent same-language fences load into a visual document") &&
                passed;
            auto* document = wrapper->textDocument();
            const int first_position = document->toRawText().indexOf(QStringLiteral("first();"));
            const int second_position = document->toRawText().indexOf(QStringLiteral("second();"));
            if (!check(first_position >= 0 && second_position >= 0,
                    "both independently fenced code bodies remain present"))
            {
                return false;
            }
            QTextCursor first(document);
            first.setPosition(first_position);
            QTextCursor second(document);
            second.setPosition(second_position);
            passed = check(first.currentFrame() != second.currentFrame(),
                         "independent adjacent code fences remain distinct native frames") &&
                passed;
            passed = check(valid(tools.applyEdit(wrapper, first_position, first_position,
                               QStringLiteral("codeLanguage"),
                               {{QStringLiteral("language"), QStringLiteral("python")}})) &&
                             tools.inspectDocument(wrapper, second_position)
                                     .value(QStringLiteral("codeLanguage"))
                                     .toString() == QStringLiteral("cpp"),
                         "changing the first fence language does not alter the second fence") &&
                passed;
            document->undo();
            passed = check(tools.sourceText(wrapper) == source,
                         "undo preserves both original fenced regions and their separation") &&
                passed;
        }
        const QString literal = QStringLiteral("    ```cpp\n    literal code\n    ```\n");
        passed = check(valid(tools.loadDocument(wrapper, literal, true, test_theme())) &&
                         wrapper->textDocument()->toRawText().contains(QStringLiteral("```cpp")) &&
                         !wrapper->textDocument()->toRawText().contains(QStringLiteral("mirrorflyFence")) &&
                         tools.sourceText(wrapper) == literal,
                     "temporary fence identifiers never alter literal fence text in indented code") &&
            passed;
        const QString quoted = QStringLiteral("> ```cpp\n> first();\n> ```\n> ```cpp\n> second();\n> ```\n");
        passed = check(valid(tools.loadDocument(wrapper, quoted, true, test_theme())),
                     "adjacent fences inside a quote remain editable") &&
            passed;
        auto* quoted_document = wrapper->textDocument();
        const int quoted_position = quoted_document->toRawText().indexOf(QStringLiteral("first();"));
        passed = check(valid(tools.applyEdit(wrapper, quoted_position, quoted_position,
                           QStringLiteral("codeLanguage"),
                           {{QStringLiteral("language"), QStringLiteral("python")}})) &&
                         tools.sourceText(wrapper).contains(QStringLiteral("> ```python")) &&
                         tools.sourceText(wrapper).contains(QStringLiteral("> first();")),
                     "visual code language edits preserve the surrounding quote structure") &&
            passed;
        return passed;
    }

    bool test_tables(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        const QString source =
            QString::fromUtf8(u8"# 计划\n\n| 科目 | 内容 |\n| --- | --- |\n| 数学 | 复习 |\n");
        bool passed = check(valid(tools.loadDocument(wrapper, source, true, test_theme())),
            "ordinary Markdown tables remain editable");
        auto* document = wrapper->textDocument();
        const int position = document->toRawText().indexOf(QString::fromUtf8(u8"复习"));
        QTextCursor cell(document);
        cell.setPosition(position);
        auto* table = cell.currentTable();
        if (!check(table != nullptr, "table cells are native QTextTable cells"))
        {
            return false;
        }
        const auto before_cross_cell = tools.sourceText(wrapper);
        passed = check(!valid(tools.applyEdit(wrapper, table->cellAt(1, 0).firstPosition(),
                           table->cellAt(1, 1).lastPosition(), QStringLiteral("bold"))) &&
                         tools.sourceText(wrapper) == before_cross_cell,
                     "cross-cell selections cannot modify native table structure") &&
            passed;
        cell.beginEditBlock();
        cell.insertText(QString::fromUtf8(u8"🦋\u00A0"));
        cell.endEditBlock();
        const QString edited = tools.sourceText(wrapper);
        passed = check(tools.canSave() && edited.contains(QString::fromUtf8(u8"🦋\u00A0复习")),
                     "editing a cell preserves supplementary characters and NBSP") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == source,
                     "cell text editing can be undone without changing the source") &&
            passed;
        passed = check(valid(tools.applyEdit(wrapper, position, position, QStringLiteral("rowAdd"))) &&
                         table->rows() == 3,
                     "row insertion updates the native editable table") &&
            passed;
        document->undo();
        passed = check(table->rows() == 2 && tools.sourceText(wrapper) == source,
                     "row insertion and header styling form one undo transaction") &&
            passed;
        passed = check(valid(tools.applyEdit(wrapper, position, position, QStringLiteral("columnAdd"))) &&
                         table->columns() == 3,
                     "column insertion updates the native table") &&
            passed;
        const auto with_empty_column = tools.sourceText(wrapper);
        QTextDocument reopened_table;
        reopened_table.setMarkdown(with_empty_column);
        QTextCursor reopened_cell(&reopened_table);
        reopened_cell.setPosition(reopened_table.toRawText().indexOf(QString::fromUtf8(u8"复习")));
        passed = check(tools.canSave() && reopened_cell.currentTable() != nullptr &&
                         reopened_cell.currentTable()->columns() == 3,
                     "an entirely empty added column survives Markdown export and reimport") &&
            passed;
        document->undo();
        passed = check(table->columns() == 2 && tools.sourceText(wrapper) == source,
                     "column insertion is also atomic") &&
            passed;
        const auto next_row = tools.applyEdit(wrapper, table->cellAt(0, 0).firstPosition(),
            table->cellAt(0, 0).firstPosition(), QStringLiteral("tableNextRow"));
        passed = check(valid(next_row) &&
                         next_row.value(QStringLiteral("selectionEnd")).toInt() ==
                             table->cellAt(1, 0).firstPosition() &&
                         !document->isUndoAvailable(),
                     "Enter moves to the next row without inserting a paragraph inside a cell") &&
            passed;
        const auto next_cell = tools.applyEdit(wrapper, position, position, QStringLiteral("tableNextCell"));
        passed = check(valid(next_cell) && table->rows() == 3 &&
                         next_cell.value(QStringLiteral("selectionEnd")).toInt() ==
                             table->cellAt(2, 0).firstPosition(),
                     "Tab at the final cell adds a row and enters its first editable cell") &&
            passed;
        document->undo();
        passed = check(table->rows() == 2 && tools.sourceText(wrapper) == source,
                     "automatic row insertion is one reversible transaction") &&
            passed;
        const auto safe_source = tools.sourceText(wrapper);
        cell.setPosition(position);
        cell.insertBlock();
        passed = check(tools.sourceText(wrapper) == safe_source && !tools.canSave() &&
                         !tools.inspectDocument(wrapper, position)
                             .value(QStringLiteral("error"))
                             .toString()
                             .isEmpty(),
                     "unsupported multi-paragraph cells block saving and retain the last valid source") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper) == source && tools.canSave(),
                     "undo restores valid serialization after a rejected structure") &&
            passed;
        const auto exited = tools.applyEdit(wrapper, position, position, QStringLiteral("exitTable"));
        passed = check(valid(exited) &&
                         !tools.inspectDocument(wrapper, exited.value(QStringLiteral("selectionEnd")).toInt())
                             .value(QStringLiteral("inTable"))
                             .toBool(),
                     "exit table places the cursor in a normal paragraph") &&
            passed;
        return passed;
    }

    bool test_list_hierarchy_and_tasks(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        const QString source = QString::fromUtf8(u8"- 父🦋\n- [x] 第二 **保留**\n  - 子项\n- 末项\n");
        bool passed = check(valid(tools.loadDocument(wrapper, source, true, test_theme())),
            "load nested lists and existing task state through the public visual document interface");
        auto* document = wrapper->textDocument();
        const int second = document->toRawText().indexOf(QString::fromUtf8(u8"第二"));
        const int child = document->toRawText().indexOf(QString::fromUtf8(u8"子项"));
        const int tail = document->toRawText().indexOf(QString::fromUtf8(u8"末项"));
        const auto state = tools.inspectDocument(wrapper, second);
        passed = check(state.value("inList").toBool() && state.value("canIndentList").toBool() &&
                         state.value("taskItem").toBool() && state.value("taskChecked").toBool() &&
                         state.value("listLevel").toInt() == 1,
                     "list inspection publishes actual hierarchy and task state for the parent toolbar") &&
            passed;
        const auto raw = document->toRawText();
        passed = check(valid(tools.applyEdit(wrapper, second, second, "listIndent")) &&
                         document->findBlock(second).textList()->format().indent() == 2 &&
                         document->findBlock(child).textList()->format().indent() == 3 &&
                         document->findBlock(tail).textList()->format().indent() == 1 &&
                         document->toRawText() == raw && tools.canSave(),
                     "visual indentation moves descendants without shifting unselected siblings or text") &&
            passed;
        const auto serialized = tools.sourceText(wrapper);
        document->undo();
        passed = check(document->findBlock(second).textList()->format().indent() == 1 &&
                         document->findBlock(child).textList()->format().indent() == 2 &&
                         tools.sourceText(wrapper) == source,
                     "list movement is one reversible transaction") &&
            passed;
        document->redo();
        passed = check(valid(tools.applyEdit(wrapper, second, second, "listOutdent")) &&
                         document->findBlock(second).textList()->format().indent() == 1,
                     "visual outdent restores the root level") &&
            passed;
        passed = check(valid(tools.applyEdit(wrapper, second, second, "task")) &&
                         document->findBlock(second).blockFormat().marker() ==
                             QTextBlockFormat::MarkerType::Checked,
                     "applying task style again retains the completed state") &&
            passed;
        QTextCursor emphasis(document);
        emphasis.setPosition(document->toRawText().indexOf(QString::fromUtf8(u8"保留")) + 1);
        passed = check(valid(tools.applyEdit(wrapper, second, second, "taskSet", {{"checked", false}})) &&
                         document->findBlock(second).blockFormat().marker() ==
                             QTextBlockFormat::MarkerType::Unchecked &&
                         emphasis.charFormat().fontWeight() >= QFont::Bold,
                     "explicit task state changes retain inline emphasis") &&
            passed;
        const auto before_invalid = tools.sourceText(wrapper);
        passed = check(!valid(tools.applyEdit(wrapper, second, second, "taskSet", {{"checked", "true"}})) &&
                         !valid(tools.applyEdit(wrapper, tail, tail, "taskSet", {{"checked", true}})) &&
                         tools.sourceText(wrapper) == before_invalid,
                     "invalid task types and non-task items reject without edits") &&
            passed;
        passed = check(valid(tools.loadDocument(wrapper, serialized, true, test_theme())),
                     "serialized nested list can reopen") &&
            passed;
        document = wrapper->textDocument();
        const int reopened = document->toRawText().indexOf(QString::fromUtf8(u8"第二"));
        passed =
            check(document->findBlock(reopened).textList()->format().indent() == 2 &&
                    document->findBlock(reopened).blockFormat().marker() ==
                        QTextBlockFormat::MarkerType::Checked &&
                    valid(tools.applyEdit(wrapper, reopened, reopened, "taskSet", {{"checked", true}})) &&
                    tools.sourceText(wrapper) == serialized,
                "saved hierarchy and completed tasks survive reopening and idempotent state changes") &&
            passed;
        const QString tasks = "- [ ] first\n  - [x] nested\n- [ ] last\n";
        tools.loadDocument(wrapper, tasks, true, test_theme());
        document = wrapper->textDocument();
        passed = check(valid(tools.applyEdit(
                           wrapper, 0, document->characterCount() - 1, "taskSet", {{"checked", true}})),
                     "a selection explicitly completes tasks across multiple nesting levels") &&
            passed;
        for (auto block = document->begin(); block.isValid(); block = block.next())
            if (block.textList())
                passed = check(block.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked,
                             "each selected task has the requested state") &&
                    passed;
        tools.loadDocument(wrapper, "- first\n  - child\n- second\n", true, test_theme());
        document = wrapper->textDocument();
        passed = check(valid(tools.applyEdit(wrapper, 0, document->characterCount() - 1, "ordered")),
                     "ordered list conversion accepts a nested selection") &&
            passed;
        const auto ordered = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, ordered, true, test_theme());
        document = wrapper->textDocument();
        const auto nested = document->findBlock(document->toRawText().indexOf("child"));
        passed = check(nested.textList() && nested.textList()->format().indent() == 2 &&
                         nested.textList()->format().style() == QTextListFormat::ListDecimal,
                     "nested ordered style survives serialization with each sibling sequence intact") &&
            passed;
        return passed;
    }

    bool test_quote_containers(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        const auto theme = full_test_theme();
        const QString source = "> **parent**\n> > child🦋\n\nplain\n";
        tools.loadDocument(wrapper, source, true, theme);
        auto* document = wrapper->textDocument();
        const int parent = document->toRawText().indexOf("parent");
        const int child = document->toRawText().indexOf("child");
        const auto raw = document->toRawText();
        bool passed =
            check(valid(tools.applyEdit(wrapper, parent, parent, "quoteSet", {{"quoteLevel", 2}})) &&
                    tools.inspectDocument(wrapper, parent).value("quoteLevel").toInt() == 2 &&
                    tools.inspectDocument(wrapper, child).value("quoteLevel").toInt() == 3 &&
                    document->findBlock(parent).begin().fragment().charFormat().fontWeight() >= QFont::Bold &&
                    document->toRawText() == raw,
                "visual quote levels preserve nested children, character formats and text");
        const auto saved = tools.sourceText(wrapper);
        QTextDocument reopened;
        mirrorfly::load_markdown_document(reopened, saved, theme);
        const int reopened_child = reopened.toRawText().indexOf("child");
        passed =
            check(
                reopened.findBlock(reopened_child).blockFormat().intProperty(QTextFormat::BlockQuoteLevel) ==
                    3,
                "quote nesting survives visual save and reopen") &&
            passed;
        document->undo();
        passed =
            check(tools.sourceText(wrapper) == source, "one undo restores all quote descendants") && passed;
        document->redo();
        passed =
            check(tools.sourceText(wrapper) == saved, "one redo restores the entire quote transaction") &&
            passed;
        for (const QVariant& value :
            {QVariant(true), QVariant("2"), QVariant(1.5), QVariant(-1), QVariant(9)})
            passed =
                check(!valid(tools.applyEdit(wrapper, parent, parent, "quoteSet", {{"quoteLevel", value}})) &&
                        tools.sourceText(wrapper) == saved,
                    "invalid visual quote levels cannot mutate the document") &&
                passed;

        struct TableSample
        {
            QString source;
            int quote;
            int list_indent;
            int owner_quote;
        };
        const TableSample samples[] = {
            {"> before\n>\n> | A | B |\n> | --- | :---: |\n> | a | b |\n>\n> after\n", 1, 0, 0},
            {"> > | A |\n> > | ---: |\n", 2, 0, 0},
            {"- parent\n\n  | A | B |\n  | --- | :---: |\n  | a | b |\n\n- tail\n", 0, 2, 0},
            {"> - parent\n>\n>   | A | B |\n>   | --- | :---: |\n>   | a | b |\n>\n> - tail\n", 1, 2, 1},
            {"- parent\n\n  > | A | B |\n  > | --- | :---: |\n  > | a | b |\n\n- tail\n", 1, 2, 0},
            {"> - | A |\n>   | --- |\n>   | a |\n", 1, 2, 0}};
        for (const auto& sample : samples)
        {
            passed = check(valid(tools.loadDocument(wrapper, sample.source, true, theme)),
                         "quoted and list-owned tables load through the public document path") &&
                passed;
            document = wrapper->textDocument();
            QTextCursor cell(document);
            cell.setPosition(document->toRawText().indexOf("A"));
            auto* table = cell.currentTable();
            if (!table)
                return check(false, "container sample imports as an actual table");
            const auto table_raw = document->toRawText();
            passed = check(tools.inspectDocument(wrapper, cell.position()).value("quoteLevel").toInt() ==
                                 sample.quote &&
                             tools.inspectDocument(wrapper, cell.position()).value("quoteMinimum").toInt() ==
                                 sample.owner_quote &&
                             valid(tools.applyEdit(wrapper, cell.position(), cell.position() + 1, "italic")),
                         "table quote metadata and inherited minimum stay separate from cell formats") &&
                passed;
            const auto serialized = tools.sourceText(wrapper);
            const auto metadata = mirrorfly::markdown_tables(serialized.toUtf8().toStdString());
            if (metadata.size() != 1)
                std::cerr << serialized.toStdString() << '\n';
            passed = check(metadata.size() == 1 && metadata[0].quote_level == sample.quote &&
                             metadata[0].list_indent == sample.list_indent &&
                             metadata[0].list_quote_level ==
                                 (sample.source.startsWith("> - |") ? 1 : sample.owner_quote),
                         "table serialization restores quote and list prefixes on every structural line") &&
                passed;
            QTextDocument table_reopened;
            mirrorfly::load_markdown_document(table_reopened, serialized, theme);
            const auto twice = mirrorfly::serialize_markdown_document(table_reopened);
            if (twice.source != serialized)
                std::cerr << "ONCE:\n"
                          << serialized.toStdString() << "TWICE:\n"
                          << twice.source.toStdString();
            passed =
                check(table_reopened.toRawText() == table_raw && twice.valid && twice.source == serialized,
                    "container tables retain surrounding text and stable source over two saves") &&
                passed;
            document->undo();
            passed = check(tools.sourceText(wrapper) == sample.source,
                         "container table undo restores exact source") &&
                passed;
            passed = check(valid(tools.applyEdit(wrapper, cell.position(), cell.position(), "quoteSet",
                               {{"quoteLevel", sample.quote + 1}})),
                         "a table quote selector targets the complete table") &&
                passed;
            const auto quote_saved = tools.sourceText(wrapper);
            const auto quote_metadata = mirrorfly::markdown_tables(quote_saved.toUtf8().toStdString());
            passed = check(quote_metadata.size() == 1 && quote_metadata[0].quote_level == sample.quote + 1 &&
                             quote_metadata[0].list_indent == sample.list_indent,
                         "changing table depth preserves its list ownership") &&
                passed;
            if (sample.owner_quote > 0)
                passed = check(!valid(tools.applyEdit(wrapper, cell.position(), cell.position(), "quoteSet",
                                   {{"quoteLevel", 0}})),
                             "a table cannot remove a quote inherited from its nonempty list parent") &&
                    passed;
            document->undo();
            if (sample.list_indent > 0 && document->toRawText().contains("parent"))
            {
                const int owner = document->toRawText().indexOf("parent");
                passed =
                    check(valid(tools.applyEdit(
                              wrapper, owner, owner, "quoteSet", {{"quoteLevel", sample.owner_quote + 1}})) &&
                            tools.inspectDocument(wrapper, cell.position()).value("quoteLevel").toInt() ==
                                sample.quote + 1,
                        "parent quote changes carry their table through one style transaction") &&
                    passed;
            }
            else if (sample.source.startsWith("> - |"))
            {
                passed = check(valid(tools.applyEdit(wrapper, cell.position(), cell.position(), "quoteSet",
                                   {{"quoteLevel", 0}})),
                             "a table that owns an empty list item can remove that item's outer quote") &&
                    passed;
                const auto unquoted =
                    mirrorfly::markdown_tables(tools.sourceText(wrapper).toUtf8().toStdString());
                passed = check(unquoted.size() == 1 && unquoted[0].quote_level == 0 &&
                                 unquoted[0].list_quote_level == 0 && unquoted[0].list_indent == 2,
                             "empty-owner quote removal retains the table's list container") &&
                    passed;
            }
        }
        tools.loadDocument(
            wrapper, "- first\n- parent\n\n  | A |\n  | --- |\n  | a |\n\n- tail\n", true, theme);
        document = wrapper->textDocument();
        const int table_parent = document->toRawText().indexOf("parent");
        passed = check(valid(tools.applyEdit(wrapper, table_parent, table_parent, "listIndent")),
                     "list indentation carries an owned table's container width") &&
            passed;
        const auto moved = mirrorfly::markdown_tables(tools.sourceText(wrapper).toUtf8().toStdString());
        passed = check(moved.size() == 1 && moved[0].list_indent == 4,
                     "table prefix indentation follows its owner's current hierarchy") &&
            passed;
        const QString fence(3, u'\x60');
        const auto code = "> " + fence + "cpp\n> int value = 7;\n> " + fence + "\n\nplain\n";
        tools.loadDocument(wrapper, code, true, theme);
        document = wrapper->textDocument();
        const int position = document->toRawText().indexOf("int value");
        passed = check(valid(tools.applyEdit(wrapper, position, position, "quoteSet", {{"quoteLevel", 2}})) &&
                         tools.sourceText(wrapper).contains("> > " + fence + "cpp") &&
                         tools.sourceText(wrapper).contains("> > int value = 7;"),
                     "code-frame quote changes preserve language and literal content") &&
            passed;
        return passed;
    }

    bool test_empty_limits_and_modes(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper)
    {
        bool passed = check(valid(tools.loadDocument(wrapper, {}, true, test_theme())) &&
                tools.sourceText(wrapper).isEmpty() && tools.canSave(),
            "a new empty Markdown document is clean and serializes safely");
        auto* document = wrapper->textDocument();
        QTextCursor cursor(document);
        cursor.insertText(QString::fromUtf8(u8"笔记"));
        passed = check(tools.sourceText(wrapper).contains(QString::fromUtf8(u8"笔记")),
                     "native typing generates Markdown source") &&
            passed;
        document->undo();
        passed = check(tools.sourceText(wrapper).isEmpty(),
                     "undo to the initial empty document does not invent content") &&
            passed;
        const QString unsupported = QStringLiteral("![image](file:///no-resource-is-loaded.png)\n");
        passed = check(valid(tools.loadDocument(wrapper, unsupported, true, test_theme())) &&
                         tools.sourceText(wrapper) == unsupported && tools.canSave() &&
                         wrapper->textDocument()->toRawText().contains(QChar::ObjectReplacementCharacter),
                     "missing local images retain source and display an editable placeholder") &&
            passed;
        const QString large(128 * 1024 + 1, u'x');
        passed = check(!valid(tools.loadDocument(wrapper, large, true, test_theme())) &&
                         tools.sourceText(wrapper) == large,
                     "oversized visual documents preserve all source content without truncation") &&
            passed;
        const QString plain = QString::fromUtf8(u8"# 原文\n前\u00A0后 🦋");
        passed = check(valid(tools.loadDocument(wrapper, plain, false, test_theme())) &&
                         tools.sourceText(wrapper) == plain &&
                         wrapper->textDocument()->begin().blockFormat().headingLevel() == 0,
                     "switching to plain text reloads literal content and preserves NBSP") &&
            passed;
        passed = check(!valid(tools.applyEdit(nullptr, 0, 0, QStringLiteral("bold"))),
                     "missing document pointers are rejected safely") &&
            passed;
        return passed;
    }

}

int run_markdown_bridge_tests(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication application(argc, argv);
    QQmlEngine engine;
    mirrorfly::EditorTools tools;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.RichText }", QUrl{});
    std::unique_ptr<QObject> editor(component.create());
    if (!check(editor != nullptr, "create an isolated rich TextEdit without any application window"))
    {
        std::cerr << component.errorString().toStdString();
        return 1;
    }
    auto* wrapper = editor->property("textDocument").value<QQuickTextDocument*>();
    if (!check(wrapper != nullptr, "use only the public QQuickTextDocument interface"))
    {
        return 1;
    }
    bool passed = test_load_unicode_undo(tools, wrapper);
    passed = test_strike_roundtrip(tools, wrapper) && passed;
    passed = test_inline_code_roundtrip(tools, wrapper) && passed;
    passed = test_table_alignment(tools, wrapper) && passed;
    passed = test_list_hierarchy_and_tasks(tools, wrapper) && passed;
    passed = test_quote_containers(tools, wrapper) && passed;
    passed = test_links(tools, wrapper) && passed;
    passed = test_resolved_links(tools, wrapper) && passed;
    passed = test_thematic_breaks(tools, wrapper) && passed;
    passed = test_markdown_paragraph_styles(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_code_spans(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_whitespace(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_images(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_headings(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_containers(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_ordered_lists(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_block_containers(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_block_moves(tools, wrapper, test_theme()) && passed;
    passed = test_markdown_block_quotes(tools, wrapper, test_theme()) && passed;
    passed = test_hard_breaks(tools, wrapper) && passed;
    passed = test_markdown_link_styles(tools, wrapper, test_theme()) && passed;
    passed = test_insert_code_signal_path(tools, editor.get(), wrapper) && passed;
    passed = test_code_insert_positions(tools, editor.get(), wrapper) && passed;
    passed = test_explicit_block_input(tools, wrapper) && passed;
    passed = test_code_frames(tools, wrapper) && passed;
    passed = test_adjacent_code_frames(tools, wrapper) && passed;
    passed = test_tables(tools, wrapper) && passed;
    passed = test_markdown_import_boundaries(tools, wrapper, test_theme()) && passed;
    passed = test_empty_limits_and_modes(tools, wrapper) && passed;
    passed = test_document_lifetimes(engine) && passed;
    passed = test_text_editor_page_code_path(engine, tools) && passed;
    passed = test_page_mode_switch(engine) && passed;
    if (passed)
    {
        std::cout << "Visual Markdown document tests passed.\n";
    }
    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_markdown_bridge_tests(argc, argv);
}
