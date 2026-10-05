#pragma once

#include <QString>
#include <QVariantMap>

class QTextDocument;
class QTextCursor;
class QTextFrame;

namespace mirrorfly
{
    void apply_markdown_inline_style(QTextCursor& cursor, const QString& action);

    void style_markdown_inline_code(QTextDocument& document, const QVariantMap& theme);
    bool markdown_inline_code_range(QTextCursor& cursor);
    bool edit_markdown_inline_code(QTextCursor& cursor, const QString& action, const QVariantMap& theme);
}
