#include "word_property_patch.hpp"
#include "word_tabs.hpp"
#include "word_xml.hpp"

#include <map>
#include <set>

namespace
{
    using namespace mirrorfly::word_xml;

    int order(const std::string& kind, const std::string& name)
    {
        static const std::map<std::string, std::vector<std::string>> orders{
            {"pPr",
                {"pStyle", "keepNext", "keepLines", "pageBreakBefore", "framePr", "widowControl", "numPr",
                    "suppressLineNumbers", "pBdr", "shd", "tabs", "suppressAutoHyphens", "kinsoku",
                    "wordWrap", "overflowPunct", "topLinePunct", "autoSpaceDE", "autoSpaceDN", "bidi",
                    "adjustRightInd", "snapToGrid", "spacing", "ind", "contextualSpacing", "mirrorIndents",
                    "suppressOverlap", "jc", "textDirection", "textAlignment", "textboxTightWrap",
                    "outlineLvl", "divId", "cnfStyle", "rPr", "sectPr", "pPrChange"}},
            {"rPr",
                {"rStyle", "rFonts", "b", "bCs", "i", "iCs", "caps", "smallCaps", "strike", "dstrike",
                    "outline", "shadow", "emboss", "imprint", "noProof", "snapToGrid", "vanish", "webHidden",
                    "color", "spacing", "w", "kern", "position", "sz", "szCs", "highlight", "u", "effect",
                    "bdr", "shd", "fitText", "vertAlign", "rtl", "cs", "em", "lang", "eastAsianLayout",
                    "specVanish", "oMath", "rPrChange"}},
            {"numPr", {"ilvl", "numId", "numberingChange", "ins"}},
            {"tcPr",
                {"cnfStyle", "tcW", "gridSpan", "hMerge", "vMerge", "tcBorders", "shd", "noWrap", "tcMar",
                    "textDirection", "tcFitText", "vAlign", "hideMark", "headers", "cellIns", "cellDel",
                    "cellMerge", "tcPrChange"}},
            {"tcBorders",
                {"top", "left", "start", "bottom", "right", "end", "insideH", "insideV", "tl2br", "tr2bl"}},
            {"tcMar", {"top", "left", "start", "bottom", "right", "end"}}};
        const auto found = orders.find(kind);
        if (found == orders.end())
            return -1;
        const auto position = std::find(found->second.begin(), found->second.end(), name);
        return position == found->second.end() ? -1 : static_cast<int>(position - found->second.begin());
    }

    void set(pugi::xml_node node, const std::string& key, const std::string& value, const std::string& prefix)
    {
        auto target = attribute(node, key.c_str());
        if (!target)
            target = node.append_attribute((prefix + ":" + key).c_str());
        target = value.c_str();
    }

    void erase(pugi::xml_node node, const char* key)
    {
        if (auto value = attribute(node, key))
            node.remove_attribute(value);
    }

    void clear_conflicts(pugi::xml_node target, const std::string& key)
    {
        const std::string name(local(target.name()));
        if (name == "rFonts")
        {
            const std::map<std::string, const char*> themes{{"ascii", "asciiTheme"}, {"hAnsi", "hAnsiTheme"},
                {"eastAsia", "eastAsiaTheme"}, {"cs", "cstheme"}};
            const auto theme = themes.find(key);
            if (theme != themes.end())
                erase(target, theme->second);
        }
        if ((name == "color" && key == "val") || key == "color")
            for (const auto* value : {"themeColor", "themeTint", "themeShade"})
                erase(target, value);
        if (name == "shd" && key == "fill")
            for (const auto* value : {"themeFill", "themeFillTint", "themeFillShade"})
                erase(target, value);
        if (name == "spacing" && (key == "before" || key == "after"))
        {
            erase(target, (key + "Lines").c_str());
            erase(target, (key + "Autospacing").c_str());
        }
        if (name == "ind")
        {
            if (key == "left")
                for (const auto* value : {"start", "startChars", "leftChars"})
                    erase(target, value);
            if (key == "right")
                for (const auto* value : {"end", "endChars", "rightChars"})
                    erase(target, value);
            if (key == "firstLine" || key == "hanging")
            {
                erase(target, "firstLineChars");
                erase(target, "hangingChars");
                erase(target, key == "firstLine" ? "hanging" : "firstLine");
            }
        }
    }

