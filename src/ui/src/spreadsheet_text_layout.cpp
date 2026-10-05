#include "spreadsheet_text_renderer.hpp"

#include <QPainter>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace
{
    void distribute(QList<QGlyphRun>& glyphs, const QList<int>& boundaries, const QTextLayout& layout,
        const QTextLine& line, const QString& text, qreal width)
    {
        int end = line.textStart() + line.textLength();
        while (end > line.textStart() && text[end - 1].isSpace())
            --end;
        QList<QRawFont> fonts;
        for (const auto& run : glyphs)
            if (!fonts.contains(run.rawFont()))
                fonts.push_back(run.rawFont());
        using Glyph = std::tuple<qsizetype, quint32, qreal, qreal>;
        std::map<Glyph, int> clusters;
        // Qt 6.8 fallback runs can restart string indexes; match shaped glyphs by range instead.
        const auto first = std::lower_bound(boundaries.cbegin(), boundaries.cend(), line.textStart());
        for (qsizetype index = first - boundaries.cbegin(); index + 1 < boundaries.size(); ++index)
        {
            const auto start = boundaries[index];
            if (start >= end)
                break;
            for (const auto& run : layout.glyphRuns(start, boundaries[index + 1] - start))
            {
                const auto font = fonts.indexOf(run.rawFont());
                const auto ids = run.glyphIndexes();
                const auto points = run.positions();
                for (qsizetype glyph = 0; glyph < std::min(ids.size(), points.size()); ++glyph)
                    clusters.emplace(Glyph{font, ids[glyph], points[glyph].x(), points[glyph].y()}, start);
            }
        }
        std::map<int, qreal> leading;
        for (auto& run : glyphs)
        {
            const auto positions = run.positions();
            const auto ids = run.glyphIndexes();
            const auto font = fonts.indexOf(run.rawFont());
            QList<qsizetype> indexes;
            for (qsizetype index = 0; index < std::min(positions.size(), ids.size()); ++index)
            {
                const auto match =
                    clusters.find(Glyph{font, ids[index], positions[index].x(), positions[index].y()});
                const auto id = match == clusters.end() ? end : match->second;
                indexes.push_back(id);
                if (id >= end)
                    continue;
                const auto found = leading.find(id);
                if (found == leading.end())
                    leading[id] = positions[index].x();
                else
                    found->second = std::min(found->second, positions[index].x());
            }
            run.setStringIndexes(indexes);
        }
        QList<std::pair<int, qreal>> visual;
        for (const auto& item : leading)
            visual.push_back(item);
        std::stable_sort(visual.begin(), visual.end(), [](const auto& a, const auto& b)
        {
            return a.second < b.second;
        });
        const auto extra = std::max(qreal(0), width - line.naturalTextWidth());
        std::map<int, qreal> offsets;
        for (qsizetype index = 0; index < visual.size(); ++index)
            offsets[visual[index].first] =
                visual.size() == 1 ? extra / 2 : extra * index / (visual.size() - 1);
        for (auto& run : glyphs)
        {
            auto positions = run.positions();
            const auto indexes = run.stringIndexes();
            for (qsizetype index = 0; index < std::min(positions.size(), indexes.size()); ++index)
            {
                const auto found = offsets.find(static_cast<int>(indexes[index]));
                if (found != offsets.end())
                    positions[index].rx() += found->second;
            }
            run.setPositions(positions);
            run.setBoundingRect(QRectF(0, line.y(), width, line.height()));
        }
    }
}

