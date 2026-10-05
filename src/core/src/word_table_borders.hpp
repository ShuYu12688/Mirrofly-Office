#pragma once

#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

namespace mirrorfly::word_detail
{
    class StyleResolver;
    void read_table_borders(WordTable& table, pugi::xml_node source, StyleResolver& styles);
    bool valid_border(const WordBorder& border);
}
