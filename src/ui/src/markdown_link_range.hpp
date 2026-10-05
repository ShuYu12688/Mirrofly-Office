#pragma once

#include <QTextFormat>

class QTextCharFormat;
class QTextCursor;

namespace mirrorfly
{
    constexpr int markdown_link_identity_property = QTextFormat::UserProperty + 66;
    bool same_markdown_link(const QTextCharFormat& first, const QTextCharFormat& second);
    bool markdown_link_range(QTextCursor& cursor);
}
