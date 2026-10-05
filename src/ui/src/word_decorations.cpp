#include "word_annotations.hpp"
#include "word_character.hpp"
#include "word_document.hpp"
#include "word_editor_document.hpp"
#include "word_format_properties.hpp"
#include "word_tabs.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QPainter>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    namespace
    {
        constexpr int border_color_property = word_paragraph_border_property;
        constexpr int border_bottom_property = QTextFormat::UserProperty + 32;
        constexpr int character_border_property = word_character_border_property;

        void append_text_lines(QVariantList& result, const QTextDocument& document, const QTextBlock& block,
            const QTextFragment& fragment, const QRectF& bounds)
        {
            const auto format = fragment.charFormat();
            const bool underline = format.property(word_double_underline_property).toBool();
            const bool strike = format.property(word_double_strike_property).toBool();
            const auto* layout = block.layout();
            if ((!underline && !strike) || !layout)
                return;
            auto color = word_character_color(format);
            if (color.isEmpty())
                color = "#000000";
            const auto start = fragment.position() - block.position();
            const auto end = start + fragment.length();
            for (int index = 0; index < layout->lineCount(); ++index)
            {
                const auto line = layout->lineAt(index);
                const auto first = std::max(start, line.textStart());
                const auto last = std::min(end, line.textStart() + line.textLength());
                if (last <= first)
                    continue;
                // Shaped selection bounds disambiguate the leading edge at bidi boundaries.
                const auto glyphs = line.glyphRuns(first, last - first, QTextLayout::RetrieveGlyphPositions);
                std::vector<std::pair<qreal, qreal>> spans;
                for (const auto& glyph : glyphs)
                {
                    const auto rect = glyph.boundingRect();
                    if (rect.width() > 0.01)
                        spans.emplace_back(rect.left(), rect.right());
                }
                // Tabs have no glyph run, but selected tab space is part of the line decoration.
                const auto text = block.text();
                for (int position = first; position < last; ++position)
                    if (text.at(position) == QChar::Tabulation)
                    {
                        const auto a = line.cursorToX(position);
                        const auto b = line.cursorToX(position + 1);
                        spans.emplace_back(std::min(a, b), std::max(a, b));
                    }
                std::sort(spans.begin(), spans.end());
                std::vector<std::pair<qreal, qreal>> merged;
                for (const auto& span : spans)
                {
                    if (!merged.empty() && span.first <= merged.back().second + 0.01)
                        merged.back().second = std::max(merged.back().second, span.second);
                    else
                        merged.push_back(span);
                }
                auto font = format.font().resolve(document.defaultFont());
                qreal baseline = line.y() + line.ascent();
                if (!glyphs.isEmpty() && !glyphs.front().positions().isEmpty())
                {
                    // Glyph positions omit the vertical adjustment applied by QTextLine::draw.
                    baseline = glyphs.front().positions().front().y();
                    font.setPointSizeF(glyphs.front().rawFont().pixelSize() * 72.0 /
                        (document.documentLayout()->paintDevice()
                                ? document.documentLayout()->paintDevice()->logicalDpiY()
                                : 96));
                }
                const QFontMetricsF metrics(font, document.documentLayout()->paintDevice());
                const auto height = metrics.ascent() + metrics.descent();
                baseline -= height * format.baselineOffset() / 100;
                if (format.verticalAlignment() == QTextCharFormat::AlignSuperScript)
                    baseline -= height * format.superScriptBaseline() / 100;
                else if (format.verticalAlignment() == QTextCharFormat::AlignSubScript)
                    baseline += height * format.subScriptBaseline() / 100;
                const auto stroke = std::max(0.6, metrics.lineWidth());
                const auto append = [&](qreal y)
                {
                    for (const auto& span : merged)
                        result.push_back(QVariantMap{{"kind", "textLine"}, {"x", bounds.x() + span.first},
                            {"y", bounds.y() + y}, {"width", span.second - span.first}, {"height", stroke},
                            {"color", color}});
                };
                if (underline)
                {
                    append(baseline + metrics.underlinePos());
                    append(baseline + metrics.underlinePos() + 2 * stroke);
                }
                if (strike)
                {
                    append(baseline - metrics.strikeOutPos() - stroke);
                    append(baseline - metrics.strikeOutPos() + stroke);
                }
            }
        }
    }
    QVariantList word_paragraph_decorations(const QTextDocument& document, const QRectF& clip)
    {
        QVariantList result = word_ruby_decorations(document, clip);
        for (const auto& block : word_decoration_blocks(document))
        {
            const auto bounds = document.documentLayout()->blockBoundingRect(block);
            if (!clip.isEmpty() && !bounds.intersects(clip))
                continue;
            append_word_tab_leaders(result, document, block, bounds);
            const auto* layout = block.layout();
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                if (fragment.isValid())
                    append_text_lines(result, document, block, fragment, bounds);
                const auto color = fragment.charFormat().property(character_border_property).toString();
                if (!fragment.isValid() || color.isEmpty() || !layout)
                    continue;
                const auto rect = document.documentLayout()->blockBoundingRect(block);
                const auto start = fragment.position() - block.position();
                const auto end = start + fragment.length();
                for (int index = 0; index < layout->lineCount(); ++index)
                {
                    const auto line = layout->lineAt(index);
                    const auto first = std::max(start, line.textStart());
                    const auto last = std::min(end, line.textStart() + line.textLength());
                    if (last <= first)
                        continue;
                    const auto x1 = line.cursorToX(first), x2 = line.cursorToX(last);
                    result.push_back(QVariantMap{{"x", rect.x() + std::min(x1, x2)},
                        {"y", rect.y() + line.y()}, {"width", std::abs(x2 - x1)}, {"height", line.height()},
                        {"color", color}, {"bottomOnly", false}});
                }
            }
            const auto color = block.blockFormat().property(border_color_property).toString();
            if (color.isEmpty())
                continue;
            const auto rect = document.documentLayout()->blockBoundingRect(block);
            result.push_back(QVariantMap{{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()},
                {"height", rect.height()}, {"color", color},
                {"bottomOnly", block.blockFormat().property(border_bottom_property)}});
        }
        return result;
    }

    void paint_word_decoration(QPainter& painter, const QVariantMap& decoration, qreal border_width)
    {
        const QRectF rect(decoration.value("x").toReal(), decoration.value("y").toReal(),
            decoration.value("width").toReal(), decoration.value("height").toReal());
        painter.save();
        painter.setPen(QPen(QColor(decoration.value("color").toString()), border_width));
        painter.setBrush(Qt::NoBrush);
        if (decoration.value("kind").toString() == "ruby")
        {
            painter.setFont(decoration.value("font").value<QFont>());
            painter.drawText(rect, Qt::AlignCenter, decoration.value("ruby").toString());
        }
        else if (decoration.value("kind").toString() == "tabLeader")
        {
            painter.setClipRect(rect, Qt::IntersectClip);
            painter.setFont(decoration.value("font").value<QFont>());
            painter.drawText(QPointF(rect.x(), decoration.value("baseline").toReal()),
                decoration.value("text").toString());
        }
        else if (decoration.value("kind").toString() == "tabBar")
            painter.drawLine(rect.topLeft(), rect.bottomLeft());
        else if (decoration.value("kind").toString() == "textLine")
        {
            auto pen = painter.pen();
            pen.setWidthF(rect.height());
            pen.setCapStyle(Qt::FlatCap);
            painter.setPen(pen);
            painter.drawLine(rect.topLeft(), rect.topRight());
        }
        else if (decoration.value("bottomOnly").toBool())
            painter.drawLine(rect.bottomLeft(), rect.bottomRight());
        else
            painter.drawRect(rect);
        painter.restore();
    }

}
