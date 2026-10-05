#pragma once

#include <pugixml.hpp>

#include <string>

namespace mirrorfly::word_detail
{
    pugi::xml_node ordered_property(pugi::xml_node target, pugi::xml_node next, const char* kind);
    void patch_properties(pugi::xml_node raw, pugi::xml_node before, pugi::xml_node after, const char* kind,
        const std::string& prefix, const std::string& skip = {});
}
