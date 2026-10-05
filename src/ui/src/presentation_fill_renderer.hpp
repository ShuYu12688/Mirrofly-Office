#pragma once

#include "presentation_scene.hpp"

namespace mirrorfly
{
    QBrush presentation_pattern_fill(const PresentationFill& fill);
    QBrush presentation_image_fill(
        const RenderPresentationPtr& document, const PresentationFill& fill, const QRectF& bounds);
}
