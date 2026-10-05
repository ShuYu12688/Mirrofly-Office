#include "presentation_transition.hpp"

#include <cmath>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    std::string local(const char* name)
    {
        const auto colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    bool contains_transition(pugi::xml_node node)
    {
        for (auto child : node.children())
        {
            if (local(child.name()) == "transition" || contains_transition(child))
                return true;
        }
        return false;
    }

    const char* speed(double duration)
    {
        if (std::abs(duration - 0.3) < 1e-6)
            return "fast";
        if (std::abs(duration - 1.0) < 1e-6)
            return "slow";
        return "med";
    }

    void write_transition(pugi::xml_node node, const mirrorfly::PresentationTransition& transition)
    {
        node.append_attribute("spd") = speed(transition.duration);
        node.append_attribute("advClick") = transition.advance_on_click ? "1" : "0";
        if (transition.advance_after >= 0)
            node.append_attribute("advTm") =
                static_cast<unsigned int>(std::llround(transition.advance_after * 1000));
        auto effect = node.append_child(("p:" + transition.type).c_str());
        if (transition.type == "push")
            effect.append_attribute("dir") = transition.direction.c_str();
    }
}

namespace mirrorfly
{
    bool valid_presentation_transition(const PresentationTransition& transition)
    {
        if (transition.type != "cut" && transition.type != "fade" && transition.type != "push")
            return false;
        if ((transition.type == "push" && transition.direction != "l" && transition.direction != "r" &&
                transition.direction != "u" && transition.direction != "d") ||
            (transition.type != "push" && !transition.direction.empty()) || !transition.orientation.empty())
            return false;
        if (!std::isfinite(transition.duration) ||
            (std::abs(transition.duration - 0.3) >= 1e-6 && std::abs(transition.duration - 0.5) >= 1e-6 &&
                std::abs(transition.duration - 1.0) >= 1e-6))
            return false;
        return std::isfinite(transition.advance_after) &&
            (transition.advance_after == -1 ||
                (transition.advance_after >= 0 && transition.advance_after <= 86400));
    }

    bool editable_presentation_transition(pugi::xml_node slide_root)
    {
        for (auto child : slide_root.children())
            if (local(child.name()) == "AlternateContent" && contains_transition(child))
                return false;
        pugi::xml_node transition;
        for (auto child : slide_root.children())
            if (local(child.name()) == "transition")
            {
                if (transition)
                    return false;
                transition = child;
            }
        if (!transition)
            return true;
        for (auto attribute : transition.attributes())
        {
            const std::string name = attribute.name();
            if (name == "spd" || name == "advClick" || name == "advTm" || name == "p14:dur" ||
                name == "dur" || name.compare(0, 5, "xmlns") == 0)
                continue;
            return false;
        }
        static const std::set<std::string> effects{
            "cover", "cut", "fade", "pull", "push", "split", "wipe", "zoom"};
        bool found = false;
        for (auto child : transition.children())
        {
            if (child.type() != pugi::node_element)
                continue;
            if (found || !effects.count(local(child.name())) || child.first_child())
                return false;
            for (auto attribute : child.attributes())
            {
                const std::string name = attribute.name();
                if (name != "dir" && name != "orient" && name != "thruBlk" && name != "spokes")
                    return false;
            }
            found = true;
        }
        return true;
    }

    std::string presentation_transition_xml(const PresentationTransition& transition)
    {
        if (transition.type.empty())
            return {};
        if (!valid_presentation_transition(transition))
            throw std::runtime_error("页面切换参数无效。");
        pugi::xml_document document;
        write_transition(document.append_child("p:transition"), transition);
        std::ostringstream output;
        document.save(output, "", pugi::format_raw | pugi::format_no_declaration);
        return output.str();
    }

    void patch_presentation_transition(pugi::xml_node slide_root, const PresentationTransition& transition)
    {
        if (!valid_presentation_transition(transition) || !editable_presentation_transition(slide_root))
            throw std::runtime_error("当前页面切换包含无法安全覆盖的内容。");
        pugi::xml_node old;
        pugi::xml_node anchor;
        for (auto child : slide_root.children())
        {
            const auto name = local(child.name());
            if (name == "transition")
                old = child;
            if (name == "clrMapOvr" || name == "cSld")
                anchor = child;
        }
        if (old)
            slide_root.remove_child(old);
        pugi::xml_node node;
        if (anchor)
            node = slide_root.insert_child_after("p:transition", anchor);
        else
            node = slide_root.prepend_child("p:transition");
        if (!node)
            throw std::runtime_error("无法写入页面切换。");
        write_transition(node, transition);
    }
}
