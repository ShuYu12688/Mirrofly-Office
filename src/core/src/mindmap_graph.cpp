#include "mindmap_graph.hpp"
#include "mindmap_layout.hpp"
#include "mindmap_placement.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <utf8.h>

namespace
{
    using namespace mirrorfly;
    MindMapStatus failure(const std::string& message)
    {
        return {MindMapError::InvalidDocument, message, false};
    }
    bool text_valid(const std::string& text, std::size_t limit)
    {
        if (text.size() > limit || !utf8::is_valid(text.begin(), text.end()))
            return false;
        for (auto i = text.begin(); i != text.end();)
        {
            const auto code = utf8::next(i, text.end());
            if ((code < 32 && code != 9 && code != 10 && code != 13) || code == 0xfffe || code == 0xffff)
                return false;
        }
        return true;
    }
    bool color_valid(const std::string& color)
    {
        return color.empty() ||
            (color.size() == 7 && color.front() == '#' &&
                std::all_of(color.begin() + 1, color.end(), [](unsigned char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }));
    }
    double number(pugi::xml_node node, const char* name)
    {
        const std::string value = node.attribute(name).value();
        double result = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
            ? result
            : std::numeric_limits<double>::quiet_NaN();
    }
    bool attributes(pugi::xml_node node, const std::set<std::string>& allowed)
    {
        for (const auto& attribute : node.attributes())
            if (!allowed.count(attribute.name()))
                return false;
        return true;
    }
}

namespace mirrorfly
{
    MindMapDocument make_free_mindmap(const std::string& text)
    {
        auto result = make_mindmap(text);
        result.free_layout = true;
        return result;
    }

    MindMapStatus validate_mindmap_graph(const MindMapDocument& document)
    {
        if (document.read_only)
            return {MindMapError::ReadOnly, document.read_only_reason, false};
        if (!document.free_layout || document.nodes.empty() ||
            document.nodes.size() > maximum_mindmap_nodes || document.edges.size() > 4000)
            return failure("自由画布需有 1–1000 个节点，最多 4000 条连线。");
        std::set<std::string> nodes, edges;
        std::set<std::pair<std::string, std::string>> pairs;
        std::size_t bytes = document.root_id.size();
        for (const auto& node : document.nodes)
        {
            if (node.text.size() > maximum_mindmap_node_text_bytes)
                return {MindMapError::TooLarge, "节点文字超过允许长度。", false};
            if (node.id.empty() || !text_valid(node.id, maximum_mindmap_id_bytes) ||
                !nodes.insert(node.id).second || !text_valid(node.text, maximum_mindmap_node_text_bytes) ||
                !node.parent_id.empty() || !node.children.empty() || node.collapsed)
                return failure("节点 ID、文字或自由画布结构无效。");
            if (!std::isfinite(node.x) || !std::isfinite(node.y) || !std::isfinite(node.width) ||
                !std::isfinite(node.height) || node.x < 0 || node.y < 0 || node.x > 48000 || node.y > 48000 ||
                node.width < 64 || node.width > 1000 || node.height < 48 || node.height > 800 ||
                !std::isfinite(node.border_width) || node.border_width < 1 || node.border_width > 6 ||
                !color_valid(node.border) || !color_valid(node.fill) ||
                (node.shape != "rounded" && node.shape != "rectangle" && node.shape != "diamond" &&
                    node.shape != "ellipse"))
                return failure("节点位置、尺寸或样式超出允许范围。");
            bytes +=
                node.id.size() + node.text.size() + node.border.size() + node.fill.size() + node.shape.size();
        }
        if (!nodes.count(document.root_id))
            return failure("画布起始节点不存在。");
        for (const auto& edge : document.edges)
        {
            if (edge.id.empty() || !text_valid(edge.id, maximum_mindmap_id_bytes) ||
                !edges.insert(edge.id).second || !nodes.count(edge.from) || !nodes.count(edge.to) ||
                !text_valid(edge.label, 256) || !pairs.emplace(edge.from, edge.to).second)
                return failure("连线 ID、端点、标签或重复连线无效。");
            bytes += edge.id.size() + edge.from.size() + edge.to.size() + edge.label.size();
        }
        if (bytes > maximum_mindmap_document_bytes)
            return {MindMapError::TooLarge, "画布字段合计最多 512 KiB。", false};
        return {};
    }

