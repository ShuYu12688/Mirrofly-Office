#pragma once

#include <cstddef>
#include <string>

namespace mirrorfly::presentation_edit_common
{
    bool valid_xml_text(const std::string& text);
    bool finite(double value);
    std::size_t saturated_add(std::size_t left, std::size_t right);
    std::string image_extension(const std::string& mime_type);
}
