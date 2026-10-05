#pragma once

#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <sstream>
#include <string_view>

namespace mirrorfly::word_xml
{
    inline constexpr auto main_ns = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    inline constexpr auto rel_ns = "http://schemas.openxmlformats.org/package/2006/relationships";
    inline constexpr auto content_ns = "http://schemas.openxmlformats.org/package/2006/content-types";
    inline constexpr auto parse_options =
        pugi::parse_default | pugi::parse_ws_pcdata_single | pugi::parse_comments | pugi::parse_pi;

    inline std::string_view local(const char* name)
    {
        const std::string_view value(name);
        const auto colon = value.find(':');
        return colon == value.npos ? value : value.substr(colon + 1);
    }

    inline bool namespace_is(pugi::xml_node node, const char* name, const char* space)
    {
        const std::string value(name);
        const auto colon = value.find(':');
        const auto key = colon == value.npos ? std::string("xmlns") : "xmlns:" + value.substr(0, colon);
        for (; node; node = node.parent())
            if (const auto declared = node.attribute(key.c_str()))
                return std::string_view(declared.value()) == space;
        return false;
    }

    inline bool named(pugi::xml_node node, const char* name, const char* space = main_ns)
    {
        return local(node.name()) == name && namespace_is(node, node.name(), space);
    }

    inline pugi::xml_node child(pugi::xml_node node, const char* name)
    {
        for (auto item : node.children())
            if (named(item, name))
                return item;
        return {};
    }

    inline pugi::xml_attribute attribute(pugi::xml_node node, const char* name)
    {
        for (auto item : node.attributes())
            if (std::string_view(item.name()).find(':') != std::string_view::npos &&
                local(item.name()) == name && namespace_is(node, item.name(), main_ns))
                return item;
        return {};
    }

    inline bool plain_run(pugi::xml_node node)
    {
        if (!named(node, "r"))
            return false;
        for (auto item : node.children())
            if (!named(item, "rPr") && !named(item, "t") && !named(item, "tab") && !named(item, "br") &&
                !named(item, "cr"))
                return false;
        return true;
    }

    inline bool plain_paragraph(pugi::xml_node node, bool allow_section = false)
    {
        if (!named(node, "p") || (!allow_section && child(child(node, "pPr"), "sectPr")))
            return false;
        for (auto item : node.children())
            if (!named(item, "pPr") && !plain_run(item))
                return false;
        return true;
    }

    inline const OfficePart* part(const std::vector<OfficePart>& parts, const std::string& path)
    {
        const auto found = std::find_if(parts.begin(), parts.end(), [&path](const auto& value)
        {
            return value.path == path;
        });
        return found == parts.end() ? nullptr : &*found;
    }

    inline bool read(const std::string& data, pugi::xml_document& xml)
    {
        if (data.size() > maximum_word_xml_bytes || data.find("<!DOCTYPE") != data.npos ||
            data.find("<!ENTITY") != data.npos || !xml.load_buffer(data.data(), data.size(), parse_options))
            return false;
        std::vector<std::pair<pugi::xml_node, int>> pending{{xml, 0}};
        std::size_t count = 0;
        while (!pending.empty())
        {
            const auto item = pending.back();
            pending.pop_back();
            if (item.second > 96 || ++count > 1000000)
                return false;
            for (auto node : item.first.children())
                pending.emplace_back(node, item.second + 1);
        }
        return true;
    }

    inline std::string bytes(pugi::xml_node node)
    {
        std::ostringstream stream;
        node.print(stream, "", pugi::format_raw);
        return stream.str();
    }

    inline void replace_part(std::vector<OfficePart>& parts, const std::string& path, pugi::xml_node xml)
    {
        for (auto& value : parts)
            if (value.path == path)
            {
                value.bytes = bytes(xml);
                return;
            }
        parts.push_back({path, bytes(xml)});
    }

    inline std::string document_target(const std::string& target)
    {
        if (target.empty() || target.find_first_of("\\:%?#") != target.npos ||
            std::any_of(target.begin(), target.end(), [](unsigned char value)
        {
            return value < 32;
        }))
            return {};
        std::vector<std::string> segments;
        if (target.front() != '/')
            segments.push_back("word");
        std::istringstream input(target);
        for (std::string segment; std::getline(input, segment, '/');)
        {
            if (segment.empty() || segment == ".")
                continue;
            if (segment == "..")
            {
                if (segments.empty())
                    return {};
                segments.pop_back();
            }
            else
                segments.push_back(segment);
        }
        std::string result;
        for (const auto& segment : segments)
            result += (result.empty() ? "" : "/") + segment;
        return result;
    }

    inline std::string document_relationship(
        const std::vector<OfficePart>& parts, const char* kind, std::string& error)
    {
        error.clear();
        const auto source = part(parts, "word/_rels/document.xml.rels");
        if (!source)
            return {};
        pugi::xml_document xml;
        if (!read(source->bytes, xml) || !named(xml.document_element(), "Relationships", rel_ns))
        {
            error = "DOCX 正文关系 XML 无效。";
            return {};
        }
        const auto type =
            std::string("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") + kind;
        std::string result;
        for (auto node : xml.document_element().children())
            if (named(node, "Relationship", rel_ns) && node.attribute("Type").value() == type)
            {
                const std::string_view mode = node.attribute("TargetMode").value();
                const auto target = document_target(node.attribute("Target").value());
                if (!result.empty() || (!mode.empty() && mode != "Internal") || target.empty())
                {
                    error = std::string("DOCX ") + kind + " 关系无效或重复；不会读取外部资源。";
                    return {};
                }
                result = target;
            }
        return result;
    }
}
