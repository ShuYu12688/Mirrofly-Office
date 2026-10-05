#pragma once

#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

namespace mirrorfly::detail
{
    bool presentation_group_is_extendable(pugi::xml_node group);

    // Mutates only the already loaded slide object tree; package ownership stays with preservation.
    void patch_presentation_group(
        pugi::xml_node tree, const PresentationSlide& slide, const PresentationEditCommand& command);
}
