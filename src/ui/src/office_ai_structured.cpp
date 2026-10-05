#include "office_ai_structured.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_workspace.hpp"
#include "spreadsheet_input.hpp"
#include "theme.hpp"

#include <mirrorfly/automation.hpp>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace
{
    QJsonObject failure(const QString& code, const QString& field, const QString& hint)
    {
        return {{"ok", false}, {"error", code}, {"field", field}, {"hint", hint}};
    }

    QJsonObject step(const QString& module, const QString& action, const QJsonArray& args)
    {
        return {{"module", module}, {"action", action}, {"args", args}};
    }

    bool plain_line(const QJsonValue& value, int maximum)
    {
        if (!value.isString())
            return false;
        const QString text = value.toString();
        return !text.trimmed().isEmpty() && text.size() <= maximum && text.isValidUtf16() &&
            !text.contains('\n') && !text.contains('\r') && !text.contains('\t') &&
            !text.contains(QChar(0x2029));
    }

    struct WordLine
    {
        QString text;
        QString kind;
        int start = 0;
    };

    QString cell_text(const QJsonValue& value)
    {
        if (value.isString())
            return value.toString();
        if (value.isDouble())
            return QString::number(value.toDouble(), 'g', 15);
        if (value.isBool())
            return value.toBool() ? "TRUE" : "FALSE";
        return {};
    }
}

namespace mirrorfly
{
    QJsonObject office_ai_word_recipe(const QJsonObject& arguments, const QVariantMap& theme)
    {
        for (const auto& key : arguments.keys())
            if (!QStringList{"title", "subtitle", "sections"}.contains(key))
                return failure("invalid_word_field", key, "Use title, optional subtitle and sections.");
        if (!plain_line(arguments.value("title"), 100))
            return failure("invalid_word_recipe", "title", "Give one short title without a line break.");
        if (arguments.contains("subtitle") && !plain_line(arguments.value("subtitle"), 180))
            return failure("invalid_word_recipe", "subtitle", "Use one short subtitle or omit it.");
        const auto sections = arguments.value("sections");
        if (!sections.isArray() || sections.toArray().isEmpty() || sections.toArray().size() > 8)
            return failure("invalid_word_recipe", "sections", "Provide 1..8 structured sections.");
        QVector<WordLine> lines{{arguments.value("title").toString(), "title", 0}};
        if (arguments.contains("subtitle"))
            lines.append({arguments.value("subtitle").toString(), "subtitle", 0});
        for (int index = 0; index < sections.toArray().size(); ++index)
        {
            const auto value = sections.toArray().at(index);
            const auto section = value.toObject();
            if (!value.isObject() || !plain_line(section.value("heading"), 80))
                return failure("invalid_word_recipe", QStringLiteral("sections[%1].heading").arg(index),
                    "Each section needs a short heading.");
            for (const auto& key : section.keys())
                if (!QStringList{"heading", "paragraphs", "bullets"}.contains(key))
                    return failure("invalid_word_field", key,
                        "A section accepts heading, paragraphs and optional bullets.");
            const auto paragraphs = section.value("paragraphs");
            const auto bullets = section.value("bullets");
            if (!paragraphs.isArray() || paragraphs.toArray().size() > 8 ||
                (section.contains("bullets") && (!bullets.isArray() || bullets.toArray().size() > 8)) ||
                paragraphs.toArray().isEmpty() && bullets.toArray().isEmpty())
                return failure("invalid_word_recipe", QStringLiteral("sections[%1]").arg(index),
                    "Give 1..8 paragraphs or bullets per section.");
            lines.append({section.value("heading").toString(), "heading", 0});
            for (int item = 0; item < paragraphs.toArray().size(); ++item)
            {
                const auto paragraph = paragraphs.toArray().at(item);
                if (!plain_line(paragraph, 450))
                    return failure("invalid_word_recipe",
                        QStringLiteral("sections[%1].paragraphs[%2]").arg(index).arg(item),
                        "Use one paragraph of at most 450 characters without line breaks.");
                lines.append({paragraph.toString(), "body", 0});
            }
            for (int item = 0; item < bullets.toArray().size(); ++item)
            {
                const auto bullet = bullets.toArray().at(item);
                if (!plain_line(bullet, 200))
                    return failure("invalid_word_recipe",
                        QStringLiteral("sections[%1].bullets[%2]").arg(index).arg(item),
                        "Use one concise bullet of at most 200 characters.");
                lines.append({bullet.toString(), "bullet", 0});
            }
        }
        QString content;
        for (auto& line : lines)
        {
            if (!content.isEmpty())
                content += '\n';
            line.start = content.size();
            content += line.text;
        }
        if (content.toUtf8().size() > 32768 || lines.size() > 90)
            return failure("invalid_word_recipe", "sections", "Split documents longer than 32 KiB.");
        QJsonArray steps{step("word", "insertText", {0, 0, content})};
        const QString accent = theme.value("wordAccent").toString();
        const QString muted = theme.value("textSecondary").toString();
        for (const auto& line : lines)
        {
            const int end = line.start + line.text.size();
            const auto format = [&](const QString& action, const QJsonValue& value)
            {
                steps.append(step("word", "format", {line.start, end, action, value}));
            };
            if (line.kind == "title")
            {
                format("heading", 1);
                format("size", 28);
                format("align", 1);
                format("spaceAfter", 16);
                format("color", accent);
            }
            else if (line.kind == "subtitle")
            {
                format("size", 14);
                format("align", 1);
                format("spaceAfter", 18);
                format("color", muted);
            }
            else if (line.kind == "heading")
            {
                format("heading", 2);
                format("spaceBefore", 14);
                format("color", accent);
            }
            else
            {
                format("spacing", line.kind == "bullet" ? 1.25 : 1.4);
                format("spaceAfter", line.kind == "bullet" ? 3 : 8);
                if (line.kind == "bullet")
                    format("list", 1);
            }
        }
        return {{"ok", true}, {"steps", steps}, {"expectedText", content}, {"paragraphCount", lines.size()},
            {"sectionCount", sections.toArray().size()}};
    }

