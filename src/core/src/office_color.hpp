#pragma once

#include <string>

namespace mirrorfly::office_color
{
    // Canonical #RRGGBB; an empty result means an invalid color or transform.
    std::string rgb(const std::string& value);
    std::string tint(const std::string& color, double amount);
}
