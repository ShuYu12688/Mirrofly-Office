#pragma once

#include <mirrorfly/presentation.hpp>

#include <cstdint>
#include <string>

namespace mirrorfly::presentation_edit_layout
{
    PresentationShape text_box(std::uint64_t& next_shape_id, const std::string& name, double x, double y,
        double width, double height, double font_size, bool bold = false, const std::string& text = {});
    PresentationSlide slide_for_layout(
        PresentationScene& scene, PresentationSlideLayout layout, const PresentationTemplatePalette& palette);
    void replace_text(PresentationShape& shape, const std::string& text);
}
