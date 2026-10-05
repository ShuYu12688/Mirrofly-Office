#include "spreadsheet_border_renderer.hpp"

#include <QPainter>
#include <cmath>

namespace mirrorfly
{
    void paint_spreadsheet_borders(
        QPainter& painter, const QRectF& rect, const QVariantMap& format, qreal scale)
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QStringList sides{"Left", "Right", "Top", "Bottom"};
        for (const auto& side : sides)
        {
            const auto style = format.value("border" + side, "none").toString();
            if (style == "none")
                continue;
            const QColor color(format.value("border" + side + "Color").toString());
            if (!color.isValid())
                continue;
            const bool medium = style == "medium" || style.startsWith("medium");
            const auto width = scale * (medium ? 2 : style == "thick" ? 3 : style == "hair" ? 0.5 : 1);
            QPen pen(color, width, Qt::SolidLine, Qt::FlatCap);
            if (style == "dotted")
                pen.setStyle(Qt::DotLine);
            else if (style.contains("DashDotDot", Qt::CaseInsensitive))
                pen.setStyle(Qt::DashDotDotLine);
            else if (style.contains("DashDot", Qt::CaseInsensitive))
                pen.setStyle(Qt::DashDotLine);
            else if (style.contains("dashed", Qt::CaseInsensitive))
                pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            const auto draw = [&](qreal inset)
            {
                const auto edge = rect.adjusted(inset, inset, -inset, -inset);
                if (side == "Left")
                    painter.drawLine(edge.topLeft(), edge.bottomLeft());
                else if (side == "Right")
                    painter.drawLine(edge.topRight(), edge.bottomRight());
                else if (side == "Top")
                    painter.drawLine(edge.topLeft(), edge.topRight());
                else
                    painter.drawLine(edge.bottomLeft(), edge.bottomRight());
            };
            draw(width / 2);
            if (style == "double")
                draw(width / 2 + scale * 2);
        }
        painter.restore();
    }

    SpreadsheetBorderRenderer::SpreadsheetBorderRenderer(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        setAcceptedMouseButtons(Qt::NoButton);
    }
    QVariantMap SpreadsheetBorderRenderer::format() const
    {
        return format_;
    }
    qreal SpreadsheetBorderRenderer::zoom() const
    {
        return zoom_;
    }
    void SpreadsheetBorderRenderer::setFormat(const QVariantMap& format)
    {
        if (format_ == format)
            return;
        format_ = format;
        update();
        emit changed();
    }
    void SpreadsheetBorderRenderer::setZoom(qreal zoom)
    {
        if (!std::isfinite(zoom) || zoom < 0.25 || zoom > 4 || qFuzzyCompare(zoom, zoom_))
            return;
        zoom_ = zoom;
        update();
        emit changed();
    }
    void SpreadsheetBorderRenderer::paint(QPainter* painter)
    {
        if (painter)
            paint_spreadsheet_borders(*painter, boundingRect(), format_, zoom_);
    }
}
