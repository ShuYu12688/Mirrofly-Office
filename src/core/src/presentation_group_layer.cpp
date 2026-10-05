#include "presentation_group_layer.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    std::string local(const char* name)
    {
        const auto colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    pugi::xml_node child(pugi::xml_node parent, const char* name)
    {
        for (auto node : parent.children())
            if (local(node.name()) == name)
                return node;
        return {};
    }

    bool layer_object(const std::string& name)
    {
        return name == "sp" || name == "pic" || name == "cxnSp" || name == "graphicFrame" || name == "grpSp";
    }
}

namespace mirrorfly
{
    PresentationGroupLayerOptions presentation_group_layer_options(const PresentationGroupFrame& group)
    {
        PresentationGroupLayerOptions result;
        if (!group.editable || group.layer_index < 0 || group.layer_count < 2 ||
            group.layer_index >= group.layer_count)
            return result;
        result.back = result.backward = group.layer_index > 0;
        result.forward = result.front = group.layer_index + 1 < group.layer_count;
        return result;
    }
}

namespace mirrorfly::detail
{
    void patch_presentation_group_layer(
        pugi::xml_node shape_tree, const std::string& group_id, const std::string& position)
    {
        if (!shape_tree || local(shape_tree.name()) != "spTree" || group_id.empty())
            throw std::runtime_error("当前页面缺少可调整图层的组合。");
        std::vector<pugi::xml_node> layers;
        pugi::xml_node group;
        std::size_t index = 0;
        for (auto node : shape_tree.children())
        {
            if (node.type() != pugi::node_element)
                continue;
            const auto name = local(node.name());
            if (!layer_object(name))
            {
                if (name != "nvGrpSpPr" && name != "grpSpPr")
                    throw std::runtime_error("页面含有未知图层结构，组合层次保持不变。");
                continue;
            }
            if (name == "grpSp")
            {
                const auto properties = child(child(node, "nvGrpSpPr"), "cNvPr");
                if (group_id == properties.attribute("id").value())
                {
                    if (group)
                        throw std::runtime_error("组合标识重复，无法安全调整层次。");
                    group = node;
                    index = layers.size();
                }
            }
            layers.push_back(node);
        }
        if (!group || layers.size() < 2)
            throw std::runtime_error("当前组合没有可调整的图层位置。");
        std::size_t target = index;
        if (position == "back" || position == "backward")
        {
            if (index == 0)
                throw std::runtime_error("当前组合已处于最底层。");
            target = position == "back" ? 0 : index - 1;
        }
        else if (position == "front" || position == "forward")
        {
            if (index + 1 == layers.size())
                throw std::runtime_error("当前组合已处于最顶层。");
            target = position == "front" ? layers.size() - 1 : index + 1;
        }
        else
            throw std::runtime_error("组合图层目标无效。");
        pugi::xml_node moved;
        if (target < index)
            moved = shape_tree.insert_move_before(group, layers[target]);
        else
            moved = shape_tree.insert_move_after(group, layers[target]);
        if (!moved)
            throw std::runtime_error("无法调整组合图层。");
    }
}
