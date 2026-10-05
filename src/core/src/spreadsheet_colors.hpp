#pragma once

#include <array>
#include <pugixml.hpp>
#include <string>
#include <vector>

namespace mirrorfly
{
    class SpreadsheetColors
    {
    public:
        SpreadsheetColors(pugi::xml_node styles = {}, pugi::xml_node theme = {});
        std::string resolve(pugi::xml_node color) const;

    private:
        std::array<std::string, 12> theme_;
        std::vector<std::string> indexed_;
    };
}
