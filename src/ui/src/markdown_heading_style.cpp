#include "markdown_blocks.hpp"

#include <QFont>
#include <QTextCursor>
#include <algorithm>

namespace mirrorfly
{
    void style_markdown_heading(const QTextBlock& block, int level, const QVariantMap& theme)
    {
        struct CharacterRange
        {
            int start;
            int end;
            QTextCharFormat format;
        };
        std::vector<CharacterRange> ranges;
        const int size = theme.value(QStringLiteral("editorFontSize"), 16).toInt();
        for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
        {
            const auto fragment = iterator.fragment();
            if (!fragment.isValid())
            {
                continue;
            }
            auto format = fragment.charFormat();
            if (level > 0 && format.fontWeight() < QFont::DemiBold)
            {
                format.setFontWeight(QFont::DemiBold);
            }
            else if (level == 0 && format.fontWeight() == QFont::DemiBold)
                format.setFontWeight(QFont::Normal);
            format.setProperty(
                QTextFormat::FontPixelSize, level > 0 ? size + std::max(2, 18 - level * 4) : size);
            ranges.push_back({fragment.position(), fragment.position() + fragment.length(), format});
        }
        for (const auto& range : ranges)
        {
            QTextCursor characters(block);
            characters.setPosition(range.start);
            characters.setPosition(range.end, QTextCursor::KeepAnchor);
            characters.setCharFormat(range.format);
        }
        if (ranges.empty())
        {
            QTextCursor characters(block);
            QTextCharFormat format;
            format.setFontWeight(level > 0 ? QFont::DemiBold : QFont::Normal);
            format.setProperty(
                QTextFormat::FontPixelSize, level > 0 ? size + std::max(2, 18 - level * 4) : size);
            characters.setBlockCharFormat(format);
        }
    }

}
