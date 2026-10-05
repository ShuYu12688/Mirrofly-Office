#include "mindmap_graph.hpp"
#include <algorithm>
#include <cstdint>
#include <mirrorfly/mindmap.hpp>
#include <pugixml.hpp>
#include <string_view>
#include <utf8.h>

namespace
{
    mirrorfly::MindMapParseResult parse_failure(mirrorfly::MindMapError error, const char* message)
    {
        return {error, message, false, {}};
    }
    bool xml_character(std::uint32_t value)
    {
        return value == 0x9 || value == 0xA || value == 0xD || (value >= 0x20 && value <= 0xD7FF) ||
            (value >= 0xE000 && value <= 0xFFFD) || (value >= 0x10000 && value <= 0x10FFFF);
    }

    bool valid_xml_text(const std::string& text)
    {
        if (!utf8::is_valid(text.begin(), text.end()))
        {
            return false;
        }
        try
        {
            auto position = text.begin();
            while (position != text.end())
            {
                if (!xml_character(utf8::next(position, text.end())))
                {
                    return false;
                }
            }
        }
        catch (const utf8::exception&)
        {
            return false;
        }
        return true;
    }

    bool whitespace(const pugi::xml_node& node)
    {
        if (node.type() != pugi::node_pcdata)
        {
            return false;
        }
        const std::string_view value = node.value();
        return std::all_of(value.begin(), value.end(), [](unsigned char character)
        {
            return std::isspace(character) != 0;
        });
    }

}

namespace mirrorfly
{
    bool is_mindmap_path(const std::string& path)
    {
        if (path.size() < 4)
            return false;
        auto suffix = path.substr(path.size() - 4);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c)
        {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        });
        return suffix == ".mfg";
    }
    MindMapDocument make_mindmap(const std::string& root_text)
    {
        MindMapDocument document;
        document.free_layout = true;
        document.root_id = "root";
        document.nodes.push_back({document.root_id, {}, root_text, false, {}});
        return document;
    }
    MindMapStatus validate_mindmap(const MindMapDocument& document)
    {
        return validate_mindmap_graph(document);
    }
    MindMapParseResult parse_mindmap(const std::string& xml)
    {
        try
        {
            if (xml.size() > maximum_mindmap_xml_bytes)
            {
                return parse_failure(MindMapError::TooLarge, "思维导图 XML 最多为 2 MiB。");
            }
            if (!valid_xml_text(xml) || xml.find("<!DOCTYPE") != xml.npos || xml.find("<!ENTITY") != xml.npos)
            {
                return parse_failure(
                    MindMapError::InvalidXml, "思维导图 XML 包含非法 Unicode、DTD 或实体声明。");
            }
            pugi::xml_document parsed;
            const auto loaded =
                parsed.load_buffer(xml.data(), xml.size(), pugi::parse_full, pugi::encoding_utf8);
            if (!loaded)
            {
                return parse_failure(MindMapError::InvalidXml, "思维导图 XML 格式无效。");
            }
            pugi::xml_node map;
            bool declaration_seen = false;
            for (const auto& node : parsed.children())
            {
                if (node.type() == pugi::node_declaration)
                {
                    if (declaration_seen || map)
                    {
                        return parse_failure(MindMapError::Unsupported, "XML 声明位置或数量无法无损处理。");
                    }
                    declaration_seen = true;
                    for (const auto& attribute : node.attributes())
                    {
                        const std::string_view name = attribute.name();
                        if (name != "version" && name != "encoding")
                        {
                            return parse_failure(
                                MindMapError::Unsupported, "XML 声明含本版无法无损保留的属性。");
                        }
                    }
                    const std::string_view version = node.attribute("version").value();
                    const std::string_view encoding = node.attribute("encoding").value();
                    if ((!version.empty() && version != "1.0") ||
                        (!encoding.empty() && encoding != "UTF-8" && encoding != "utf-8"))
                    {
                        return parse_failure(MindMapError::Unsupported, "本版仅支持 XML 1.0 的 UTF-8 声明。");
                    }
                    continue;
                }
                if (whitespace(node))
                {
                    continue;
                }
                if (node.type() != pugi::node_element || map)
                {
                    return parse_failure(
                        MindMapError::Unsupported, "文件含注释、处理指令或多个顶层结构，本版无法无损处理。");
                }
                map = node;
            }
            if (map && std::string_view(map.name()) == "mirrorfly-map")
                return parse_mindmap_graph(xml);
            return parse_failure(MindMapError::Unsupported, "本版仅支持 .mfg 自由导图，旧 .mm 格式已移除。");
        }
        catch (const std::bad_alloc&)
        {
            return parse_failure(MindMapError::TooLarge, "思维导图内容超过可用内存。");
        }
    }
    MindMapTextResult serialize_mindmap(const MindMapDocument& document)
    {
        try
        {
            return serialize_mindmap_graph(document);
        }
        catch (const std::bad_alloc&)
        {
            return {MindMapError::TooLarge, "思维导图内容超过可用内存。", false, {}};
        }
    }
    MindMapTextResult export_mindmap_outline(const MindMapDocument& document)
    {
        try
        {
            return outline_mindmap_graph(document);
        }
        catch (const std::bad_alloc&)
        {
            return {MindMapError::TooLarge, "思维导图内容超过可用内存。", false, {}};
        }
    }
    MindMapStatus apply_mindmap_command(MindMapDocument& document, const MindMapCommand& command)
    {
        try
        {
            return edit_mindmap_graph(document, command);
        }
        catch (const std::bad_alloc&)
        {
            return {MindMapError::TooLarge, "思维导图内容超过可用内存。", false};
        }
    }
}
