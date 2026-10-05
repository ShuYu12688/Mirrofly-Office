#pragma once

#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

namespace mirrorfly
{
    struct WordImageReference
    {
        std::string relationship;
        WordImage image;
        bool approximate = false;
    };

    WordImageReference read_word_image(pugi::xml_node object);
}
