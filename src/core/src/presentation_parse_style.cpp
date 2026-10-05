#include "presentation_parse_style.hpp"
#include "presentation_parse_package.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>

namespace mirrorfly::presentation_parse_style
{
    using presentation_parse_package::child;
    using presentation_parse_package::local_name;
    using presentation_parse_package::number;

    std::string hex_color(const std::string& value)
    {
        if (value.size() != 6 || value.find_first_not_of("0123456789ABCDEFabcdef") != std::string::npos)
        {
            return {};
        }
        return "#" + value;
    }

    std::array<double, 3> color_channels(const std::string& color)
    {
        if (color.size() != 7)
        {
            return {0, 0, 0};
        }
        return {std::strtol(color.substr(1, 2).c_str(), nullptr, 16) / 255.0,
            std::strtol(color.substr(3, 2).c_str(), nullptr, 16) / 255.0,
            std::strtol(color.substr(5, 2).c_str(), nullptr, 16) / 255.0};
    }

    std::string color_hex(const std::array<double, 3>& channels)
    {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string result = "#";
        for (const auto channel : channels)
        {
            const auto byte = static_cast<unsigned>(std::lround(std::clamp(channel, 0.0, 1.0) * 255));
            result += digits[byte / 16];
            result += digits[byte % 16];
        }
        return result;
    }

    void change_luminance(std::array<double, 3>& rgb, double multiplier, double offset)
    {
        const double maximum = *std::max_element(rgb.begin(), rgb.end());
        const double minimum = *std::min_element(rgb.begin(), rgb.end());
        const double lightness = (maximum + minimum) / 2;
        const double changed = std::clamp(lightness * multiplier + offset, 0.0, 1.0);
        if (maximum == minimum)
        {
            rgb.fill(changed);
            return;
        }
        const double chroma = maximum - minimum;
        const double saturation = chroma / (1 - std::abs(2 * lightness - 1));
        const double new_chroma = (1 - std::abs(2 * changed - 1)) * saturation;
        for (auto& value : rgb)
        {
            value = (value - minimum) / chroma * new_chroma + changed - new_chroma / 2;
        }
    }

    void change_saturation(std::array<double, 3>& rgb, double multiplier)
    {
        const double maximum = *std::max_element(rgb.begin(), rgb.end());
        const double minimum = *std::min_element(rgb.begin(), rgb.end());
        if (maximum == minimum)
        {
            return;
        }
        const double lightness = (maximum + minimum) / 2;
        const double chroma = maximum - minimum;
        const double saturation = chroma / (1 - std::abs(2 * lightness - 1));
        const double changed = std::clamp(saturation * multiplier, 0.0, 1.0);
        const double new_chroma = (1 - std::abs(2 * lightness - 1)) * changed;
        for (auto& value : rgb)
        {
            value = (value - minimum) / chroma * new_chroma + lightness - new_chroma / 2;
        }
    }

