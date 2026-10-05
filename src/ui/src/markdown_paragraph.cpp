#include "markdown_paragraph.hpp"
#include "markdown_blocks.hpp"
#include "markdown_code.hpp"
#include "markdown_link_range.hpp"

#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QUuid>

namespace mirrorfly
{
    PreparedMarkdownParagraphs prepare_markdown_paragraphs(const QString& source)
    {
        PreparedMarkdownParagraphs result;
        auto bytes = source.toUtf8().toStdString();
        for (const auto& paragraph : markdown_paragraphs(bytes))
            if (!paragraph.runs.empty() || paragraph.heading)
            {
                QString token;
                do
                {
                    token = "MIRRORFLYPARAGRAPH" + QUuid::createUuid().toString(QUuid::Id128);
                } while (source.contains(token));
                result.tokens.push_back({token, paragraph});
            }
        for (auto iterator = result.tokens.rbegin(); iterator != result.tokens.rend(); ++iterator)
        {
            auto token = iterator->token;
            if (iterator->paragraph.start == iterator->paragraph.end)
                token = " " + token + " ";
            bytes.replace(iterator->paragraph.start, iterator->paragraph.end - iterator->paragraph.start,
                token.toStdString());
        }
        result.source = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
        return result;
    }

    void insert_markdown_runs(QTextCursor& cursor, const std::vector<MarkdownParagraphRun>& runs,
        const QString& identity, const QVariantMap& theme)
    {
        const auto original = cursor.charFormat();
        const auto* table = cursor.currentTable();
        const bool heading =
            cursor.blockFormat().headingLevel() > 0 || (table && table->cellAt(cursor).row() == 0);
        cursor.removeSelectedText();
        for (const auto& run : runs)
        {
            auto format = original;
            format.setFontStyleName({});
            const int normal = heading ? QFont::DemiBold : QFont::Normal;
            format.setFontWeight(run.style.bold ? QFont::Bold : normal);
            format.setFontItalic(run.style.italic);
            format.setFontUnderline(false);
            format.setFontStrikeOut(run.style.strike);
            format.setFontFixedPitch(run.style.code);
            format.setAnchor(run.link_id != 0);
            if (run.link_id != 0)
            {
                format.setAnchorHref(QString::fromStdString(run.url));
                format.setToolTip(QString::fromStdString(run.title));
            }
            else
            {
                format.clearProperty(QTextFormat::AnchorHref);
                format.clearProperty(QTextFormat::TextToolTip);
            }
            if (run.link_id != 0)
                format.setProperty(markdown_link_identity_property, identity + QString::number(run.link_id));
            else
                format.clearProperty(markdown_link_identity_property);
            format.setProperty(markdown_hard_break_property, run.style.hard_break);
            format.setForeground(QColor(theme.value(run.link_id != 0 ? "accent" : "textPrimary").toString()));
            auto text = QString::fromStdString(run.style.text);
            if (run.style.hard_break)
                text = QString(QChar::LineSeparator).repeated(static_cast<qsizetype>(run.style.text.size()));
            cursor.insertText(text, format);
        }
    }

    bool restore_markdown_paragraphs(
        QTextDocument& document, const PreparedMarkdownParagraphs& prepared, const QVariantMap& theme)
    {
        int position = 0;
        for (const auto& token : prepared.tokens)
        {
            auto cursor = document.find(token.token, position, QTextDocument::FindCaseSensitively);
            if (cursor.isNull())
                return false;
            auto heading_format = cursor.blockFormat();
            heading_format.setHeadingLevel(token.paragraph.heading_level);
            cursor.setBlockFormat(heading_format);
            insert_markdown_runs(cursor, token.paragraph.runs, token.token, theme);
            auto format = cursor.blockFormat();
            // Native import may carry a preceding fence format onto the next empty-head list item.
            format.clearProperty(QTextFormat::BlockCodeFence);
            format.clearProperty(QTextFormat::BlockCodeLanguage);
            cursor.setBlockFormat(format);
            set_markdown_containers(cursor.block(), token.paragraph);
            if (token.paragraph.heading && token.paragraph.runs.empty())
                style_markdown_heading(cursor.block(), token.paragraph.heading_level, theme);
            position = cursor.position();
        }
        return true;
    }

}
