#include "markdown_link.hpp"
#include "markdown_link_range.hpp"

#include <mirrorfly/markdown.hpp>

#include <QColor>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

#include <algorithm>
#include <vector>

namespace mirrorfly
{
    QVariantMap inspect_markdown_link(QTextCursor cursor)
    {
        const bool linked = markdown_link_range(cursor);
        return {{"inLink", linked}, {"linkUrl", linked ? cursor.charFormat().anchorHref() : QString{}},
            {"linkTitle", linked ? cursor.charFormat().toolTip() : QString{}}};
    }

    bool edit_markdown_link(
        QTextCursor& cursor, const QString& action, const QVariantMap& options, const QVariantMap& theme)
    {
        QString url;
        QString title;
        if (action == "link")
        {
            for (const auto& name : {QStringLiteral("url"), QStringLiteral("title")})
                if (options.contains(name) &&
                    (options.value(name).metaType().id() != QMetaType::QString ||
                        !options.value(name).toString().isValidUtf16()))
                    return false;
            url = options.value("url", "https://example.com").toString();
            title = options.value("title").toString();
            if (markdown_link_suffix(url.toUtf8().toStdString(), title.toUtf8().toStdString()).empty())
                return false;
        }
        if (cursor.document()->findBlock(cursor.selectionStart()) !=
            cursor.document()->findBlock(cursor.selectionEnd() - (cursor.hasSelection() ? 1 : 0)))
            return false;
        if (cursor.selectedText().contains(QChar::LineSeparator))
            return false;
        QTextCursor whole(cursor);
        const bool existing = markdown_link_range(whole);
        if (action == "unlink" && !existing)
            return false;
        if (existing)
            cursor = whole;
        else
        {
            for (auto iterator = cursor.block().begin(); !iterator.atEnd(); ++iterator)
            {
                const auto fragment = iterator.fragment();
                if (fragment.isValid() && fragment.charFormat().isAnchor() &&
                    fragment.position() < cursor.selectionEnd() &&
                    fragment.position() + fragment.length() > cursor.selectionStart())
                    return false;
            }
            if (!cursor.hasSelection())
            {
                const int position = cursor.position();
                cursor.insertText(QStringLiteral("链接文字"));
                cursor.setPosition(position, QTextCursor::KeepAnchor);
            }
        }
        auto identity = cursor.charFormat().property(markdown_link_identity_property).toString();
        if (!existing || identity.isEmpty())
            identity = QUuid::createUuid().toString(QUuid::Id128);
        const int start = cursor.selectionStart();
        const int end = cursor.selectionEnd();
        struct Range
        {
            int start;
            int end;
            QTextCharFormat format;
        };
        std::vector<Range> ranges;
        for (auto iterator = cursor.document()->findBlock(start).begin(); !iterator.atEnd(); ++iterator)
        {
            const auto fragment = iterator.fragment();
            if (!fragment.isValid() || fragment.position() >= end ||
                fragment.position() + fragment.length() <= start)
                continue;
            auto format = fragment.charFormat();
            format.setAnchor(action == "link");
            if (action == "link")
            {
                format.setAnchorHref(url);
                format.setProperty(markdown_link_identity_property, identity);
            }
            else
            {
                format.clearProperty(QTextFormat::AnchorHref);
                format.clearProperty(QTextFormat::AnchorName);
                format.clearProperty(markdown_link_identity_property);
            }
            format.setFontUnderline(action == "link");
            if (title.isEmpty())
                format.clearProperty(QTextFormat::TextToolTip);
            else
                format.setToolTip(title);
            format.setForeground(QColor(theme.value(action == "link" ? "accent" : "textPrimary").toString()));
            ranges.push_back({std::max(start, fragment.position()),
                std::min(end, fragment.position() + fragment.length()), format});
        }
        for (const auto& range : ranges)
        {
            QTextCursor fragment(cursor.document());
            fragment.setPosition(range.start);
            fragment.setPosition(range.end, QTextCursor::KeepAnchor);
            fragment.setCharFormat(range.format);
        }
        return true;
    }

}
