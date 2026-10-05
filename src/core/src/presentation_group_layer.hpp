#pragma once

#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

namespace mirrorfly::detail
{
    void patch_presentation_group_layer(
        pugi::xml_node shape_tree, const std::string& group_id, const std::string& position);
}
