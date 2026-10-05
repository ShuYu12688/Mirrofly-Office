#pragma once

#include <mirrorfly/presentation_geometry.hpp>

#include <QPainter>
#include <QPainterPath>

namespace mirrorfly
{
    QPainterPath presentation_path(const PresentationPath& path, double width, double height);
    void paint_presentation_geometry(QPainter& painter, const PresentationGeometry& geometry, double width,
        double height, const QBrush& fill, const QPen& outline);
}