    MindMapStatus edit_mindmap_graph(MindMapDocument& document, const MindMapCommand& command)
    {
        const auto valid = validate_mindmap_graph(document);
        if (valid.error != MindMapError::None)
            return valid;
        if (command.type == MindMapCommandType::AutoLayout)
            return layout_mindmap_graph(document);
        auto candidate = document;
        auto target = std::find_if(candidate.nodes.begin(), candidate.nodes.end(), [&](const auto& node)
        {
            return node.id == command.target_id;
        });
        if (command.type == MindMapCommandType::CreateNode || command.type == MindMapCommandType::AddChild ||
            command.type == MindMapCommandType::AddSibling)
        {
            MindMapNode node;
            node.id = command.new_id;
            node.text = command.text;
            node.x = command.x;
            node.y = command.y;
            if (command.type == MindMapCommandType::AddChild)
            {
                if (target == candidate.nodes.end())
                    return failure("连接起点不存在。");
                candidate.edges.push_back({"link-" + node.id, target->id, node.id, {}});
            }
            if (!place_mindmap_node(candidate, node))
                return failure("无法为节点找到有效且不重叠的位置。");
            candidate.nodes.push_back(std::move(node));
        }
        else if (command.type == MindMapCommandType::Disconnect)
        {
            const auto edge =
                std::find_if(candidate.edges.begin(), candidate.edges.end(), [&](const auto& item)
            {
                return item.id == command.target_id;
            });
            if (edge == candidate.edges.end())
                return failure("连线不存在。");
            candidate.edges.erase(edge);
        }
        else
        {
            if (target == candidate.nodes.end())
                return failure("节点不存在。");
            switch (command.type)
            {
            case MindMapCommandType::Rename:
                if (target->text == command.text)
                    return {};
                target->text = command.text;
                break;
            case MindMapCommandType::MoveNode:
                target->x = command.x;
                target->y = command.y;
                break;
            case MindMapCommandType::StyleNode:
                target->width = command.width;
                target->height = command.height;
                target->border = command.border;
                target->fill = command.fill;
                target->shape = command.shape;
                target->border_width = command.border_width;
                break;
            case MindMapCommandType::Connect:
                candidate.edges.push_back({command.new_id, target->id, command.parent_id, command.text});
                break;
            case MindMapCommandType::DeleteSubtree:
                if (candidate.nodes.size() == 1)
                    return failure("画布至少保留一个节点。");
                candidate.nodes.erase(target);
                {
                    const auto removed =
                        std::remove_if(candidate.edges.begin(), candidate.edges.end(), [&](const auto& edge)
                    {
                        return edge.from == command.target_id || edge.to == command.target_id;
                    });
                    candidate.edges.erase(removed, candidate.edges.end());
                }
                if (candidate.root_id == command.target_id)
                    candidate.root_id = candidate.nodes.front().id;
                break;
            default:
                return {MindMapError::InvalidCommand, "此操作不适用于自由画布。", false};
            }
        }
        if ((command.type == MindMapCommandType::MoveNode || command.type == MindMapCommandType::StyleNode) &&
            !place_mindmap_node(candidate, *target))
            return failure("无法为节点找到有效且不重叠的位置。");
        const auto checked = validate_mindmap_graph(candidate);
        if (checked.error != MindMapError::None)
            return checked;
        document = std::move(candidate);
        return {MindMapError::None, {}, true};
    }