    std::pair<std::string, double> read_color(Node parent, const Theme& theme, const std::string& placeholder)
    {
        for (auto node : parent.children())
        {
            const auto kind = local_name(node.name());
            std::string color;
            if (kind == "srgbClr")
            {
                color = hex_color(node.attribute("val").value());
            }
            else if (kind == "sysClr")
            {
                color = hex_color(node.attribute("lastClr").value());
            }
            else if (kind == "schemeClr")
            {
                std::string key = node.attribute("val").value();
                if (key == "phClr")
                {
                    color = placeholder;
                }
                else
                {
                    const auto alias = theme.aliases.find(key);
                    key = alias == theme.aliases.end() ? key : alias->second;
                    const auto found = theme.colors.find(key);
                    color = found == theme.colors.end() ? "#000000" : found->second;
                }
            }
            else if (kind == "prstClr")
            {
                const std::map<std::string, std::string> presets{{"black", "#000000"}, {"white", "#FFFFFF"},
                    {"red", "#FF0000"}, {"green", "#008000"}, {"blue", "#0000FF"}, {"yellow", "#FFFF00"},
                    {"gray", "#808080"}, {"ltGray", "#D3D3D3"}, {"dkGray", "#A9A9A9"}, {"orange", "#FFA500"},
                    {"purple", "#800080"}, {"cyan", "#00FFFF"}};
                const auto found = presets.find(node.attribute("val").value());
                color = found == presets.end() ? "#000000" : found->second;
                theme.approximated_color = theme.approximated_color || found == presets.end();
            }
            else if (kind == "scrgbClr")
            {
                std::array<double, 3> values{number(node.attribute("r")) / 100000,
                    number(node.attribute("g")) / 100000, number(node.attribute("b")) / 100000};
                for (auto& value : values)
                {
                    value = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - 0.055;
                }
                color = color_hex(values);
            }
            else if (kind == "hslClr")
            {
                const double hue = std::fmod(number(node.attribute("hue")) / 60000, 360.0) / 60;
                const double saturation = std::clamp(number(node.attribute("sat")) / 100000, 0.0, 1.0);
                const double lightness = std::clamp(number(node.attribute("lum")) / 100000, 0.0, 1.0);
                const double chroma = (1 - std::abs(2 * lightness - 1)) * saturation;
                const double intermediate = chroma * (1 - std::abs(std::fmod(hue, 2.0) - 1));
                std::array<double, 3> values;
                if (hue < 1)
                {
                    values = {chroma, intermediate, 0};
                }
                else if (hue < 2)
                {
                    values = {intermediate, chroma, 0};
                }
                else if (hue < 3)
                {
                    values = {0, chroma, intermediate};
                }
                else if (hue < 4)
                {
                    values = {0, intermediate, chroma};
                }
                else if (hue < 5)
                {
                    values = {intermediate, 0, chroma};
                }
                else
                {
                    values = {chroma, 0, intermediate};
                }
                for (auto& value : values)
                {
                    value += lightness - chroma / 2;
                }
                color = color_hex(values);
            }
            else
            {
                continue;
            }
            auto values = color_channels(color);
            double opacity = 1;
            for (auto modifier : node.children())
            {
                const auto operation = local_name(modifier.name());
                const double amount = number(modifier.attribute("val")) / 100000.0;
                if (operation == "alpha")
                {
                    opacity = amount;
                }
                else if (operation == "alphaMod")
                {
                    opacity *= amount;
                }
                else if (operation == "alphaOff")
                {
                    opacity += amount;
                }
                else if (operation == "lumMod" || operation == "lumOff")
                {
                    change_luminance(
                        values, operation == "lumMod" ? amount : 1, operation == "lumOff" ? amount : 0);
                }
                else if (operation == "satMod")
                {
                    change_saturation(values, amount);
                }
                else if (operation == "shade" || operation == "tint")
                {
                    for (auto& value : values)
                    {
                        value = operation == "shade" ? value * amount : value * amount + 1 - amount;
                    }
                }
                else
                {
                    theme.approximated_color = true;
                }
            }
            return {color.empty() ? "#000000" : color_hex(values), std::clamp(opacity, 0.0, 1.0)};
        }
        return {{}, 1};
    }

    void apply_color_map(Theme& theme, Node mapping)
    {
        for (auto alias : mapping.attributes())
        {
            theme.aliases[local_name(alias.name())] = alias.value();
        }
    }

    Theme read_theme(Node root)
    {
        Theme result;
        result.colors = {{"dk1", "#000000"}, {"lt1", "#FFFFFF"}, {"dk2", "#1F497D"}, {"lt2", "#EEECE1"},
            {"accent1", "#4F81BD"}, {"accent2", "#C0504D"}, {"accent3", "#9BBB59"}, {"accent4", "#8064A2"},
            {"accent5", "#4BACC6"}, {"accent6", "#F79646"}, {"hlink", "#0000FF"}, {"folHlink", "#800080"}};
        const auto elements = child(root, "themeElements");
        for (auto scheme : child(elements, "clrScheme").children())
        {
            const auto color = read_color(scheme, result);
            if (!color.first.empty())
            {
                result.colors[local_name(scheme.name())] = color.first;
            }
        }
        const auto fonts = child(elements, "fontScheme");
        for (const auto& name : {"majorFont", "minorFont"})
        {
            const auto font = child(fonts, name);
            auto& latin = std::string(name) == "majorFont" ? result.major_font : result.minor_font;
            auto& east_asian =
                std::string(name) == "majorFont" ? result.major_east_asian : result.minor_east_asian;
            const std::string specified = child(font, "latin").attribute("typeface").value();
            if (!specified.empty())
            {
                latin = specified;
            }
            east_asian = child(font, "ea").attribute("typeface").value();
            for (auto script : font.children())
            {
                if (east_asian.empty() && std::string(script.attribute("script").value()) == "Hans")
                {
                    east_asian = script.attribute("typeface").value();
                }
            }
        }
        result.format = child(elements, "fmtScheme");
        return result;
    }

}
