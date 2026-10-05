#pragma once

#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <string>

namespace mirrorfly
{
    bool valid_presentation_transition(const PresentationTransition& transition);
    bool editable_presentation_transition(pugi::xml_node slide_root);
    std::string presentation_transition_xml(const PresentationTransition& transition);
    void patch_presentation_transition(pugi::xml_node slide_root, const PresentationTransition& transition);
}
