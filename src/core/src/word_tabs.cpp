#include "word_tabs.hpp"
#include "word_style_merge.hpp"
#include "word_xml.hpp"
#include <cmath>
#include <limits>
#include <map>

namespace mirrorfly::word_detail
{
    namespace
    {
        using namespace word_xml;
        pugi::xml_node at(pugi::xml_node tabs, int position)
        {
            for (auto node : tabs.children())
                if (named(node, "tab") && attribute(node, "pos").as_int() == position)
                    return node;
            return {};
        }
        pugi::xml_node insert(pugi::xml_node tabs, int position, const std::string& prefix)
        {
            const auto name = prefix + ":tab";
            for (auto node : tabs.children())
                if (named(node, "tab") && attribute(node, "pos").as_int() > position)
                    return tabs.insert_child_before(name.c_str(), node);
            return tabs.append_child(name.c_str());
        }
        void attribute_value(
            pugi::xml_node node, const char* key, const std::string& value, const std::string& prefix)
        {
            auto field = attribute(node, key);
            if (!field)
                field = node.append_attribute((prefix + ":" + key).c_str());
            field = value.c_str();
        }
    }

    bool valid_tab_stops(const std::vector<WordTabStop>& stops)
    {
        if (stops.size() > 64)
            return false;
        double previous = -1638.4;
        for (const auto& stop : stops)
        {
            if (!std::isfinite(stop.position) || stop.position < -1638.35 || stop.position > 1638.35 ||
                stop.position <= previous ||
                std::abs(stop.position * 20 - std::round(stop.position * 20)) > 0.0001 ||
                stop.alignment.empty() || stop.alignment.size() > 32 || stop.leader.empty() ||
                stop.leader.size() > 32)
                return false;
            previous = stop.position;
        }
        return true;
    }

    std::vector<WordTabStop> read_tab_stops(pugi::xml_node tabs)
    {
        std::map<int, WordTabStop> sorted;
        for (auto node : tabs.children())
            if (named(node, "tab"))
            {
                const int position = attribute(node, "pos").as_int();
                const std::string alignment = attribute(node, "val").as_string("left");
                if (alignment == "clear")
                    sorted.erase(position);
                else
                    sorted[position] = {
                        position / 20.0, alignment, attribute(node, "leader").as_string("none")};
            }
        std::vector<WordTabStop> result;
        for (const auto& [position, stop] : sorted)
            result.push_back(stop);
        return result;
    }

    void write_tab_stops(pugi::xml_node properties, const std::vector<WordTabStop>& stops)
    {
        if (stops.empty())
            return;
        auto tabs = properties.append_child("w:tabs");
        for (const auto& stop : stops)
        {
            auto node = tabs.append_child("w:tab");
            node.append_attribute("w:pos") = static_cast<int>(std::round(stop.position * 20));
            node.append_attribute("w:val") = stop.alignment.c_str();
            node.append_attribute("w:leader") = stop.leader.c_str();
        }
    }

    void merge_tab_stops(pugi::xml_node target, pugi::xml_node source)
    {
        for (auto node : source.children())
            if (named(node, "tab"))
            {
                const int position = attribute(node, "pos").as_int();
                if (auto old = at(target, position))
                    target.remove_child(old);
                if (std::string_view(attribute(node, "val").value()) == "clear")
                    continue;
                auto added = insert(target, position, "w");
                for (const auto* key : {"pos", "val", "leader"})
                    if (auto field = attribute(node, key))
                        set(added, key, field.value());
            }
    }

    void patch_tab_stops(
        pugi::xml_node properties, pugi::xml_node before, pugi::xml_node after, const std::string& prefix)
    {
        auto tabs = child(properties, "tabs");
        if (!tabs)
            tabs = properties.append_child((prefix + ":tabs").c_str());
        std::map<int, pugi::xml_node> changes;
        for (auto node : before.children())
            if (named(node, "tab"))
                changes[attribute(node, "pos").as_int()] = {};
        for (auto node : after.children())
            if (named(node, "tab"))
                changes[attribute(node, "pos").as_int()] = node;
        for (const auto& [position, next] : changes)
        {
            const auto previous = at(before, position);
            if (bytes(previous) == bytes(next))
                continue;
            auto target = at(tabs, position);
            if (!target)
                target = insert(tabs, position, prefix);
            attribute_value(target, "pos", std::to_string(position), prefix);
            attribute_value(target, "val", next ? attribute(next, "val").as_string("left") : "clear", prefix);
            attribute_value(
                target, "leader", next ? attribute(next, "leader").as_string("none") : "none", prefix);
        }
    }

    bool read_tab_interval(const std::vector<OfficePart>& parts, double& points, std::string& error)
    {
        const auto path = document_relationship(parts, "settings", error);
        if (!error.empty())
            return false;
        if (path.empty())
            return true;
        const auto source = part(parts, path);
        pugi::xml_document xml;
        if (!source || !read(source->bytes, xml) || !named(xml.document_element(), "settings"))
        {
            error = "DOCX 文档设置缺失或无效。";
            return false;
        }
        if (const auto stop = child(xml.document_element(), "defaultTabStop"))
            points = attribute(stop, "val").as_double(720) / 20;
        return true;
    }

    void write_tab_interval(std::vector<OfficePart>& parts, double points)
    {
        if (points == 36)
            return;
        pugi::xml_document settings, rels, types;
        auto root = settings.append_child("w:settings");
        root.append_attribute("xmlns:w") = main_ns;
        root.append_child("w:defaultTabStop").append_attribute("w:val") =
            static_cast<int>(std::round(points * 20));
        const auto xml = bytes(root);
        parts.insert(parts.end() - 1, {"word/settings.xml", xml});
        read(part(parts, "word/_rels/document.xml.rels")->bytes, rels);
        auto rel = rels.document_element().append_child("Relationship");
        rel.append_attribute("Id") = "settings";
        rel.append_attribute("Type") =
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings";
        rel.append_attribute("Target") = "settings.xml";
        replace_part(parts, "word/_rels/document.xml.rels", rels);
        read(part(parts, "[Content_Types].xml")->bytes, types);
        auto type = types.document_element().append_child("Override");
        type.append_attribute("PartName") = "/word/settings.xml";
        type.append_attribute("ContentType") =
            "application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml";
        replace_part(parts, "[Content_Types].xml", types);
    }
}
