#pragma once

#include <QString>
#include <vector>

class QTextDocument;

namespace mirrorfly
{
    struct MarkdownLinkToken
    {
        QString label_token;
        QString label;
        QString suffix;
    };
    struct ProtectedMarkdownLinks
    {
        bool valid = false;
        std::vector<MarkdownLinkToken> tokens;
    };
    ProtectedMarkdownLinks protect_markdown_links(QTextDocument& document);
    bool restore_markdown_links(QString& source, const std::vector<MarkdownLinkToken>& tokens);
}
