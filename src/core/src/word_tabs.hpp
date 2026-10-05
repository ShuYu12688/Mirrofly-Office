#pragma once
#include <mirrorfly/word.hpp>
#include <pugixml.hpp>

namespace mirrorfly::word_detail
{
    bool valid_tab_stops(const std::vector<WordTabStop>& stops);
    std::vector<WordTabStop> read_tab_stops(pugi::xml_node tabs);
    void write_tab_stops(pugi::xml_node properties, const std::vector<WordTabStop>& stops);
    void merge_tab_stops(pugi::xml_node target, pugi::xml_node source);
    void patch_tab_stops(
        pugi::xml_node properties, pugi::xml_node before, pugi::xml_node after, const std::string& prefix);
    bool read_tab_interval(const std::vector<OfficePart>& parts, double& points, std::string& error);
    void write_tab_interval(std::vector<OfficePart>& parts, double points);
}
