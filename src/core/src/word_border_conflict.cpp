#include <mirrorfly/word.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>

namespace
{
    constexpr std::array<std::string_view, 25> styles{"single", "thick", "double", "dotted", "dashed",
        "dotDash", "dotDotDash", "triple", "thinThickSmallGap", "thickThinSmallGap", "thinThickThinSmallGap",
        "thinThickMediumGap", "thickThinMediumGap", "thinThickThinMediumGap", "thinThickLargeGap",
        "thickThinLargeGap", "thinThickThinLargeGap", "wave", "doubleWave", "dashSmallGap", "dashDotStroked",
        "threeDEmboss", "threeDEngrave", "outset", "inset"};

    int rank(const std::string& style)
    {
        const auto found = std::find(styles.begin(), styles.end(), style);
        return found == styles.end() ? 0 : static_cast<int>(found - styles.begin()) + 1;
    }

    double weight(const mirrorfly::WordBorder& border)
    {
        const auto value = rank(border.style);
        if (value == 4 || value == 5)
            return 1;
        // MS-OI29500 uses border numbers 1/2/3, then 8..27 for the remaining styles.
        return border.width * 8 * (value > 5 ? value + 2 : value);
    }

    std::tuple<int, int, int> brightness(const std::string& color)
    {
        if (color.size() != 7 || color.front() != '#' ||
            color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            return {};
        const auto r = std::stoi(color.substr(1, 2), nullptr, 16);
        const auto g = std::stoi(color.substr(3, 2), nullptr, 16);
        const auto b = std::stoi(color.substr(5, 2), nullptr, 16);
        return {r + b + 2 * g, b + 2 * g, g};
    }
}

namespace mirrorfly
{
    WordBorder resolve_word_border(const WordBorder& first, const WordBorder& second)
    {
        if (first.style == "nil")
            return first;
        if (second.style == "nil")
            return second;
        if (first.style.empty() || first.style == "none" || first.width <= 0)
            return second;
        if (second.style.empty() || second.style == "none" || second.width <= 0)
            return first;
        if (weight(first) != weight(second))
            return weight(first) > weight(second) ? first : second;
        if (rank(first.style) != rank(second.style))
            return rank(first.style) < rank(second.style) ? first : second;
        return brightness(first.color) <= brightness(second.color) ? first : second;
    }
}