    QJsonObject office_ai_table_recipe(const QJsonObject& arguments, const QVariantMap& theme)
    {
        for (const auto& key : arguments.keys())
            if (!QStringList{"title", "columns", "rows", "totalRow", "columnWidth"}.contains(key))
                return failure("invalid_table_field", key,
                    "Use title, columns, rows, optional totalRow and columnWidth.");
        if (!plain_line(arguments.value("title"), 31) ||
            arguments.value("title").toString().startsWith('\'') ||
            arguments.value("title").toString().endsWith('\'') ||
            arguments.value("title").toString().contains(QRegularExpression("[\\[\\]:*?/\\\\]")))
            return failure("invalid_table_recipe", "title",
                "Give a worksheet name of at most 31 characters without []:*?/\\.");
        const auto columns = arguments.value("columns");
        const auto rows = arguments.value("rows");
        if (!columns.isArray() || columns.toArray().isEmpty() || columns.toArray().size() > 12)
            return failure("invalid_table_recipe", "columns", "Provide 1..12 column headings.");
        if (!rows.isArray() || rows.toArray().isEmpty() || rows.toArray().size() > 100)
            return failure("invalid_table_recipe", "rows", "Provide 1..100 data rows.");
        if (arguments.contains("totalRow") && !arguments.value("totalRow").isBool())
            return failure("invalid_table_recipe", "totalRow", "Use true or false.");
        const auto width = arguments.value("columnWidth");
        if (arguments.contains("columnWidth") &&
            (!width.isDouble() || width.toDouble() < 8 || width.toDouble() > 40))
            return failure("invalid_table_recipe", "columnWidth", "Use a width from 8 to 40 characters.");
        QStringList lines;
        QStringList headers;
        for (int column = 0; column < columns.toArray().size(); ++column)
        {
            const auto heading = columns.toArray().at(column);
            if (!plain_line(heading, 40) || heading.toString().contains('\t'))
                return failure("invalid_table_recipe", QStringLiteral("columns[%1]").arg(column),
                    "Each heading must fit on one line and contain no tabs.");
            headers.append(spreadsheet_quote_cell(heading.toString()));
        }
        lines.append(headers.join('\t'));
        for (int row = 0; row < rows.toArray().size(); ++row)
        {
            const auto value = rows.toArray().at(row);
            if (!value.isArray() || value.toArray().size() != columns.toArray().size())
                return failure("invalid_table_recipe", QStringLiteral("rows[%1]").arg(row),
                    "Every row must contain exactly one value for each heading.");
            QStringList cells;
            for (int column = 0; column < value.toArray().size(); ++column)
            {
                const auto item = value.toArray().at(column);
                const QString input = cell_text(item);
                SpreadsheetValue parsed;
                QString error;
                if ((!item.isString() && !item.isDouble() && !item.isBool()) || input.size() > 200 ||
                    input.contains('\t') || input.contains('\n') || input.contains('\r') ||
                    !spreadsheet_input_value(input, "auto", parsed, error))
                    return failure("invalid_table_recipe",
                        QStringLiteral("rows[%1][%2]").arg(row).arg(column),
                        error.isEmpty() ? "Use a short value or a supported formula." : error);
                cells.append(spreadsheet_quote_cell(input));
            }
            lines.append(cells.join('\t'));
        }
        const QString tsv = lines.join('\n');
        QVector<QStringList> checked;
        QString paste_error;
        if (!spreadsheet_paste_rows(tsv, checked, paste_error) || checked.size() != lines.size())
            return failure("invalid_table_recipe", "rows", paste_error);
        const int data_last_row = 1 + rows.toArray().size();
        const QString last_column = QString(QChar(static_cast<int>('A' + columns.toArray().size() - 1)));
        const QString table_range = QStringLiteral("A1:%1%2").arg(last_column).arg(data_last_row);
        const QJsonObject table_palette{{"header", theme.value("sheetsHeaderFill").toString()},
            {"body", theme.value("sheetsTableBodyFill").toString()},
            {"alternate", theme.value("sheetsBandFill").toString()},
            {"headerText", theme.value("sheetsHeaderText").toString()},
            {"bodyText", theme.value("sheetsTableBodyText").toString()}};
        QJsonArray steps{step("sheets", "renameSheet", {arguments.value("title")}),
            step("sheets", "selectAddress", {"A1"}), step("sheets", "pasteText", {tsv}),
            step("sheets", "selectAddress", {table_range}),
            step("sheets", "styleSelection", {"banded", table_palette}),
            step("sheets", "resizeSelection", {true, width.isDouble() ? width.toDouble() : 19.0})};
        if (arguments.value("totalRow").toBool())
        {
            const QJsonObject total_palette{{"fill", theme.value("sheetsSurface").toString()},
                {"text", theme.value("sheetsTableBodyText").toString()},
                {"line", theme.value("sheetsStrongLine").toString()}};
            const QString total_range =
                QStringLiteral("A%1:").arg(data_last_row) + last_column + QString::number(data_last_row);
            steps.append(step("sheets", "selectAddress", {total_range}));
            steps.append(step("sheets", "styleSelection", {"total", total_palette}));
        }
        steps.append(step("sheets", "selectAddress", {table_range}));
        return {{"ok", true}, {"steps", steps}, {"rowCount", rows.toArray().size() + 1},
            {"columnCount", columns.toArray().size()}, {"expectedTitle", arguments.value("title")},
            {"firstHeader", columns.toArray().first()},
            {"lastValue", cell_text(rows.toArray().last().toArray().last())}};
    }

