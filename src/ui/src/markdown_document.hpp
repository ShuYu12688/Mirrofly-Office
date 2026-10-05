#pragma once

#include <QString>
#include <QVariantMap>

#include <memory>

class QTextDocument;

namespace mirrorfly
{
    constexpr int maximum_markdown_bytes = 128 * 1024;
    struct MarkdownSerialization
    {
        bool valid = false;
        QString source;
        QString error;
    };

    std::unique_ptr<QTextDocument> create_editor_document();
    QString markdown_support_error(const QString& source);
    QString load_markdown_document(QTextDocument& document, const QString& source, const QVariantMap& theme);
    MarkdownSerialization serialize_markdown_document(const QTextDocument& document);
    QVariantMap edit_markdown_document(QTextDocument& document, int start, int end, const QString& action,
        const QVariantMap& options, const QVariantMap& theme);
    QVariantMap inspect_markdown_document(QTextDocument& document, int position);
}
