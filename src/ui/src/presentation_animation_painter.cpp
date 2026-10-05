#include "presentation_animation_painter.hpp"

#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace
{
    QPainterPath reveal_path(const QRectF& bounds, const std::string& filter, double progress)
    {
        QPainterPath path;
        const auto kind = filter.substr(0, filter.find('('));
        const double width = bounds.width();
        const double height = bounds.height();
        const auto center = bounds.center();
        if (progress >= 1)
        {
            path.addRect(bounds);
            return path;
        }
        if (progress <= 0)
            return path;
        if (kind == "circle")
        {
            const double radius = std::hypot(width, height) * progress / 2;
            path.addEllipse(center, radius, radius);
        }
        else if (kind == "diamond")
        {
            path.moveTo(width / 2, height / 2 - height * progress);
            path.lineTo(width / 2 + width * progress, height / 2);
            path.lineTo(width / 2, height / 2 + height * progress);
            path.lineTo(width / 2 - width * progress, height / 2);
            path.closeSubpath();
        }
        else if (kind == "plus")
        {
            path.setFillRule(Qt::WindingFill);
            path.addRect(width * (1 - progress) / 2, 0, width * progress, height);
            path.addRect(0, height * (1 - progress) / 2, width, height * progress);
        }
        else if (kind == "box")
            path.addRect(
                width * (1 - progress) / 2, height * (1 - progress) / 2, width * progress, height * progress);
        else if (kind == "barn")
        {
            if (filter.find("Horizontal") != std::string::npos)
                path.addRect(0, height * (1 - progress) / 2, width, height * progress);
            else
                path.addRect(width * (1 - progress) / 2, 0, width * progress, height);
        }
        else if (kind == "wheel")
        {
            int spokes = 1;
            if (filter.find("2") != std::string::npos)
                spokes = 2;
            if (filter.find("3") != std::string::npos)
                spokes = 3;
            if (filter.find("4") != std::string::npos)
                spokes = 4;
            if (filter.find("8") != std::string::npos)
                spokes = 8;
            const double radius = std::hypot(width, height);
            const QRectF circle(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
            for (int index = 0; index < spokes; ++index)
            {
                path.moveTo(center);
                path.arcTo(circle, 90 + index * 360.0 / spokes, -progress * 360.0 / spokes);
                path.closeSubpath();
            }
        }
        else if (kind == "blinds")
        {
            const bool vertical = filter.find("vertical") != std::string::npos;
            for (int index = 0; index < 10; ++index)
                if (vertical)
                    path.addRect(index * width / 10, 0, width * progress / 10, height);
                else
                    path.addRect(0, index * height / 10, width, height * progress / 10);
        }
        else if (kind == "checkerboard" || kind == "dissolve" || kind == "randomBars")
        {
            const int columns = kind == "randomBars" ? 1 : 16;
            const int rows = kind == "randomBars" ? 40 : 12;
            for (int row = 0; row < rows; ++row)
                for (int column = 0; column < columns; ++column)
                {
                    const double rank = ((row * 37 + column * 53 + row * column * 7) % 101) / 101.0;
                    if (kind == "checkerboard")
                    {
                        const double reveal = std::clamp(progress * 2 - ((row + column) % 2), 0.0, 1.0);
                        path.addRect(column * width / columns, row * height / rows, width / columns * reveal,
                            height / rows);
                    }
                    else if (rank < progress)
                        path.addRect(column * width / columns, row * height / rows, width / columns + 0.1,
                            height / rows + 0.1);
                }
        }
        else if (kind == "strips")
        {
            path.moveTo(0, 0);
            path.lineTo(width * 2 * progress, 0);
            path.lineTo(0, height * 2 * progress);
            path.closeSubpath();
        }
        else if (filter.find("down") != std::string::npos)
            path.addRect(0, 0, width, height * progress);
        else if (filter.find("up") != std::string::npos)
            path.addRect(0, height * (1 - progress), width, height * progress);
        else if (filter.find("left") != std::string::npos)
            path.addRect(width * (1 - progress), 0, width * progress, height);
        else
            path.addRect(0, 0, width * progress, height);
        if ((kind == "box" || kind == "circle" || kind == "diamond" || kind == "plus") &&
            filter.find("in") != std::string::npos)
        {
            QPainterPath full;
            full.addRect(bounds);
            return full.subtracted(reveal_path(bounds, kind + "(out)", 1 - progress));
        }
        return path;
    }
}

namespace mirrorfly
{
    bool apply_presentation_animation(
        QPainter& painter, const PresentationShape& shape, const PresentationAnimationState& state)
    {
        if (state.opacity <= 0 || std::abs(state.scale_x) < 1e-8 || std::abs(state.scale_y) < 1e-8)
            return false;
        painter.setOpacity(painter.opacity() * std::clamp(state.opacity, 0.0, 1.0));
        const auto& matrix = shape.transform;
        const QTransform transform(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
        const auto center = transform.map(QPointF(shape.width / 2, shape.height / 2));
        painter.translate(state.x + center.x(), state.y + center.y());
        painter.rotate(state.rotation);
        painter.scale(state.scale_x, state.scale_y);
        painter.translate(-center.x(), -center.y());
        if (!state.clip.empty())
        {
            const auto path = reveal_path(QRectF(0, 0, shape.width, shape.height), state.clip, state.reveal);
            painter.setClipPath(transform.map(path), Qt::IntersectClip);
        }
        return true;
    }
}