    QJsonObject OfficeAiToolbox::prepareStructured(const QString& name, const QJsonObject& arguments) const
    {
        const QString module = name == "office_compose_word" ? "word" : "sheets";
        if (!loaded_groups_.contains(module))
            return failure("group_not_loaded", module, "Load the current document group first.");
        const auto runtime = QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot())).object();
        const auto workspace = office_ai_workspace(runtime);
        const auto document = runtime.value("modules").toObject().value(module).toObject();
        const auto snapshot = document.value("snapshot").toObject();
        const auto selected_cells = snapshot.value("cells").toArray();
        if (!workspace.value("ok").toBool() || workspace.value("currentModule") != module ||
            !document.value("active").toBool() || document.value("locked").toBool() ||
            document.value("modified").toBool())
            return failure("new_document_required", module,
                "Create a fresh editable document before composing; read and edit existing files precisely.");
        if (module == "word" &&
            (document.value("readOnly").toBool() || snapshot.value("paragraphs").toInt() != 1 ||
                document.value("documentName") != QStringLiteral("未命名.docx") ||
                !snapshot.value("plainText").toString().isEmpty()))
        {
            auto result = failure(
                "new_document_required", module, "Compose Word only in a fresh empty editable document.");
            result.insert("documentName", document.value("documentName"));
            result.insert("paragraphs", snapshot.value("paragraphs"));
            result.insert("textLength", snapshot.value("plainText").toString().size());
            result.insert("readOnly", document.value("readOnly"));
            return result;
        }
        if (module == "sheets" &&
            (!document.value("documentPath").toString().isEmpty() ||
                document.value("sheetNames").toArray().size() != 1 ||
                document.value("currentSheet").toInt() != 0 ||
                (!selected_cells.isEmpty() &&
                    !selected_cells.first().toObject().value("text").toString().isEmpty())))
            return failure(
                "new_document_required", module, "Compose a table only in a fresh empty one-sheet workbook.");
        const auto theme = load_theme(QCoreApplication::applicationDirPath()).values;
        auto content = arguments;
        content.remove("expectedRevision");
        auto plan =
            module == "word" ? office_ai_word_recipe(content, theme) : office_ai_table_recipe(content, theme);
        if (!plan.value("ok").toBool())
            return plan;
        QJsonObject receipt = plan;
        receipt.remove("ok");
        receipt.remove("steps");
        receipt.insert("operation", module == "word" ? "compose_word" : "compose_table");
        return {{"ok", true}, {"steps", plan.value("steps")}, {"receipt", receipt}};
    }
}
