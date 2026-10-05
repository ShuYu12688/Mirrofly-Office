#pragma once
#include "markdown_break.hpp"

#include <QStringList>
#include <QVariantMap>
#include <vector>

class QTextCursor;
class QTextBlock;
class QTextDocument;
class QTextFrame;
class QTextTable;

namespace mirrorfly
{
    struct PreparedMarkdownTables
    {
        QString source;
        QStringList owner_tokens;
    };
    PreparedMarkdownTables prepare_markdown_table_containers(const QString& source);
    void preserve_markdown_empty_blocks(QTextDocument& copy, const QTextDocument& original);
    struct MarkdownTableToken
    {
        QString token;
        QString owner_token;
        QStringList alignments;
        int rows;
        QString prefix;
        int outer_quotes = 0;
    };
    void style_markdown_table(QTextTable* table, const QVariantMap& theme);
    void load_markdown_table_alignments(QTextDocument& document, const QString& source);
    QString markdown_table_alignment(QTextTable* table, int column);
    bool align_markdown_table(QTextCursor& cursor, const QVariantMap& options);
    int markdown_table_quote_level(QTextTable* table);
    int markdown_table_quote_minimum(QTextTable* table);
    bool set_markdown_table_quote(QTextTable* table, int level, const QVariantMap& theme);
    bool markdown_table_owned_by(QTextTable* table, const QTextBlock& owner);
    bool can_change_markdown_owner_quote(const QTextBlock& owner, int previous, int level);
    void change_markdown_owner_quote(
        const QTextBlock& owner, int previous, int level, const QVariantMap& theme);
    void refresh_markdown_table_containers(QTextDocument& document, const QVariantMap& theme);
    void insert_markdown_table(QTextCursor& cursor, const QVariantMap& options, const QVariantMap& theme);
    void resize_markdown_table(QTextCursor& cursor, const QString& action, const QVariantMap& theme);
    bool navigate_markdown_table(QTextCursor& cursor, const QString& action, const QVariantMap& theme);
    QString markdown_table_structure_error(QTextFrame* frame);
    void pad_markdown_table_cells(QTextFrame* frame, QChar marker);
    std::vector<MarkdownTableToken> protect_markdown_table_alignments(QTextDocument& document);
    bool restore_markdown_table_alignments(QString& source, const std::vector<MarkdownTableToken>& tables,
        std::vector<MarkdownInlineToken>* bodies = nullptr);
}
