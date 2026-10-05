#pragma once

#include <pugixml.hpp>

#include <string>

namespace mirrorfly::presentation_math
{
    struct Limit
    {
        std::string message;
    };

    void append_text(pugi::xml_node node, std::string& output, unsigned depth);
    bool contains(pugi::xml_node node, unsigned depth);
}
