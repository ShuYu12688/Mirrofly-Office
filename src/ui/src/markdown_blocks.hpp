#pragma once

#include <QTextBlock>
#include <QVariantMap>
#include <vector>

class QTextCursor;
class QTextDocument;

namespace mirrorfly
{
    std::vector<QTextBlock> selected_markdown_blocks(const QTextCursor& cursor);
    void style_markdown_heading(const QTextBlock& block, int level, const QVariantMap& theme);
    void apply_markdown_block(
        QTextCursor& cursor, const QString& action, const QVariantMap& options, const QVariantMap& theme);
    void apply_markdown_list(QTextCursor& cursor, const QString& action);
    bool edit_markdown_list(QTextCursor& cursor, const QString& action, const QVariantMap& options);
    QVariantMap inspect_markdown_list(const QTextCursor& cursor);
    bool set_markdown_quote(QTextCursor& cursor, int level, const QVariantMap& theme);
}