    pugi::xml_node neutral(pugi::xml_node holder, const std::string& name)
    {
        static const std::map<std::string, std::string> values{{"keepNext", "0"}, {"pageBreakBefore", "0"},
            {"bidi", "0"}, {"outlineLvl", "9"}, {"vertAlign", "baseline"}, {"spacing", "0"},
            {"color", "auto"}, {"bdr", "nil"}, {"top", "nil"}, {"bottom", "nil"}, {"left", "nil"},
            {"right", "nil"}};
        auto node = holder.append_child(("w:" + name).c_str());
        if (const auto value = values.find(name); value != values.end())
            node.append_attribute("w:val") = value->second.c_str();
        else if (name == "ind")
        {
            node.append_attribute("w:left") = 0;
            node.append_attribute("w:right") = 0;
            node.append_attribute("w:firstLine") = 0;
        }
        else if (name == "shd")
        {
            node.append_attribute("w:val") = "clear";
            node.append_attribute("w:fill") = "auto";
        }
        else if (name == "pBdr")
        {
            for (const auto* edge : {"w:top", "w:left", "w:bottom", "w:right", "w:between", "w:bar"})
                node.append_child(edge).append_attribute("w:val") = "nil";
        }
        else
        {
            holder.remove_child(node);
            return {};
        }
        return node;
    }

    void patch_node(
        pugi::xml_node target, pugi::xml_node before, pugi::xml_node after, const std::string& prefix)
    {
        std::set<std::string> keys;
        for (auto node : {before, after})
            for (auto value : node.attributes())
                if (std::string_view(value.name()).find(':') != std::string_view::npos &&
                    namespace_is(node, value.name(), main_ns))
                    keys.insert(std::string(local(value.name())));
        for (const auto& key : keys)
        {
            const auto previous = attribute(before, key.c_str());
            const auto next = attribute(after, key.c_str());
            if (bool(previous) == bool(next) && std::string_view(previous.value()) == next.value())
                continue;
            if (!next)
                erase(target, key.c_str());
            else
            {
                clear_conflicts(target, key);
                set(target, key, next.value(), prefix);
            }
        }
        mirrorfly::word_detail::patch_properties(
            target.parent(), before, after, std::string(local(target.name())).c_str(), prefix);
    }
}

namespace mirrorfly::word_detail
{
    pugi::xml_node ordered_property(pugi::xml_node target, pugi::xml_node next, const char* kind)
    {
        const int position = order(kind, std::string(local(next.name())));
        if (position >= 0)
            for (auto item : target.children())
                if (named(item, std::string(local(item.name())).c_str()) &&
                    order(kind, std::string(local(item.name()))) > position)
                    return target.insert_copy_before(next, item);
        return target.append_copy(next);
    }

    void patch_properties(pugi::xml_node raw, pugi::xml_node before, pugi::xml_node after, const char* kind,
        const std::string& prefix, const std::string& skip)
    {
        std::set<std::string> names;
        for (auto node : {before, after})
            for (auto item : node.children())
                names.insert(std::string(local(item.name())));
        auto target = child(raw, kind);
        pugi::xml_document temporary;
        auto holder = temporary.append_child("w:properties");
        holder.append_attribute("xmlns:w") = main_ns;
        for (const auto& name : names)
        {
            if (name == skip)
                continue;
            const auto previous = child(before, name.c_str());
            auto next = child(after, name.c_str());
            if (bytes(previous) == bytes(next))
                continue;
            if (name == "tabs")
            {
                if (!target)
                    target = raw.prepend_child((prefix + ":" + kind).c_str());
                if (!child(target, "tabs"))
                    ordered_property(target, holder.append_child((prefix + ":tabs").c_str()), kind);
                patch_tab_stops(target, previous, next, prefix);
                continue;
            }
            if (!next)
                next = neutral(holder, name);
            if (!target)
                target = raw.prepend_child((prefix + ":" + kind).c_str());
            auto existing = child(target, name.c_str());
            if (!next)
            {
                if (existing)
                    target.remove_child(existing);
                continue;
            }
            if (!existing)
            {
                auto empty = holder.append_child((prefix + ":" + name).c_str());
                existing = ordered_property(target, empty, kind);
                for (auto value : next.attributes())
                    if (std::string_view(value.name()).find(':') != std::string_view::npos &&
                        namespace_is(next, value.name(), main_ns))
                        set(existing, std::string(local(value.name())), value.value(), prefix);
            }
            patch_node(existing, previous, next, prefix);
            // A combined UI property must explicitly cancel competing inherited properties.
            const char* companion = nullptr;
            if (std::string_view(kind) == "rPr" && name == "shd")
                companion = "highlight";
            if (companion)
            {
                auto node = child(target, companion);
                if (!node)
                    node = ordered_property(
                        target, holder.append_child((prefix + ":" + companion).c_str()), kind);
                set(node, "val", "none", prefix);
            }
        }
    }
}
