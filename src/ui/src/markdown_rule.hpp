#pragma once

#include "markdown_break.hpp"

#include <QVariantMap>

class QTextCursor;
class QTextDocument;

namespace mirrorfly
{
    bool can_insert_markdown_rule(const QTextCursor& cursor);
    void insert_markdown_rule(QTextCursor& cursor, const QVariantMap& theme);
    std::vector<MarkdownInlineToken> protect_markdown_rules(QTextDocument& document);
}
