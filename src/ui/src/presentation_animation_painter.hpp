#pragma once

#include <mirrorfly/presentation.hpp>

#include <QPainter>

namespace mirrorfly
{
    // The caller owns save/restore. The result is false for a fully hidden object.
    bool apply_presentation_animation(
        QPainter& painter, const PresentationShape& shape, const PresentationAnimationState& state);
}
