#pragma once

#include <mirrorfly/presentation.hpp>

namespace mirrorfly
{
    PresentationSlide refined_presentation_template(std::uint64_t& next_shape_id,
        PresentationSlideLayout layout, const PresentationTemplatePalette& palette);
}
