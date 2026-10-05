#include "word_paragraph_format.hpp"
#include "word_distribution.hpp"
#include "word_format_properties.hpp"
#include "word_tabs.hpp"
#include "word_units.hpp"
#include <QColor>
#include <QTextBlock>
#include <algorithm>

namespace mirrorfly
{
    constexpr int source_paragraph_property = word_source_paragraph_property;
    constexpr int border_color_property = word_paragraph_border_property;
    constexpr int border_bottom_property = QTextFormat::UserProperty + 32;
    int word_alignment_index(Qt::Alignment value)
    {
        if (value.testFlag(Qt::AlignHCenter))
            return 1;
        if (value.testFlag(Qt::AlignRight))
            return 2;
        if (value.testFlag(Qt::AlignJustify))
            return 3;
        return 0;
    }
    Qt::Alignment word_alignment(int value)
    {
        switch (value)
        {
        case 1:
            return Qt::AlignHCenter;
        case 2:
            return Qt::AlignRight;
        case 3:
            return Qt::AlignJustify;
        default:
            return Qt::AlignLeft;
        }
    }

    QTextBlockFormat word_paragraph_format(const mirrorfly::WordParagraph& paragraph)
    {
        QTextBlockFormat block;
        block.setProperty(source_paragraph_property, QVariant::fromValue<qulonglong>(paragraph.source_id));
        block.setRightMargin(word_points_to_pixels(paragraph.right_indent));
        if (paragraph.page_break_before)
            block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        block.setHeadingLevel(paragraph.heading);
        block.setAlignment(word_alignment(paragraph.alignment));
        block.setProperty(mirrorfly::word_distributed_property, paragraph.alignment == 4);
        block.setLayoutDirection(paragraph.right_to_left ? Qt::RightToLeft : Qt::LeftToRight);
        block.setLineHeight(paragraph.line_spacing * 100, QTextBlockFormat::ProportionalHeight);
        if (paragraph.line_spacing_rule)
        {
            auto height_type = QTextBlockFormat::MinimumHeight;
            if (paragraph.line_spacing_rule == 1)
                height_type = QTextBlockFormat::FixedHeight;
            block.setLineHeight(word_points_to_pixels(paragraph.line_spacing_points), height_type);
        }
        block.setLeftMargin(word_points_to_pixels(paragraph.left_indent));
        block.setTextIndent(word_points_to_pixels(paragraph.first_line_indent));
        block.setTopMargin(word_points_to_pixels(paragraph.space_before));
        for (const auto& run : paragraph.runs)
            if (!run.ruby.empty())
            {
                block.setLineHeight(200, QTextBlockFormat::ProportionalHeight);
                block.setTopMargin(std::max(block.topMargin(), run.size * 2 / 3));
            }
        block.setBottomMargin(word_points_to_pixels(paragraph.space_after));
        if (!paragraph.background.empty())
            block.setBackground(QColor(QString::fromStdString(paragraph.background)));
        block.setProperty(border_color_property, QString::fromStdString(paragraph.border_color));
        block.setProperty(border_bottom_property, paragraph.border_bottom_only);
        apply_word_tabs(block, paragraph.tabs);
        return block;
    }

    bool word_indent_range_valid(
        QTextDocument& document, const QTextCursor& cursor, bool setting_left, double value)
    {
        const int last_position = std::max(cursor.selectionStart(), cursor.selectionEnd() - 1);
        for (auto item = document.findBlock(cursor.selectionStart()); item.isValid(); item = item.next())
        {
            const auto format = item.blockFormat();
            const double left = setting_left ? value : word_pixels_to_points(format.leftMargin());
            const double first_line = setting_left ? word_pixels_to_points(format.textIndent()) : value;
            if (left + first_line < 0)
            {
                return false;
            }
            if (item.position() + item.length() - 1 >= last_position)
            {
                break;
            }
        }
        return true;
    }
    void extract_word_paragraph_format(const QTextBlockFormat& format, WordParagraph& paragraph)
    {
        paragraph.right_indent = word_pixels_to_points(format.rightMargin());
        paragraph.page_break_before = format.pageBreakPolicy().testFlag(QTextFormat::PageBreak_AlwaysBefore);
        paragraph.line_spacing_rule = 0;
        if (format.lineHeightType() == QTextBlockFormat::FixedHeight)
            paragraph.line_spacing_rule = 1;
        else if (format.lineHeightType() == QTextBlockFormat::MinimumHeight)
            paragraph.line_spacing_rule = 2;
        paragraph.line_spacing_points =
            paragraph.line_spacing_rule ? word_pixels_to_points(format.lineHeight()) : 0;
        paragraph.heading = format.headingLevel();
        paragraph.alignment = 0;
        if (format.property(word_distributed_property).toBool())
        {
            paragraph.alignment = 4;
        }
        else if (format.alignment().testFlag(Qt::AlignHCenter))
        {
            paragraph.alignment = 1;
        }
        else if (format.alignment().testFlag(Qt::AlignRight))
        {
            paragraph.alignment = 2;
        }
        else if (format.alignment().testFlag(Qt::AlignJustify))
        {
            paragraph.alignment = 3;
        }
        paragraph.line_spacing = format.lineHeightType() == QTextBlockFormat::ProportionalHeight
            ? format.lineHeight() / 100
            : paragraph.line_spacing;
        paragraph.left_indent = word_pixels_to_points(format.leftMargin());
        paragraph.first_line_indent = word_pixels_to_points(format.textIndent());
        paragraph.space_before = word_pixels_to_points(format.topMargin());
        paragraph.space_after = word_pixels_to_points(format.bottomMargin());
        paragraph.background.clear();
        if (format.background().style() != Qt::NoBrush)
            paragraph.background = format.background().color().name().toStdString();
        paragraph.border_color = format.property(border_color_property).toString().toStdString();
        paragraph.border_bottom_only = format.property(border_bottom_property).toBool();
        paragraph.right_to_left = format.layoutDirection() == Qt::RightToLeft;
        paragraph.tabs = word_tabs_from_format(format);
    }
}
