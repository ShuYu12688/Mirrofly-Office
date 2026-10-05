#include "presentation_edit_common.hpp"

#include <utf8.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace mirrorfly::presentation_edit_common
{
    bool valid_xml_text(const std::string& text)
    {
        if (!utf8::is_valid(text.begin(), text.end()))
            return false;
        for (const unsigned char character : text)
            if (character < 32 && character != '\t' && character != '\n' && character != '\r')
                return false;
        return true;
    }

    bool finite(double value)
    {
        return std::isfinite(value);
    }

    std::size_t saturated_add(std::size_t left, std::size_t right)
    {
        if (right > std::numeric_limits<std::size_t>::max() - left)
            return std::numeric_limits<std::size_t>::max();
        return left + right;
    }

    std::string image_extension(const std::string& mime_type)
    {
        if (mime_type == "image/png")
            return "png";
        if (mime_type == "image/jpeg")
            return "jpg";
        if (mime_type == "image/gif")
            return "gif";
        if (mime_type == "image/bmp")
            return "bmp";
        if (mime_type == "image/webp")
            return "webp";
        return {};
    }
}
