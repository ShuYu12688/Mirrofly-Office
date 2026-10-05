#pragma once
#include <mirrorfly/word.hpp>
#include <pugixml.hpp>
namespace mirrorfly::word_detail
{
    void read_paragraph_layout(pugi::xml_node properties, WordParagraph& paragraph);
}
