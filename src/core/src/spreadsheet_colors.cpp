#include "spreadsheet_colors.hpp"
#include "office_color.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace
{
    pugi::xml_node child(pugi::xml_node parent, const char* name)
    {
        for (auto item : parent.children())
        {
            const std::string_view qualified(item.name());
            const auto colon = qualified.find(':');
            if (qualified.substr(colon == qualified.npos ? 0 : colon + 1) == name)
                return item;
        }
        return {};
    }

    int index(pugi::xml_attribute attribute)
    {
        const std::string_view text(attribute.value());
        int value = -1;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? value : -1;
    }

    std::string argb(const std::string& text)
    {
        if (text.size() == 8 && text.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
            return mirrorfly::office_color::rgb(text.substr(2));
        return mirrorfly::office_color::rgb(text);
    }
}

namespace mirrorfly
{
    SpreadsheetColors::SpreadsheetColors(pugi::xml_node styles, pugi::xml_node theme)
    {
        // SpreadsheetML's first slots are light/dark, unlike DrawingML's XML child order.
        const char* slots[]{"lt1", "dk1", "lt2", "dk2", "accent1", "accent2", "accent3", "accent4", "accent5",
            "accent6", "hlink", "folHlink"};
        const auto scheme = child(child(theme, "themeElements"), "clrScheme");
        for (std::size_t i = 0; i < theme_.size(); ++i)
        {
            const auto entry = child(scheme, slots[i]);
            theme_[i] = office_color::rgb(child(entry, "srgbClr").attribute("val").value());
            if (theme_[i].empty())
                theme_[i] = office_color::rgb(child(entry, "sysClr").attribute("lastClr").value());
        }
        // System text/background remain useful for theme-less workbooks created by this app.
        if (!theme)
        {
            theme_[0] = "#FFFFFF";
            theme_[1] = "#000000";
        }
        constexpr const char* palette[]{"000000", "FFFFFF", "FF0000", "00FF00", "0000FF", "FFFF00", "FF00FF",
            "00FFFF", "000000", "FFFFFF", "FF0000", "00FF00", "0000FF", "FFFF00", "FF00FF", "00FFFF",
            "800000", "008000", "000080", "808000", "800080", "008080", "C0C0C0", "808080", "9999FF",
            "993366", "FFFFCC", "CCFFFF", "660066", "FF8080", "0066CC", "CCCCFF", "000080", "FF00FF",
            "FFFF00", "00FFFF", "800080", "800000", "008080", "0000FF", "00CCFF", "CCFFFF", "CCFFCC",
            "FFFF99", "99CCFF", "FF99CC", "CC99FF", "FFCC99", "3366FF", "33CCCC", "99CC00", "FFCC00",
            "FF9900", "FF6600", "666699", "969696", "003366", "339966", "003300", "333300", "993300",
            "993366", "333399", "333333"};
        for (const auto* color : palette)
            indexed_.push_back(office_color::rgb(color));
        const auto custom = child(child(styles, "colors"), "indexedColors");
        if (custom)
        {
            indexed_.clear();
            for (auto color : custom.children())
            {
                if (indexed_.size() >= 64)
                    break;
                indexed_.push_back(argb(color.attribute("rgb").value()));
            }
        }
    }

    std::string SpreadsheetColors::resolve(pugi::xml_node color) const
    {
        if (!color)
            return {};
        std::string result;
        if (color.attribute("auto").as_bool())
            result = "#000000";
        else if (color.attribute("rgb"))
            result = argb(color.attribute("rgb").value());
        else if (color.attribute("theme"))
        {
            const int slot = index(color.attribute("theme"));
            if (slot >= 0 && slot < static_cast<int>(theme_.size()))
                result = theme_[slot];
        }
        else if (color.attribute("indexed"))
        {
            const int slot = index(color.attribute("indexed"));
            if (slot == 64 || slot == 65)
                result = slot == 64 ? "#000000" : "#FFFFFF";
            else if (slot >= 0 && slot < static_cast<int>(indexed_.size()))
                result = indexed_[slot];
        }
        if (result.empty() || !color.attribute("tint"))
            return result;
        const std::string text = color.attribute("tint").value();
        char* end = nullptr;
        const double amount = std::strtod(text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size())
            return {};
        return office_color::tint(result, amount);
    }
}
