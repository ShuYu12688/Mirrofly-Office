#include "markdown_paragraph_styles_tests.hpp"
#include "editor_tools.hpp"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <mirrorfly/markdown.hpp>

#include <iostream>

namespace
{
    bool check(bool value, const char* message)
    {
        if (!value)
            std::cerr << "FAIL: " << message << '\n';
        return value;
    }

    int style_at(QTextDocument& document, int position)
    {
        QTextCursor cursor(&document);
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        const auto format = cursor.charFormat();
        return (format.fontWeight() >= QFont::Bold ? 1 : 0) | (format.fontItalic() ? 2 : 0) |
            (format.fontStrikeOut() ? 4 : 0);
    }
}

bool test_markdown_paragraph_styles(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    bool passed = true;
    QJsonArray corpus;
    for (const auto& initial : {QStringLiteral("abc\n"), QStringLiteral("> abc\n"), QStringLiteral("- abc\n"),
             QStringLiteral("# abc\n"), QStringLiteral("a[b](../body)c\n"),
             QStringLiteral("# a[b](../body)c\n")})
        for (int mask = 0; mask < 512; ++mask)
        {
            passed = check(tools.loadDocument(wrapper, initial, true, theme).value("valid").toBool(),
                         "body/heading/container style fixture imports") &&
                passed;
            const auto original = wrapper->textDocument()->toRawText();
            for (int position = 0; position < 3; ++position)
                for (const auto& action : {std::pair<int, QString>{1, "bold"}, {2, "italic"}, {4, "strike"}})
                    if ((mask >> (position * 3)) & action.first)
                        passed = check(tools.applyEdit(wrapper, position, position + 1, action.second)
                                           .value("valid")
                                           .toBool(),
                                     "crossing body styles use actual public visual transactions") &&
                            passed;
            const auto saved = tools.sourceText(wrapper);
            passed = check(tools.canSave() && !saved.contains("MIRRORFLY"),
                         "styled paragraph emits no temporary import/save markers") &&
                passed;
            tools.loadDocument(wrapper, saved, true, theme);
            auto* document = wrapper->textDocument();
            passed = check(document->toRawText() == original && original == "abc",
                         "body/heading style save-reopen preserves every literal character") &&
                passed;
            QJsonArray states;
            for (int position = 0; position < 3; ++position)
            {
                const int expected = (mask >> (position * 3)) & 7;
                states.append(expected);
                passed = check(style_at(*document, position) == expected,
                             "body and heading characters retain independent crossing styles") &&
                    passed;
            }
            const auto state = tools.inspectDocument(wrapper, 0);
            passed =
                check(state.value("quoteLevel").toInt() == (initial.startsWith('>') ? 1 : 0) &&
                        state.value("inList").toBool() == initial.startsWith('-') &&
                        document->begin().blockFormat().headingLevel() == (initial.startsWith('#') ? 1 : 0),
                    "crossing styles preserve the parent paragraph/container type") &&
                passed;
            if (initial.contains("../body"))
                passed = check(tools.inspectDocument(wrapper, 1).value("linkUrl") == "../body" &&
                                 !tools.inspectDocument(wrapper, 0).value("inLink").toBool() &&
                                 !tools.inspectDocument(wrapper, 2).value("inLink").toBool(),
                             "crossing styles preserve link boundaries inside ordinary and heading text") &&
                    passed;
            corpus.append(
                QJsonObject{{"source", saved}, {"initial", initial}, {"text", "abc"}, {"states", states}});
        }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile file(QDir(output).filePath("body-styles.json"));
        const auto bytes = QJsonDocument(corpus).toJson(QJsonDocument::Compact);
        passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                     "export all 3072 actual body/header/container style saves") &&
            passed;
    }
    tools.loadDocument(wrapper, "**bold** *italic*\n", true, theme);
    passed = check(tools.applyEdit(wrapper, 0, 0, "heading", {{"headingLevel", 2}}).value("valid").toBool(),
                 "heading conversion keeps explicit character formatting") &&
        passed;
    const auto heading = tools.sourceText(wrapper);
    tools.loadDocument(wrapper, heading, true, theme);
    passed = check(style_at(*wrapper->textDocument(), 0) == 1 && style_at(*wrapper->textDocument(), 5) == 2,
                 "heading conversion retains explicit bold and italic after reopen") &&
        passed;
    passed = check(tools.applyEdit(wrapper, 0, 0, "paragraph").value("valid").toBool(),
                 "returning to a paragraph clears only implicit heading appearance") &&
        passed;
    const auto plain = tools.sourceText(wrapper);
    tools.loadDocument(wrapper, plain, true, theme);
    passed = check(style_at(*wrapper->textDocument(), 0) == 1 && style_at(*wrapper->textDocument(), 5) == 2,
                 "paragraph conversion retains explicit character styles") &&
        passed;
    tools.loadDocument(wrapper, "abc\n", true, theme);
    QTextCursor foreign(wrapper->textDocument());
    foreign.setPosition(1);
    QTextImageFormat image;
    image.setName("unsupported-image");
    foreign.insertImage(image);
    const auto guarded = tools.sourceText(wrapper);
    passed = check(!tools.canSave() && guarded == "abc\n",
                 "unsupported foreign inline objects retain the last valid source instead of disappearing") &&
        passed;
    return passed;
}
