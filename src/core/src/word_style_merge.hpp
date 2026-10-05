#pragma once

#include <pugixml.hpp>
#include <string>

namespace mirrorfly::word_detail
{
    bool enabled(pugi::xml_node node);
    void merge_properties(pugi::xml_node target, pugi::xml_node source, bool toggle = false);
    pugi::xml_node property_root(pugi::xml_document& xml, const char* name);
    void set(pugi::xml_node node, const char* name, const std::string& value);
}
