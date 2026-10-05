#include "word_character.hpp"
#include "word_tabs.hpp"
#include "word_units.hpp"
#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QTextLayout>
#include <algorithm>

namespace mirrorfly
{
    void append_word_tab_leaders(
        QVariantList& result, const QTextDocument& document, const QTextBlock& block, const QRectF& bounds)
    {
        const auto* layout = block.layout();
        if (!layout)
            return;
        const auto stops = word_tabs_from_format(block.blockFormat());
        const auto text = block.text();
        for (int line_index = 0; line_index < layout->lineCount(); ++line_index)
        {
            const auto line = layout->lineAt(line_index);
            for (const auto& stop : stops)
                if (stop.alignment == "bar" && stop.position >= 0)
                    result.push_back(QVariantMap{{"kind", "tabBar"},
                        {"x", bounds.x() + word_points_to_pixels(stop.position)},
                        {"y", bounds.y() + line.y()}, {"width", 1.0}, {"height", line.height()},
                        {"color", "#000000"}});
            for (int position = line.textStart(); position < line.textStart() + line.textLength(); ++position)
            {
                if (text.at(position) != QChar::Tabulation)
                    continue;
                const auto before = line.cursorToX(position);
                const auto after = line.cursorToX(position + 1);
                const auto found = std::find_if(stops.begin(), stops.end(), [&](const auto& stop)
                {
                    return stop.alignment != "bar" && word_points_to_pixels(stop.position) > before + 0.01;
                });
                if (found == stops.end() || found->leader == "none" || after - before <= 2)
                    continue;
                QTextCursor cursor(block);
                cursor.setPosition(block.position() + position);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                const auto format = cursor.charFormat();
                const auto font = format.font().resolve(document.defaultFont());
                const QFontMetricsF metrics(font, document.documentLayout()->paintDevice());
                auto color = word_character_color(format);
                if (color.isEmpty())
                    color = "#000000";
                const auto baseline = bounds.y() + line.y() + line.ascent();
                const auto x = bounds.x() + before + 1;
                const auto width = after - before - 2;
                if (found->leader == "underscore" || found->leader == "heavy")
                {
                    const auto thickness =
                        std::max(0.6, metrics.lineWidth()) * (found->leader == "heavy" ? 2 : 1);
                    result.push_back(
                        QVariantMap{{"kind", "textLine"}, {"x", x}, {"y", baseline + metrics.underlinePos()},
                            {"width", width}, {"height", thickness}, {"color", color}});
                }
                else
                {
                    QString glyph;
                    if (found->leader == "dot")
                        glyph = ".";
                    else if (found->leader == "hyphen")
                        glyph = "-";
                    else if (found->leader == "middleDot")
                        glyph = QStringLiteral("·");
                    if (glyph.isEmpty())
                        continue;
                    const auto advance = std::max(1.0, metrics.horizontalAdvance(glyph));
                    const auto count = std::min(4096, static_cast<int>(width / advance));
                    if (count > 0)
                        result.push_back(
                            QVariantMap{{"kind", "tabLeader"}, {"x", x}, {"y", baseline - metrics.ascent()},
                                {"baseline", baseline}, {"width", width}, {"height", metrics.height()},
                                {"color", color}, {"font", font}, {"text", glyph.repeated(count)}});
                }
            }
        }
    }
}
