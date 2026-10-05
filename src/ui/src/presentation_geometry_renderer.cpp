#include "presentation_geometry_renderer.hpp"

#include <algorithm>

namespace
{
    QColor path_color(QColor color, const std::string& mode)
    {
        const bool lighten = mode == "lighten" || mode == "lightenLess";
        const double amount = mode == "lighten" || mode == "darken" ? 0.4 : 0.2;
        const auto channel = [&](qreal value)
        {
            return lighten ? value + (1 - value) * amount : value * (1 - amount);
        };
        color.setRgbF(channel(color.redF()), channel(color.greenF()), channel(color.blueF()), color.alphaF());
        return color;
    }

    QBrush path_brush(const QBrush& brush, const std::string& mode)
    {
        if (mode == "none")
            return Qt::NoBrush;
        if (mode == "norm" || brush.style() == Qt::NoBrush)
            return brush;
        if (brush.gradient())
        {
            QGradient gradient = *brush.gradient();
            auto stops = gradient.stops();
            for (auto& stop : stops)
                stop.second = path_color(stop.second, mode);
            gradient.setStops(stops);
            return QBrush(gradient);
        }
        return QBrush(path_color(brush.color(), mode));
    }
}

namespace mirrorfly
{
    QPainterPath presentation_path(const PresentationPath& path, double width, double height)
    {
        QPainterPath result;
        result.setFillRule(Qt::WindingFill);
        if (path.width <= 0 || path.height <= 0)
            return result;
        const auto x = width / path.width, y = height / path.height;
        for (const auto& command : path.commands)
        {
            const auto& v = command.values;
            switch (command.action)
            {
            case PresentationPathAction::Move:
                result.moveTo(v[0] * x, v[1] * y);
                break;
            case PresentationPathAction::Line:
                result.lineTo(v[0] * x, v[1] * y);
                break;
            case PresentationPathAction::Quadratic:
                result.quadTo(v[0] * x, v[1] * y, v[2] * x, v[3] * y);
                break;
            case PresentationPathAction::Cubic:
                result.cubicTo(v[0] * x, v[1] * y, v[2] * x, v[3] * y, v[4] * x, v[5] * y);
                break;
            case PresentationPathAction::Close:
                result.closeSubpath();
                break;
            }
        }
        return result;
    }

    void paint_presentation_geometry(QPainter& painter, const PresentationGeometry& geometry, double width,
        double height, const QBrush& fill, const QPen& outline)
    {
        painter.save();
        for (const auto& path : geometry.paths)
        {
            painter.setBrush(path_brush(fill, path.fill));
            painter.setPen(path.stroke ? outline : QPen(Qt::NoPen));
            painter.drawPath(presentation_path(path, width, height));
        }
        painter.restore();
    }
}