namespace mirrorfly
{
    SpreadsheetTextLayout layout_spreadsheet_text(const QString& input, const QFont& font, qreal width,
        const QVariantMap& format, const QPaintDevice* device)
    {
        SpreadsheetTextLayout result;
        if (!std::isfinite(width) || width <= 0)
            return result;
        auto text = input.left(32767);
        text.replace("\r\n", "\n");
        text.replace('\r', '\n');
        text.replace('\n', QChar::LineSeparator);
        const bool stacked = format.value("textRotation").toInt() == 255;
        if (stacked)
        {
            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
            QString vertical;
            int start = 0;
            for (int end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary())
            {
                if (!vertical.isEmpty())
                    vertical.append(QChar::LineSeparator);
                vertical.append(text.mid(start, end - start));
                start = end;
            }
            text = vertical;
        }
        const bool distributed = !stacked && format.value("align").toString() == "distributed";
        QTextLayout layout(text, font, device);
        QTextOption option;
        if (!stacked && format.value("wrap").toString() == "1")
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        else
            option.setWrapMode(QTextOption::NoWrap);
        option.setAlignment(
            !stacked && format.value("align") == "justify" ? Qt::AlignJustify : Qt::AlignLeft);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true)
        {
            auto line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(width);
            line.setPosition({0, result.height});
            result.height += line.height();
            result.width = std::max(result.width, line.naturalTextWidth());
        }
        layout.endLayout();
        if (stacked)
            for (int index = 0; index < layout.lineCount(); ++index)
            {
                auto line = layout.lineAt(index);
                line.setPosition({(result.width - line.naturalTextWidth()) / 2, line.y()});
            }
        if (format.value("shrinkToFit").toString() == "1" && format.value("wrap").toString() != "1" &&
            layout.lineCount() == 1 && !text.contains(QChar::LineSeparator) && result.width > width)
        {
            result.scale = width / result.width;
            result.width = width;
            result.height *= result.scale;
        }
        QList<int> boundaries{0};
        if (distributed)
        {
            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
            for (int next = finder.toNextBoundary(); next >= 0; next = finder.toNextBoundary())
                boundaries.push_back(next);
        }
        for (int index = 0; index < layout.lineCount(); ++index)
        {
            const auto line = layout.lineAt(index);
            auto glyphs = layout.glyphRuns(line.textStart(), line.textLength(), QTextLayout::RetrieveAll);
            if (distributed)
                distribute(glyphs, boundaries, layout, line, text, width);
            result.glyphs.append(glyphs);
        }
        return result;
    }

    SpreadsheetTextLayout layout_spreadsheet_cell_text(const QString& text, const QFont& font,
        const QSizeF& size, const QVariantMap& format, const QPaintDevice* device)
    {
        if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0 ||
            size.height() <= 0)
            return {};
        const int encoded = format.value("textRotation").toInt();
        const int angle = encoded >= 0 && encoded <= 180 ? (encoded <= 90 ? -encoded : encoded - 90) : 0;
        if (angle == 0)
        {
            auto result = layout_spreadsheet_text(text, font, size.width(), format, device);
            result.transform.scale(result.scale, result.scale);
            return result;
        }
        const qreal radians = angle * std::acos(qreal(-1)) / 180;
        const qreal cosine = std::abs(std::cos(radians));
        const qreal sine = std::abs(std::sin(radians));
        // Available baseline length is projected into the rotated cell; glyphs keep their font size.
        const qreal width = std::min(cosine > 1e-6 ? size.width() / cosine : size.height(),
            sine > 1e-6 ? size.height() / sine : size.width());
        auto unscaled = format;
        unscaled.insert("shrinkToFit", "0");
        auto result = layout_spreadsheet_text(text, font, width, unscaled, device);
        QTransform transform;
        transform.rotate(angle);
        auto bounds = transform.mapRect(QRectF(0, 0, result.width, result.height));
        if (format.value("shrinkToFit").toString() == "1" && format.value("wrap").toString() != "1" &&
            !text.contains('\n') && !text.contains('\r') && !text.contains(QChar::LineSeparator) &&
            bounds.width() > 0 && bounds.height() > 0)
        {
            result.scale =
                std::min({qreal(1), size.width() / bounds.width(), size.height() / bounds.height()});
            transform.scale(result.scale, result.scale);
            bounds = transform.mapRect(QRectF(0, 0, result.width, result.height));
        }
        transform *= QTransform::fromTranslate(-bounds.left(), -bounds.top());
        result.transform = transform;
        result.width = bounds.width();
        result.height = bounds.height();
        return result;
    }
}
