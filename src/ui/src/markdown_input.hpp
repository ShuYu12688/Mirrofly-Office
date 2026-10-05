#pragma once

#include <QVariantMap>

class QTextCursor;
class QTextFrame;

namespace mirrorfly
{
    void normal_markdown_block(QTextCursor& cursor, const QVariantMap& theme);
    bool continue_markdown_heading(QTextCursor& cursor, const QVariantMap& theme);
    bool convert_explicit_markdown_block(QTextCursor& cursor, const QVariantMap& theme);
    void leave_markdown_frame(QTextCursor& cursor, QTextFrame* frame, const QVariantMap& theme);
    QString markdown_selection_region_error(QTextCursor cursor);
}
