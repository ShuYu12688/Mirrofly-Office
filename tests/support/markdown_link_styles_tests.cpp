#include "markdown_link_styles_tests.hpp"
#include "editor_tools.hpp"

#include <mirrorfly/markdown.hpp>

#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextDocument>

#include <iostream>

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    int style_at(QTextDocument& document, int position)
    {
        QTextCursor cursor(&document);
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        const auto format = cursor.charFormat();
        return (format.fontWeight() >= QFont::Bold ? 1 : 0) | (format.fontItalic() ? 2 : 0) |
            (format.fontStrikeOut() ? 4 : 0) | (format.fontFixedPitch() ? 8 : 0);
    }

    bool valid(const QVariantMap& result)
    {
        return result.value("valid").toBool();
    }

    bool adjacent_links(mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
    {
        bool passed = true;
        for (const auto& source : {QStringLiteral("[first](../same \"tip\")[second](../same \"tip\")\n"),
                 QStringLiteral("[**first**][r][*second*][r]\n\n[r]: ../same \"tip\"\n")})
        {
            passed = check(valid(tools.loadDocument(wrapper, source, true, theme)),
                         "adjacent same-target occurrences load independently") &&
                passed;
            auto* document = wrapper->textDocument();
            passed = check(document->toRawText() == "firstsecond" &&
                             !valid(tools.applyEdit(wrapper, 2, 8, "link", {{"url", "../bad"}})) &&
                             tools.sourceText(wrapper) == source && !document->isUndoAvailable(),
                         "selection across two anchors cannot merge them into one link") &&
                passed;
            passed = check(valid(tools.applyEdit(wrapper, 1, 1, "link", {{"url", "../first"}})) &&
                             tools.inspectDocument(wrapper, 5).value("linkUrl") == "../same",
                         "updating the first anchor leaves the adjacent same-target anchor untouched") &&
                passed;
            const auto saved = tools.sourceText(wrapper);
            document->undo();
            passed = check(tools.sourceText(wrapper) == source,
                         "undo restores both adjacent source occurrences exactly") &&
                passed;
            document->redo();
            passed = check(tools.sourceText(wrapper) == saved,
                         "redo preserves the independent link transaction") &&
                passed;
            tools.loadDocument(wrapper, saved, true, theme);
            document = wrapper->textDocument();
            passed = check(document->toRawText() == "firstsecond" &&
                             tools.inspectDocument(wrapper, 0).value("linkUrl") == "../first" &&
                             tools.inspectDocument(wrapper, 5).value("linkUrl") == "../same" &&
                             valid(tools.applyEdit(wrapper, 5, 5, "unlink")) &&
                             tools.inspectDocument(wrapper, 1).value("inLink").toBool() &&
                             !tools.inspectDocument(wrapper, 6).value("inLink").toBool(),
                         "save-reopen and second-anchor removal preserve the neighboring link") &&
                passed;
        }
        const QString source = "[first](../same)second\n";
        tools.loadDocument(wrapper, source, true, theme);
        passed = check(valid(tools.applyEdit(wrapper, 5, 11, "link", {{"url", "../same"}})),
                     "a new same-target link can be inserted immediately after an existing link") &&
            passed;
        const auto saved = tools.sourceText(wrapper);
        const auto links = mirrorfly::markdown_links(saved.toUtf8().toStdString());
        passed = check(links.size() == 2 && links[0].text == "first" && links[1].text == "second",
                     "the serializer emits two source occurrences instead of merging equal addresses") &&
            passed;
        tools.loadDocument(wrapper, saved, true, theme);
        passed = check(valid(tools.applyEdit(wrapper, 6, 6, "link", {{"url", "../second"}})) &&
                         tools.inspectDocument(wrapper, 1).value("linkUrl") == "../same",
                     "freshly inserted adjacent identities survive save-reopen") &&
            passed;
        return passed;
    }
}

