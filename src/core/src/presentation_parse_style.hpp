#pragma once

#include <pugixml.hpp>

#include <map>
#include <string>
#include <utility>

namespace mirrorfly::presentation_parse_style
{
    using Node = pugi::xml_node;

    struct Theme
    {
        std::map<std::string, std::string> colors;
        std::map<std::string, std::string> aliases{
            {"bg1", "lt1"}, {"tx1", "dk1"}, {"bg2", "lt2"}, {"tx2", "dk2"}};
        std::string major_font = "Arial";
        std::string minor_font = "Arial";
        std::string major_east_asian;
        std::string minor_east_asian;
        Node format;
        mutable bool approximated_color = false;
    };

    std::pair<std::string, double> read_color(
        Node parent, const Theme& theme, const std::string& placeholder = "#000000");
    void apply_color_map(Theme& theme, Node mapping);
    Theme read_theme(Node root);
}
