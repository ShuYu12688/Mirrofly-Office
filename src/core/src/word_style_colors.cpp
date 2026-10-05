#include "office_color.hpp"
#include "word_styles.hpp"
#include "word_xml.hpp"

namespace
{
    pugi::xml_node drawing_child(pugi::xml_node node, const char* name)
    {
        for (auto item : node.children())
            if (mirrorfly::word_xml::named(
                    item, name, "http://schemas.openxmlformats.org/drawingml/2006/main"))
                return item;
        return {};
    }
}

namespace mirrorfly::word_detail
{
    using namespace word_xml;

    std::string StyleResolver::theme_color(pugi::xml_node color, bool fill) const
    {
        const auto palette =
            drawing_child(drawing_child(theme_.document_element(), "themeElements"), "clrScheme");
        const char* theme_key = fill ? "themeFill" : "themeColor";
        std::string name = attribute(color, theme_key).value();
        static const std::map<std::string, std::string> aliases{{"dark1", "dk1"}, {"dark2", "dk2"},
            {"light1", "lt1"}, {"light2", "lt2"}, {"text1", "dk1"}, {"text2", "dk2"}, {"background1", "lt1"},
            {"background2", "lt2"}, {"hyperlink", "hlink"}, {"followedHyperlink", "folHlink"}};
        if (const auto alias = aliases.find(name); alias != aliases.end())
            name = alias->second;
        if (name.empty())
            return {};
        const auto entry = drawing_child(palette, name.c_str());
        auto rgb = office_color::rgb(drawing_child(entry, "srgbClr").attribute("val").value());
        if (rgb.empty())
            rgb = office_color::rgb(drawing_child(entry, "sysClr").attribute("lastClr").value());
        if (rgb.empty())
            return {};
        const auto tint = attribute(color, fill ? "themeFillTint" : "themeTint");
        const auto shade = attribute(color, fill ? "themeFillShade" : "themeShade");
        // Word gives tint precedence when both attributes are present.
        const auto transform = tint ? tint : shade;
        if (transform)
        {
            const std::string value = transform.value();
            if (value.size() != 2 || value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                return {};
            const double factor = std::stoul(value, nullptr, 16) / 255.0;
            rgb = office_color::tint(rgb, tint ? 1 - factor : factor - 1);
        }
        return rgb;
    }
}
