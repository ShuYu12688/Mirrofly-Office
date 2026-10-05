#pragma once

#include <QString>
#include <QTextFormat>
#include <vector>

class QTextCursor;
class QTextDocument;

namespace mirrorfly
{
    constexpr int markdown_hard_break_property = QTextFormat::UserProperty + 67;
    struct MarkdownInlineToken
    {
        QString token;
        QString source;
    };
    struct PreparedMarkdownBreaks
    {
        QString source;
        std::vector<QString> tokens;
    };
    PreparedMarkdownBreaks prepare_markdown_hard_breaks(const QString& source);
    bool restore_markdown_hard_break_import(QTextDocument& document, const PreparedMarkdownBreaks& prepared);
    std::vector<MarkdownInlineToken> protect_markdown_hard_breaks(
        QTextDocument& document, const QString& reserved_markers);
    bool restore_markdown_inline_tokens(QString& source, const std::vector<MarkdownInlineToken>& tokens);
    bool can_insert_markdown_hard_break(QTextCursor cursor);
}
