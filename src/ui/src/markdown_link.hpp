#pragma once

#include <QVariantMap>

#include <mirrorfly/markdown.hpp>

class QTextCursor;
class QTextDocument;

namespace mirrorfly
{
    struct MarkdownLinkImportToken
    {
        QString placeholder;
        MarkdownLinkInfo link;
    };
    struct PreparedMarkdownLinks
    {
        QString source;
        std::vector<MarkdownLinkImportToken> tokens;
    };
    PreparedMarkdownLinks prepare_markdown_links(const QString& source);
    bool restore_markdown_link_import(
        QTextDocument& document, const PreparedMarkdownLinks& prepared, const QVariantMap& theme);
    QVariantMap inspect_markdown_link(QTextCursor cursor);
    bool edit_markdown_link(
        QTextCursor& cursor, const QString& action, const QVariantMap& options, const QVariantMap& theme);

}
