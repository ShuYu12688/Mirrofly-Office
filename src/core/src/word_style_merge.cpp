#include "word_style_merge.hpp"
#include "word_tabs.hpp"
#include "word_xml.hpp"

#include <set>

namespace mirrorfly::word_detail
{
    using namespace word_xml;
    bool enabled(pugi::xml_node node)
    {
        const std::string_view value = attribute(node, "val").value();
        return node && value != "0" && value != "false" && value != "off";
    }

    void set(pugi::xml_node node, const char* name, const std::string& value)
    {
        auto target = attribute(node, name);
        if (!target)
            target = node.append_attribute(("w:" + std::string(name)).c_str());
        target = value.c_str();
    }

    void remove_attribute(pugi::xml_node node, const char* name)
    {
        if (auto value = attribute(node, name))
            node.remove_attribute(value);
    }

    void merge_properties(pugi::xml_node target, pugi::xml_node source, bool style)
    {
        static const std::set<std::string> toggles{"b", "bCs", "i", "iCs", "caps", "smallCaps", "strike",
            "outline", "shadow", "emboss", "imprint", "vanish"};
        static const std::set<std::string> switches{"dstrike", "noProof", "snapToGrid", "webHidden", "rtl",
            "cs", "keepNext", "keepLines", "pageBreakBefore", "bidi", "widowControl", "contextualSpacing"};
        for (auto value : source.attributes())
            if (namespace_is(source, value.name(), main_ns) &&
                std::string_view(value.name()).find(':') != std::string_view::npos)
                set(target, std::string(local(value.name())).c_str(), value.value());
        for (auto item : source.children())
        {
            const std::string name(local(item.name()));
            if (!named(item, name.c_str()))
                continue;
            auto existing = child(target, name.c_str());
            if (existing && (local(source.name()) == "tcBorders" || local(source.name()) == "tblBorders"))
            {
                // Each edge is one border property; omitted attributes use border defaults.
                target.remove_child(existing);
                existing = {};
            }
            const bool previous = enabled(existing);
            if (!existing)
                existing = target.append_child(("w:" + name).c_str());
            if (toggles.count(name) || switches.count(name))
            {
                const bool value = style && toggles.count(name) ? previous != enabled(item) : enabled(item);
                set(existing, "val", value ? "1" : "0");
                if (value && (name == "strike" || name == "dstrike"))
                {
                    // A more specific active line replaces the other inherited strike variant.
                    if (auto other = child(target, name == "strike" ? "dstrike" : "strike"))
                        set(other, "val", "0");
                }
                continue;
            }
            if (name == "rFonts")
            {
                const std::pair<const char*, const char*> choices[] = {{"ascii", "asciiTheme"},
                    {"hAnsi", "hAnsiTheme"}, {"eastAsia", "eastAsiaTheme"}, {"cs", "cstheme"}};
                for (const auto& choice : choices)
                    if (attribute(item, choice.first) || attribute(item, choice.second))
                    {
                        remove_attribute(existing, choice.first);
                        remove_attribute(existing, choice.second);
                    }
            }
            else if (name == "ind")
            {
                if (attribute(item, "firstLine") || attribute(item, "firstLineChars"))
                {
                    remove_attribute(existing, "hanging");
                    remove_attribute(existing, "hangingChars");
                }
                else if (attribute(item, "hanging") || attribute(item, "hangingChars"))
                {
                    remove_attribute(existing, "firstLine");
                    remove_attribute(existing, "firstLineChars");
                }
                if (attribute(item, "left"))
                    remove_attribute(existing, "start");
                if (attribute(item, "start"))
                    remove_attribute(existing, "left");
                if (attribute(item, "right"))
                    remove_attribute(existing, "end");
                if (attribute(item, "end"))
                    remove_attribute(existing, "right");
            }
            else if (name == "color" && (attribute(item, "val") || attribute(item, "themeColor")))
            {
                for (const auto* key : {"val", "themeColor", "themeTint", "themeShade"})
                    remove_attribute(existing, key);
            }
            else if (name == "u" && !attribute(item, "val"))
                set(existing, "val", "single");
            if (attribute(item, "fill") || attribute(item, "themeFill"))
                for (const auto* key : {"fill", "themeFill", "themeFillTint", "themeFillShade"})
                    remove_attribute(existing, key);
            if (name != "color" && (attribute(item, "color") || attribute(item, "themeColor")))
                for (const auto* key : {"color", "themeColor", "themeTint", "themeShade"})
                    remove_attribute(existing, key);
            if (name == "tabs")
                merge_tab_stops(existing, item);
            else
                merge_properties(existing, item, style);
        }
    }

    pugi::xml_node property_root(pugi::xml_document& xml, const char* name)
    {
        xml.reset();
        auto node = xml.append_child(name);
        node.append_attribute("xmlns:w") = main_ns;
        return node;
    }

}
