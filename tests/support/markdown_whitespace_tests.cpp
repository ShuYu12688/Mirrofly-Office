#include "markdown_whitespace_tests.hpp"
#include "editor_tools.hpp"
#include <QDir>
#include <QFile>
#include <QFont>
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
    int style_at(QTextDocument& document, int position)
    {
        QTextCursor cursor(&document);
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        const auto format = cursor.charFormat();
        return (format.fontWeight() >= QFont::Bold ? 1 : 0) | (format.fontItalic() ? 2 : 0) |
            (format.fontStrikeOut() ? 4 : 0);
    }
    QString encoded(const QString& text)
    {
        QString source;
        for (const auto point : text.toUcs4())
            source += "&#" + QString::number(point) + ';';
        return source;
    }
}
bool test_markdown_whitespace(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    const QStringList values{"  ", "    lead  ", "\t middle\t", QString::fromUtf8(u8"　空  "), " a  b "};
    const QStringList contexts{"%1\n", "# %1\n", "> %1\n", "- %1\n- sibling\n", "- owner\n\n  %1\n\n- tail\n",
        "[%1](../white \"tip\")\n", "> - [%1](../white \"tip\")\n> - tail\n",
        "| header |\n| --- |\n| %1 |\n"};
    const std::vector<std::pair<int, QString>> actions{{1, "bold"}, {2, "italic"}, {4, "strike"}};
    bool passed = true;
    QJsonArray corpus;
    for (const auto& context : contexts)
        for (const auto& value : values)
            for (int mask = 1; mask < 8; ++mask)
            {
                const auto original = context.arg(encoded(value));
                if (!check(tools.loadDocument(wrapper, original, true, theme).value("valid").toBool(),
                        "whitespace fixture imports inside the selected parent"))
                    return false;
                auto* document = wrapper->textDocument();
                const auto raw = document->toRawText();
                const int start = raw.indexOf(value);
                if (!check(start >= 0, "import preserves exact Unicode spaces and tabs"))
                    return false;
                QString previous;
                for (const auto& action : actions)
                    if (mask & action.first)
                    {
                        previous = tools.sourceText(wrapper);
                        passed = check(tools.applyEdit(wrapper, start, start + value.size(), action.second)
                                           .value("valid")
                                           .toBool(),
                                     "public style action accepts whitespace selection") &&
                            passed;
                    }
                const auto saved = tools.sourceText(wrapper);
                if (!check(tools.canSave(), "styled whitespace remains serializable"))
                {
                    std::cerr << original.toStdString() << '\n';
                    return false;
                }
                document->undo();
                passed = check(tools.sourceText(wrapper) == previous,
                             "one undo restores previous whitespace style") &&
                    passed;
                document->redo();
                passed =
                    check(tools.sourceText(wrapper) == saved, "redo preserves whitespace styling source") &&
                    passed;
                passed = check(tools.loadDocument(wrapper, saved, true, theme).value("valid").toBool(),
                             "styled whitespace reopens") &&
                    passed;
                document = wrapper->textDocument();
                if (!check(document->toRawText() == raw, "style save preserves all whitespace characters"))
                {
                    std::cerr << "original=" << original.toStdString() << " saved=" << saved.toStdString()
                              << " raw=" << raw.toUtf8().toHex().toStdString()
                              << " reopened=" << document->toRawText().toUtf8().toHex().toStdString() << '\n';
                    return false;
                }
                for (int position = start; position < start + value.size(); ++position)
                    passed = check(style_at(*document, position) == mask,
                                 "every selected character including whitespace retains exact styles") &&
                        passed;
                for (const auto& action : actions)
                    if (mask & action.first)
                        passed = check(tools.applyEdit(wrapper, start, start + value.size(), action.second)
                                           .value("valid")
                                           .toBool(),
                                     "same parent style action cancels whitespace style") &&
                            passed;
                const auto plain = tools.sourceText(wrapper);
                corpus.append(QJsonObject{{"original", original}, {"source", saved}, {"plain", plain},
                    {"selected", value}, {"mask", mask}});
            }
    QJsonArray source_corpus;
    for (const auto& value : values)
        for (const auto& prefix :
            {QStringLiteral("L"), QStringLiteral("\\L"), QStringLiteral("\\\\L"), QString::fromUtf8(u8"甲")})
            for (const auto& action : actions)
            {
                const auto original = QString::fromUtf8(u8"> - 🦋") + prefix + value + "R\n> - tail\n";
                const auto bytes = original.toUtf8().toStdString();
                const auto selected = value.toUtf8().toStdString();
                const auto start = bytes.find(selected);
                const auto edit = mirrorfly::make_markdown_edit(
                    bytes, start, start + selected.size(), action.second.toStdString());
                if (!check(edit.valid,
                        "source whitespace action accepts word-adjacent Unicode and escaped neighbors"))
                    return false;
                auto changed = bytes;
                changed.replace(edit.start, edit.end - edit.start, edit.replacement);
                source_corpus.append(
                    QJsonObject{{"original", original}, {"source", QString::fromStdString(changed)},
                        {"selected", value}, {"mask", action.first}});
            }
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QFile source_file(QDir(output).filePath("whitespace-source.json"));
        passed = check(source_file.open(QIODevice::WriteOnly) &&
                         source_file.write(QJsonDocument(source_corpus).toJson()) > 0,
                     "export actual source whitespace transactions") &&
            passed;
        QFile file(QDir(output).filePath("whitespace-styles.json"));
        passed = check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(corpus).toJson()) > 0,
                     "export actual whitespace-style transactions") &&
            passed;
    }
    return passed;
}
