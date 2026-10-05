#pragma once

#include <mirrorfly/presentation.hpp>

#include <QPainter>

namespace mirrorfly
{
    QPen presentation_line_pen(QPen pen, const PresentationLineStyle& style);
    void paint_presentation_line_ends(QPainter& painter, const PresentationShape& shape, const QPen& pen);
}
