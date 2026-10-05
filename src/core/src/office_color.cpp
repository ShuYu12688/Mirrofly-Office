#include "office_color.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mirrorfly::office_color
{
    std::string rgb(const std::string& value)
    {
        const auto start = value.size() == 7 && value.front() == '#' ? 1u : 0u;
        if (value.size() - start != 6 ||
            value.find_first_not_of("0123456789abcdefABCDEF", start) != std::string::npos)
            return {};
        std::string result = "#" + value.substr(start);
        for (auto& c : result)
            if (c >= 'a' && c <= 'f')
                c -= 'a' - 'A';
        return result;
    }

    std::string tint(const std::string& color, double amount)
    {
        auto result = rgb(color);
        if (result.empty() || !std::isfinite(amount) || amount < -1 || amount > 1)
            return {};
        if (amount == 0)
            return result;
        std::array<double, 3> channels{};
        for (std::size_t i = 0; i < channels.size(); ++i)
            channels[i] = std::stoul(result.substr(1 + i * 2, 2), nullptr, 16) / 255.0;
        const auto bounds = std::minmax_element(channels.begin(), channels.end());
        const double light = (*bounds.first + *bounds.second) / 2;
        const double adjusted = amount < 0 ? light * (1 + amount) : light * (1 - amount) + amount;
        // HSL lightness changes preserve hue and saturation. Scaling the centered RGB
        // components avoids a hue-sector conversion and handles achromatic colors directly.
        const double chroma = *bounds.second - *bounds.first;
        const double saturation = chroma == 0 ? 0 : chroma / (1 - std::abs(2 * light - 1));
        const double next_chroma = (1 - std::abs(2 * adjusted - 1)) * saturation;
        constexpr char hex[] = "0123456789ABCDEF";
        for (std::size_t i = 0; i < channels.size(); ++i)
        {
            const double channel =
                chroma == 0 ? adjusted : adjusted + (channels[i] - light) * next_chroma / chroma;
            const int byte = static_cast<int>(std::lround(std::clamp(channel, 0.0, 1.0) * 255));
            result[1 + i * 2] = hex[byte / 16];
            result[2 + i * 2] = hex[byte % 16];
        }
        return result;
    }
}