bool test_markdown_link_styles(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    bool passed = adjacent_links(tools, wrapper, theme);
    QJsonArray corpus;
    for (int combination = 0; combination < 512; ++combination)
    {
        if (!check(valid(tools.loadDocument(wrapper, "[abc](../styles)\n", true, theme)),
                "load crossing-style link test"))
            return false;
        QJsonArray states;
        for (int position = 0; position < 3; ++position)
        {
            const int mask = (combination >> (position * 3)) & 7;
            states.append(mask);
            const QStringList actions{"bold", "italic", "strike"};
            for (int bit = 0; bit < 3; ++bit)
                if (mask & (1 << bit))
                    passed = check(valid(tools.applyEdit(wrapper, position, position + 1, actions[bit])),
                                 "apply crossing label style through public visual transactions") &&
                        passed;
        }
        const auto saved = tools.sourceText(wrapper);
        corpus.append(QJsonObject{{"source", saved}, {"text", "abc"}, {"states", states}});
        if (!check(tools.canSave() && valid(tools.loadDocument(wrapper, saved, true, theme)),
                "crossing label styles remain saveable and reopen without falling back"))
            return false;
        auto* document = wrapper->textDocument();
        passed = check(document->toRawText() == "abc" && !saved.contains("MIRRORFLY"),
                     "crossing styles preserve the literal caption without internal markers") &&
            passed;
        for (int position = 0; position < 3; ++position)
            passed = check(style_at(*document, position) == states[position].toInt(),
                         "each caption character retains its independent bold/italic/strike combination") &&
                passed;
        passed = check(valid(tools.applyEdit(wrapper, 1, 1, "link", {{"url", "../styles"}})) &&
                         tools.sourceText(wrapper) == saved,
                     "repeated label saves do not change emphasis nesting or entity boundaries") &&
            passed;
    }
    const QString source = QString::fromUtf8(u8"**[甲🦋 and `a|b`](../same)[乙](../same)**\n");
    passed = check(valid(tools.loadDocument(wrapper, source, true, theme)),
                 "inherited outer emphasis and styled inline code load through public caption runs") &&
        passed;
    passed = check(valid(tools.applyEdit(wrapper, 0, 0, "link", {{"url", "../unicode"}})),
                 "edit a Unicode caption without changing the neighboring inherited-style anchor") &&
        passed;
    const auto saved = tools.sourceText(wrapper);
    tools.loadDocument(wrapper, saved, true, theme);
    auto* document = wrapper->textDocument();
    const auto code = document->toRawText().indexOf("a|b");
    passed = check(document->toRawText() == QString::fromUtf8(u8"甲🦋 and a|b乙") && code >= 0 &&
                     style_at(*document, 0) == 1 && style_at(*document, code) == 9 &&
                     tools.inspectDocument(wrapper, document->toRawText().size() - 1).value("linkUrl") ==
                         "../same",
                 "Unicode, code pipes and outer bold survive independent caption save-reopen") &&
        passed;
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    for (const auto& literal : {std::string("a|b"), std::string("a\\|b"), std::string("a\\\\|b")})
    {
        const auto caption = mirrorfly::markdown_inline_label({{literal, true, true, true, true}}, true);
        const auto table_source =
            QString::fromStdString("| A | B |\n| --- | --- |\n| [" + caption + "](../same) | tail |\n");
        passed = check(valid(tools.loadDocument(wrapper, table_source, true, theme)),
                     "styled linked code with literal slashes and pipes loads inside a table") &&
            passed;
        document = wrapper->textDocument();
        const auto body = QString::fromStdString(literal);
        const int position = document->toRawText().indexOf(body);
        passed = check(position >= 0 && style_at(*document, position) == 15 &&
                         valid(tools.applyEdit(wrapper, position, position, "link", {{"url", "../pipe"}})),
                     "decoded caption code is not unescaped a second time by the table loader") &&
            passed;
        const auto table_saved = tools.sourceText(wrapper);
        tools.loadDocument(wrapper, table_saved, true, theme);
        document = wrapper->textDocument();
        const int reopened = document->toRawText().indexOf(body);
        passed = check(reopened >= 0 && style_at(*document, reopened) == 15 &&
                         tools.inspectDocument(wrapper, reopened).value("tableColumns").toInt() == 2,
                     "styled linked code keeps literal pipes, slashes and columns after save-reopen") &&
            passed;
        if (!output.isEmpty() && literal == "a\\|b")
        {
            QFile file(QDir(output).filePath("linked-code-table.md"));
            const auto bytes = table_saved.toUtf8();
            passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                         "export actual linked table code for independent parsing") &&
                passed;
        }
    }
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("crossing-styles.json"));
        const auto bytes = QJsonDocument(corpus).toJson(QJsonDocument::Compact);
        passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                     "export all actual crossing-style saves for independent CommonMark verification") &&
            passed;
    }
    tools.loadDocument(wrapper, "[caption](../safe)\n", true, theme);
    document = wrapper->textDocument();
    const auto original = tools.sourceText(wrapper);
    QTextCursor invalid(document);
    invalid.setPosition(2);
    invalid.insertText(QString(QChar(1)));
    passed = check(tools.sourceText(wrapper) == original && !tools.canSave(),
                 "invalid caption serialization preserves the last valid source instead of dropping text") &&
        passed;
    document->undo();
    passed = check(tools.sourceText(wrapper) == original && tools.canSave(),
                 "undo restores saving after an invalid caption edit") &&
        passed;
    return passed;
}
