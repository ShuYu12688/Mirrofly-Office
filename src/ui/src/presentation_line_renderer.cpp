#include "presentation_line_renderer.hpp"
#include "presentation_geometry_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    double end_size(const std::string& value)
    {
        return value == "sm" ? 2 : (value == "lg" ? 5 : 3);
    }

    void draw_end(QPainter& painter, const QPainterPath& path, bool head,
        const mirrorfly::PresentationLineEnd& end, const QPen& line)
    {
        if (end.type.empty() || end.type == "none" || path.elementCount() < 2)
            return;
        const QPointF tip = path.pointAtPercent(head ? 0 : 1);
        QPointF inside = tip;
        for (const double amount : {0.001, 0.01, 0.1, 0.5})
        {
            inside = path.pointAtPercent(head ? amount : 1 - amount);
            if (QLineF(tip, inside).length() > 0.0001)
                break;
        }
        if (QLineF(tip, inside).length() <= 0.0001)
            return;
        const double length = end_size(end.length) * line.widthF();
        const double half_width = end_size(end.width) * line.widthF() / 2;
        QPainterPath marker;
        if (end.type == "oval")
            marker.addEllipse(QRectF(-length / 2, -half_width, length, half_width * 2));
        else if (end.type == "diamond")
        {
            marker.moveTo(length / 2, 0);
            marker.lineTo(0, half_width);
            marker.lineTo(-length / 2, 0);
            marker.lineTo(0, -half_width);
            marker.closeSubpath();
        }
        else if (end.type == "triangle" || end.type == "stealth" || end.type == "arrow")
        {
            marker.moveTo(-length, -half_width);
            marker.lineTo(0, 0);
            marker.lineTo(-length, half_width);
            if (end.type == "stealth")
                marker.lineTo(-length * 0.65, 0);
            if (end.type != "arrow")
                marker.closeSubpath();
        }
        else
            return;
        painter.save();
        painter.translate(tip);
        painter.rotate(std::atan2(tip.y() - inside.y(), tip.x() - inside.x()) * 180 / 3.14159265358979323846);
        QPen pen(line.brush(), line.widthF(), Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
        painter.setPen(end.type == "arrow" ? pen : QPen(Qt::NoPen));
        painter.setBrush(end.type == "arrow" ? QBrush(Qt::NoBrush) : line.brush());
        painter.drawPath(marker);
        painter.restore();
    }
}

namespace mirrorfly
{
    QPen presentation_line_pen(QPen pen, const PresentationLineStyle& style)
    {
        if (pen.style() == Qt::NoPen)
            return pen;
        pen.setCapStyle(
            style.cap == "rnd" ? Qt::RoundCap : (style.cap == "sq" ? Qt::SquareCap : Qt::FlatCap));
        pen.setJoinStyle(
            style.join == "miter" ? Qt::MiterJoin : (style.join == "bevel" ? Qt::BevelJoin : Qt::RoundJoin));
        if (!style.dashes.empty() && style.dashes.size() % 2 == 0)
        {
            QList<qreal> pattern;
            for (const double value : style.dashes)
                pattern.append(std::clamp(value, 0.01, 1000.0));
            pen.setDashPattern(pattern);
        }
        return pen;
    }

    void paint_presentation_line_ends(QPainter& painter, const PresentationShape& shape, const QPen& pen)
    {
        if (pen.style() == Qt::NoPen || pen.widthF() <= 0)
            return;
        const auto draw = [&](const QPainterPath& path)
        {
            draw_end(painter, path, true, shape.line_style.head, pen);
            draw_end(painter, path, false, shape.line_style.tail, pen);
        };
        if (shape.path_geometry)
        {
            for (const auto& path : shape.path_geometry->paths)
                if (path.stroke && !path.commands.empty() &&
                    path.commands.back().action != PresentationPathAction::Close)
                    draw(presentation_path(path, shape.width, shape.height));
        }
        else if (shape.geometry == "line")
        {
            QPainterPath path;
            path.moveTo(0, 0);
            path.lineTo(shape.width, shape.height);
            draw(path);
        }
    }
}
