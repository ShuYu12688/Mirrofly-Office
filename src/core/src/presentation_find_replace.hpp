#pragma once

#include "presentation_preservation.hpp"

namespace mirrorfly
{
    PresentationEditResult replace_presentation_text_model(
        PresentationScene& scene, const PresentationEditCommand& command, PresentationEditResult result);
    void patch_presentation_text_matches(
        PresentationPackageState& state, const PresentationScene& before, const PresentationScene& after);
}
