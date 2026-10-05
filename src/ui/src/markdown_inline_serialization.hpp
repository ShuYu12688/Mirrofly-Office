#pragma once

#include <QString>
#include <vector>

class QTextDocument;

namespace mirrorfly
{
    struct MarkdownCodeSpan
    {
        QString token;
        QString source;
    };
    QString markdown_serialization_markers(const QTextDocument& document);
    void prepare_markdown_inline_formats(QTextDocument& document, const QString& markers);
    std::vector<MarkdownCodeSpan> protect_markdown_code_spans(QTextDocument& document);
}
