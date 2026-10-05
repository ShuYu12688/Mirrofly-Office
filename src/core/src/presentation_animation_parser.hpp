#pragma once

#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

namespace mirrorfly
{
    void read_presentation_animations(pugi::xml_node slide_root, PresentationSlide& slide);
}
