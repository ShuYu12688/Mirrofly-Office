#pragma once

#include <QHash>
#include <QString>
#include <QVariantMap>
#include <vector>

class QTextBlock;
class QTextCursor;
class QTextDocument;
class QTextFrame;
class QTextFrameFormat;

namespace mirrorfly
{
    struct PreparedMarkdown
    {
        QString source;
        QHash<QString, QString> languages;
        QHash<QString, QString> original_suffixes;
    };

    struct CodeRange
    {
        int start;
        int end;
        QString language;
        int quote_level;
        QString text;
    };

    QString language_name(QString language);
    void insert_markdown_code_frame(
        QTextCursor& cursor, const QVariantMap& options, const QVariantMap& theme);
    PreparedMarkdown separate_fences(const QString& source);
    void restore_literal_fences(QTextDocument& document, const PreparedMarkdown& prepared);
    bool code_block(const QTextBlock& block);
    QTextFrame* code_frame(QTextCursor cursor);
    QTextFrameFormat frame_style(const QVariantMap& theme);
    void style_code_frame(
        QTextFrame* frame, const QString& language, const QVariantMap& theme, int quote_level = -1);
    std::vector<CodeRange> code_ranges(const QTextDocument& document);
    QString fenced_text(const QString& text, const QString& language, const QString& prefix);
}
