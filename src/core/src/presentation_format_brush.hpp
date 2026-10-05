#pragma once

#include <mirrorfly/presentation.hpp>

namespace mirrorfly
{
    // Copies supported appearance only. Geometry, content, hyperlinks and media ownership stay with the
    // target.
    bool presentation_format_brush_supported(const PresentationShape& shape);
    bool apply_presentation_format_brush(PresentationShape& target, const PresentationShape& source);
}
