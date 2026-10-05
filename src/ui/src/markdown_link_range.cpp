#include "markdown_link_range.hpp"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace
{
    QTextCharFormat format_at(QTextDocument* document, int position)
    {
        QTextCursor cursor(document);
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        return cursor.charFormat();
    }

}

namespace mirrorfly
{
    bool same_markdown_link(const QTextCharFormat& first, const QTextCharFormat& second)
    {
        return first.isAnchor() && second.isAnchor() && first.anchorHref() == second.anchorHref() &&
            first.toolTip() == second.toolTip() &&
            first.property(markdown_link_identity_property) ==
            second.property(markdown_link_identity_property);
    }

    bool markdown_link_range(QTextCursor& cursor)
    {
        auto* document = cursor.document();
        const int position = cursor.selectionStart();
        const auto block = document->findBlock(position);
        const int block_end = block.position() + block.length() - 1;
        const int probe = position < block_end ? position : position - 1;
        if (probe < block.position())
            return false;
        const auto format = format_at(document, probe);
        if (!format.isAnchor())
            return false;
        int start = block.position();
        int end = start;
        bool found = false;
        for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
        {
            const auto fragment = iterator.fragment();
            if (!fragment.isValid())
                continue;
            if (!same_markdown_link(format, fragment.charFormat()))
            {
                if (found)
                    break;
                start = fragment.position() + fragment.length();
            }
            else
            {
                end = fragment.position() + fragment.length();
                found = found || (probe >= fragment.position() && probe < end);
            }
        }
        if (!found || cursor.selectionEnd() > end)
            return false;
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        return true;
    }

}
