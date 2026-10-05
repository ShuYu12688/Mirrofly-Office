#pragma once

#include "markdown_break.hpp"
#include "markdown_container.hpp"

#include <QVariantMap>
#include <mirrorfly/markdown.hpp>

class QTextCursor;
class QTextDocument;
class QTextFragment;

namespace mirrorfly
{
    struct MarkdownParagraphToken
    {
        QString token;
        MarkdownParagraphInfo paragraph;
    };
    struct PreparedMarkdownParagraphs
    {
        QString source;
        std::vector<MarkdownParagraphToken> tokens;
    };
    struct ProtectedMarkdownParagraphs
    {
        bool valid = false;
        std::vector<MarkdownInlineToken> tokens;
    };
    PreparedMarkdownParagraphs prepare_markdown_paragraphs(const QString& source);
    bool restore_markdown_paragraphs(
        QTextDocument& document, const PreparedMarkdownParagraphs& prepared, const QVariantMap& theme);
    void insert_markdown_runs(QTextCursor& cursor, const std::vector<MarkdownParagraphRun>& runs,
        const QString& identity, const QVariantMap& theme);
    std::vector<MarkdownInlineRun> markdown_fragment_runs(const QTextFragment& fragment);
    ProtectedMarkdownParagraphs protect_markdown_paragraphs(
        QTextDocument& document, const QString& reserved_markers, bool table_cells = false);
}