    MindMapParseResult parse_mindmap_graph(const std::string& xml)
    {
        MindMapParseResult result;
        pugi::xml_document parsed;
        if (!parsed.load_buffer(xml.data(), xml.size(), pugi::parse_full, pugi::encoding_utf8))
            return {MindMapError::InvalidXml, "自由画布 XML 无效。", false, {}};
        const auto root = parsed.document_element();
        if (!attributes(root, {"version", "root"}) || std::string(root.attribute("version").value()) != "1")
            return {MindMapError::Unsupported, "不支持此自由画布版本或属性。", false, {}};
        result.document.free_layout = true;
        result.document.root_id = root.attribute("root").value();
        for (const auto& child : root.children())
        {
            if (child.type() == pugi::node_pcdata &&
                std::string(child.value()).find_first_not_of(" \r\n\t") == std::string::npos)
                continue;
            if (child.type() != pugi::node_element || child.first_child())
                return {MindMapError::Unsupported, "自由画布含不支持的内容。", false, {}};
            const std::string kind = child.name();
            if (kind == "node" &&
                attributes(
                    child, {"id", "text", "x", "y", "width", "height", "border", "fill", "shape", "stroke"}))
            {
                MindMapNode node;
                node.id = child.attribute("id").value();
                node.text = child.attribute("text").value();
                node.x = number(child, "x");
                node.y = number(child, "y");
                node.width = number(child, "width");
                node.height = number(child, "height");
                node.border = child.attribute("border").value();
                node.fill = child.attribute("fill").value();
                node.shape = child.attribute("shape").value();
                node.border_width = number(child, "stroke");
                result.document.nodes.push_back(std::move(node));
            }
            else if (kind == "edge" && attributes(child, {"id", "from", "to", "label"}))
                result.document.edges.push_back(
                    {child.attribute("id").value(), child.attribute("from").value(),
                        child.attribute("to").value(), child.attribute("label").value()});
            else
                return {MindMapError::Unsupported, "自由画布含未知节点或属性。", false, {}};
            if (result.document.nodes.size() > maximum_mindmap_nodes || result.document.edges.size() > 4000)
                return {MindMapError::TooLarge, "自由画布超过节点或连线预算。", false, {}};
        }
        const auto valid = validate_mindmap_graph(result.document);
        result.error = valid.error;
        result.message = valid.message;
        return result;
    }

    MindMapTextResult serialize_mindmap_graph(const MindMapDocument& document)
    {
        const auto valid = validate_mindmap_graph(document);
        if (valid.error != MindMapError::None)
            return {valid.error, valid.message, false, {}};
        pugi::xml_document xml;
        auto root = xml.append_child("mirrorfly-map");
        root.append_attribute("version") = "1";
        root.append_attribute("root") = document.root_id.c_str();
        for (const auto& node : document.nodes)
        {
            auto item = root.append_child("node");
            item.append_attribute("id") = node.id.c_str();
            item.append_attribute("text") = node.text.c_str();
            item.append_attribute("x") = node.x;
            item.append_attribute("y") = node.y;
            item.append_attribute("width") = node.width;
            item.append_attribute("height") = node.height;
            item.append_attribute("border") = node.border.c_str();
            item.append_attribute("fill") = node.fill.c_str();
            item.append_attribute("shape") = node.shape.c_str();
            item.append_attribute("stroke") = node.border_width;
        }
        for (const auto& edge : document.edges)
        {
            auto item = root.append_child("edge");
            item.append_attribute("id") = edge.id.c_str();
            item.append_attribute("from") = edge.from.c_str();
            item.append_attribute("to") = edge.to.c_str();
            item.append_attribute("label") = edge.label.c_str();
        }
        std::ostringstream stream;
        xml.save(stream, "    ", pugi::format_default, pugi::encoding_utf8);
        auto text = stream.str();
        if (text.size() > maximum_mindmap_xml_bytes)
            return {MindMapError::TooLarge, "自由画布序列化超过 2 MiB。", false, {}};
        return {MindMapError::None, {}, false, std::move(text)};
    }

    MindMapTextResult outline_mindmap_graph(const MindMapDocument& document)
    {
        const auto valid = validate_mindmap_graph(document);
        if (valid.error != MindMapError::None)
            return {valid.error, valid.message, false, {}};
        std::map<std::string, std::string> titles;
        std::string text;
        for (const auto& node : document.nodes)
        {
            titles[node.id] = node.text;
            text += "- " + node.text + "\n";
        }
        text += "\n连线：\n";
        for (const auto& edge : document.edges)
        {
            if (text.size() + titles.at(edge.from).size() + titles.at(edge.to).size() + edge.label.size() +
                    12 >
                maximum_mindmap_xml_bytes)
                return {MindMapError::TooLarge, "文字大纲超过 2 MiB。", false, {}};
            text += titles.at(edge.from) + " → " + titles.at(edge.to) +
                (edge.label.empty() ? "" : "：" + edge.label) + "\n";
        }
        if (text.size() > maximum_mindmap_xml_bytes)
            return {MindMapError::TooLarge, "文字大纲超过 2 MiB。", false, {}};
        return {MindMapError::None, {}, false, std::move(text)};
    }
}
