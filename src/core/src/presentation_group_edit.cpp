#include "presentation_group_edit.hpp"
#include "presentation_group_transform.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using Node = pugi::xml_node;
    using Action = mirrorfly::PresentationEditAction;

    std::string local(const char* name)
    {
        const auto colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    Node child(Node parent, const char* name)
    {
        for (auto node : parent.children())
            if (local(node.name()) == name)
                return node;
        return {};
    }

    pugi::xml_attribute attribute(Node node, const char* name)
    {
        auto found = node.attribute(name);
        return found ? found : node.append_attribute(name);
    }

    Node properties(Node shape)
    {
        for (auto node : shape.children())
            if (const auto found = child(node, "cNvPr"))
                return found;
        return {};
    }

    Node shape_node(Node tree, const std::string& id)
    {
        if (id.empty())
            return {};
        Node result;
        for (auto item : tree.children())
        {
            Node found;
            if (properties(item).attribute("id").value() == id)
                found = item;
            if (local(item.name()) == "grpSp")
                if (const auto nested = shape_node(item, id))
                {
                    if (found)
                        throw std::runtime_error("对象标识重复，无法安全编辑。");
                    found = nested;
                }
            if (found)
            {
                if (result)
                    throw std::runtime_error("对象标识重复，无法安全编辑。");
                result = found;
            }
        }
        return result;
    }

    unsigned long long largest_id(Node node)
    {
        auto result = local(node.name()) == "cNvPr" ? node.attribute("id").as_ullong() : 1;
        for (auto item : node.children())
            result = std::max(result, largest_id(item));
        return result;
    }

    std::string next_shape_id(Node tree)
    {
        const auto value = largest_id(tree) + 1;
        if (value >= std::numeric_limits<unsigned int>::max())
            throw std::runtime_error("对象标识数量已达上限。");
        return std::to_string(value);
    }

    Node next_element(Node item)
    {
        do
            item = item.next_sibling();
        while (item && item.type() != pugi::node_element);
        return item;
    }

    std::optional<long long> coordinate(pugi::xml_attribute source)
    {
        if (!source)
            return std::nullopt;
        const char* text = source.value();
        const char* end = text + std::strlen(text);
        long long value = 0;
        const auto parsed = std::from_chars(text, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != end || value < -9007199254740991LL ||
            value > 9007199254740991LL)
            return std::nullopt;
        return value;
    }

    bool identity_group(Node group)
    {
        if (local(group.name()) != "grpSp")
            return false;
        const auto group_properties = child(child(group, "nvGrpSpPr"), "cNvPr");
        if (child(group_properties, "hlinkClick") || child(group_properties, "hlinkMouseOver"))
            return false;
        const auto style = child(group, "grpSpPr");
        const auto transform = child(style, "xfrm");
        const auto false_or_missing = [](pugi::xml_attribute value)
        {
            return !value || std::strcmp(value.value(), "0") == 0 || std::strcmp(value.value(), "false") == 0;
        };
        if (!transform || !false_or_missing(transform.attribute("flipH")) ||
            !false_or_missing(transform.attribute("flipV")))
            return false;
        for (auto property : style.children())
            if (property.type() == pugi::node_element && local(property.name()) != "xfrm")
                return false;
        bool has_member = false;
        for (auto member : group.children())
        {
            if (member.type() != pugi::node_element)
                continue;
            const auto kind = local(member.name());
            if (kind == "nvGrpSpPr" || kind == "grpSpPr")
                continue;
            if (kind != "sp" && kind != "pic" && kind != "cxnSp")
                return false;
            has_member = true;
        }
        if (!has_member)
            return false;
        if (const auto rotation = transform.attribute("rot"))
        {
            const auto value = coordinate(rotation);
            if (!value || *value % 21600000 != 0)
                return false;
        }
        const auto offset = child(transform, "off");
        const auto extent = child(transform, "ext");
        const auto child_offset = child(transform, "chOff");
        const auto child_extent = child(transform, "chExt");
        const auto x = coordinate(offset.attribute("x"));
        const auto y = coordinate(offset.attribute("y"));
        const auto width = coordinate(extent.attribute("cx"));
        const auto height = coordinate(extent.attribute("cy"));
        return x && y && width && height && *width > 0 && *height > 0 &&
            *x == coordinate(child_offset.attribute("x")) && *y == coordinate(child_offset.attribute("y")) &&
            *width == coordinate(child_extent.attribute("cx")) &&
            *height == coordinate(child_extent.attribute("cy")) && std::abs(*x) <= 127000000 &&
            std::abs(*y) <= 127000000 && *width <= 127000000 && *height <= 127000000;
    }

    std::array<double, 4> shape_bounds(const mirrorfly::PresentationShape& item)
    {
        std::array<double, 4> result{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
        for (const double x : {0.0, item.width})
            for (const double y : {0.0, item.height})
            {
                const double px = item.transform[0] * x + item.transform[2] * y + item.transform[4];
                const double py = item.transform[1] * x + item.transform[3] * y + item.transform[5];
                result[0] = std::min(result[0], px);
                result[1] = std::min(result[1], py);
                result[2] = std::max(result[2], px);
                result[3] = std::max(result[3], py);
            }
        return result;
    }
}

namespace mirrorfly::detail
{
    bool presentation_group_is_extendable(Node group)
    {
        return identity_group(group);
    }

    void patch_presentation_group(
        Node tree, const PresentationSlide& slide, const PresentationEditCommand& command)
    {
        if (command.action == Action::GroupAdjacent)
        {
            const auto& shape = slide.shapes[command.shape_index];
            const auto& other = slide.shapes[*command.target_index];
            auto first = shape_node(tree, shape.source_id);
            auto second = shape_node(tree, other.source_id);
            if (!first || !second || first.parent() != tree || second.parent() != tree ||
                (next_element(first) != second && next_element(second) != first))
                throw std::runtime_error("只能组合图层相邻的两个普通对象。");
            if (next_element(second) == first)
                std::swap(first, second);
            const auto a = shape_bounds(shape);
            const auto b = shape_bounds(other);
            const double left = std::min(a[0], b[0]);
            const double top = std::min(a[1], b[1]);
            const double right = std::max(a[2], b[2]);
            const double bottom = std::max(a[3], b[3]);
            if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) ||
                !std::isfinite(bottom) || right - left < 0.01 || bottom - top < 0.01 ||
                std::abs(left) > 10000 || std::abs(top) > 10000 || right - left > 10000 ||
                bottom - top > 10000)
                throw std::runtime_error("组合范围超出可编辑坐标。");
            const auto id = next_shape_id(tree);
            auto group = tree.insert_child_before("p:grpSp", first);
            auto metadata = group.append_child("p:nvGrpSpPr");
            auto properties = metadata.append_child("p:cNvPr");
            properties.append_attribute("id") = id.c_str();
            properties.append_attribute("name") = "组合";
            metadata.append_child("p:cNvGrpSpPr");
            metadata.append_child("p:nvPr");
            auto transform = group.append_child("p:grpSpPr").append_child("a:xfrm");
            const auto x = static_cast<long long>(std::llround(left * 12700));
            const auto y = static_cast<long long>(std::llround(top * 12700));
            const auto width = static_cast<long long>(std::llround((right - left) * 12700));
            const auto height = static_cast<long long>(std::llround((bottom - top) * 12700));
            transform.append_child("a:off").append_attribute("x") = x;
            attribute(child(transform, "off"), "y") = y;
            transform.append_child("a:ext").append_attribute("cx") = width;
            attribute(child(transform, "ext"), "cy") = height;
            transform.append_child("a:chOff").append_attribute("x") = x;
            attribute(child(transform, "chOff"), "y") = y;
            transform.append_child("a:chExt").append_attribute("cx") = width;
            attribute(child(transform, "chExt"), "cy") = height;
            group.append_copy(first);
            group.append_copy(second);
            tree.remove_child(first);
            tree.remove_child(second);
        }
        else if (command.action == Action::AddToGroup)
        {
            auto group = shape_node(tree, command.group_id);
            const auto& target_shape = slide.shapes[*command.target_index];
            auto target = shape_node(tree, target_shape.source_id);
            if (!group || !target || group.parent() != tree || target.parent() != tree ||
                !identity_group(group) || (next_element(group) != target && next_element(target) != group))
                throw std::runtime_error("只能向简单组合加入图层相邻的普通对象。");
            const auto bounds = shape_bounds(target_shape);
            if (!std::all_of(bounds.begin(), bounds.end(),
                    [](double value)
            {
                return std::isfinite(value);
            }) ||
                std::any_of(bounds.begin(), bounds.end(), [](double value)
            {
                return std::abs(value) > 10000;
            }))
                throw std::runtime_error("相邻对象超出可组合坐标范围。");
            const auto transform = child(child(group, "grpSpPr"), "xfrm");
            const auto offset = child(transform, "off");
            const auto extent = child(transform, "ext");
            const auto child_offset = child(transform, "chOff");
            const auto child_extent = child(transform, "chExt");
            const long long old_x = offset.attribute("x").as_llong();
            const long long old_y = offset.attribute("y").as_llong();
            const long long old_right = old_x + extent.attribute("cx").as_llong();
            const long long old_bottom = old_y + extent.attribute("cy").as_llong();
            const long long x = std::min(old_x, static_cast<long long>(std::floor(bounds[0] * 12700)));
            const long long y = std::min(old_y, static_cast<long long>(std::floor(bounds[1] * 12700)));
            const long long right = std::max(old_right, static_cast<long long>(std::ceil(bounds[2] * 12700)));
            const long long bottom =
                std::max(old_bottom, static_cast<long long>(std::ceil(bounds[3] * 12700)));
            if (std::abs(x) > 127000000 || std::abs(y) > 127000000 || right - x > 127000000 ||
                bottom - y > 127000000)
                throw std::runtime_error("组合范围超出可编辑坐标。");
            attribute(offset, "x") = x;
            attribute(offset, "y") = y;
            attribute(child_offset, "x") = x;
            attribute(child_offset, "y") = y;
            attribute(extent, "cx") = right - x;
            attribute(extent, "cy") = bottom - y;
            attribute(child_extent, "cx") = right - x;
            attribute(child_extent, "cy") = bottom - y;
            if (next_element(target) == group)
            {
                Node first_member;
                for (auto item : group.children())
                    if (item.type() == pugi::node_element && local(item.name()) != "nvGrpSpPr" &&
                        local(item.name()) != "grpSpPr")
                    {
                        first_member = item;
                        break;
                    }
                if (!first_member)
                    throw std::runtime_error("空组合不能追加对象。");
                group.insert_copy_before(target, first_member);
            }
            else
                group.append_copy(target);
            tree.remove_child(target);
        }
        else
        {
            auto group = shape_node(tree, command.group_id);
            if (!group || local(group.name()) != "grpSp" || group.parent() != tree)
                throw std::runtime_error("只能取消当前页的顶层组合。");
            const auto group_properties = child(child(group, "nvGrpSpPr"), "cNvPr");
            if (child(group_properties, "hlinkClick") || child(group_properties, "hlinkMouseOver"))
                throw std::runtime_error("组合含有点击操作，取消组合会丢失该操作。");
            auto transform = child(child(group, "grpSpPr"), "xfrm");
            auto offset = child(transform, "off");
            auto extent = child(transform, "ext");
            auto child_offset = child(transform, "chOff");
            auto child_extent = child(transform, "chExt");
            if (!offset || !extent || !child_offset || !child_extent ||
                transform.attribute("flipH").as_bool() || transform.attribute("flipV").as_bool())
                throw std::runtime_error("此组合包含暂不能安全展开的翻转或坐标。");
            for (auto property : child(group, "grpSpPr").children())
                if (property.type() == pugi::node_element && local(property.name()) != "xfrm")
                    throw std::runtime_error("组合效果无法安全展开。");
            if (!offset.attribute("x") || !offset.attribute("y") || !extent.attribute("cx") ||
                !extent.attribute("cy") || !child_offset.attribute("x") || !child_offset.attribute("y") ||
                !child_extent.attribute("cx") || !child_extent.attribute("cy"))
                throw std::runtime_error("组合坐标缺失，无法取消组合。");
            const auto width = extent.attribute("cx").as_llong();
            const auto height = extent.attribute("cy").as_llong();
            const auto child_width = child_extent.attribute("cx").as_llong();
            const auto child_height = child_extent.attribute("cy").as_llong();
            if (width <= 0 || height <= 0 || child_width <= 0 || child_height <= 0)
                throw std::runtime_error("组合尺寸无效，无法取消组合。");
            const bool scaled = width != child_width || height != child_height;
            const bool rotated = transform.attribute("rot").as_llong() % 21600000 != 0;
            struct Member
            {
                Node node;
                mirrorfly::detail::FlattenedGroupMember flattened;
            };
            std::vector<Member> children;
            for (auto node : group.children())
            {
                if (node.type() != pugi::node_element)
                    continue;
                const auto kind = local(node.name());
                if (kind == "nvGrpSpPr" || kind == "grpSpPr")
                    continue;
                if ((scaled || rotated) && kind != "pic")
                    throw std::runtime_error("缩放或旋转组合中的文字或图形不能安全展开。");
                if (kind != "sp" && kind != "pic" && kind != "cxnSp" && kind != "graphicFrame")
                    throw std::runtime_error("此组合包含暂不能展开的对象：" + kind);
                if ((scaled || rotated) && !child(child(node, "blipFill"), "stretch"))
                    throw std::runtime_error("平铺图片不能安全展开组合变换。");
                if (scaled || rotated)
                    for (auto property : child(node, "spPr").children())
                        if (property.type() == pugi::node_element &&
                            ((local(property.name()) == "ln" && !child(property, "noFill")) ||
                                local(property.name()) == "effectLst" ||
                                local(property.name()) == "effectDag" ||
                                local(property.name()) == "scene3d" || local(property.name()) == "sp3d"))
                            throw std::runtime_error("图片组合包含不能安全展开的边框或效果。");
                auto location =
                    kind == "graphicFrame" ? child(node, "xfrm") : child(child(node, "spPr"), "xfrm");
                const auto flattened =
                    mirrorfly::detail::flatten_presentation_group_member(transform, location);
                if (!flattened)
                    throw std::runtime_error("组合内对象包含无法展开的旋转或坐标。");
                children.push_back({node, *flattened});
            }
            if (children.empty())
                throw std::runtime_error("空组合不能取消。");
            for (const auto& member : children)
            {
                auto copy = tree.insert_copy_before(member.node, group);
                Node location;
                if (local(copy.name()) == "graphicFrame")
                    location = child(copy, "xfrm");
                else
                    location = child(child(copy, "spPr"), "xfrm");
                const auto member_offset = child(location, "off");
                const auto member_extent = child(location, "ext");
                attribute(member_offset, "x") = member.flattened.x;
                attribute(member_offset, "y") = member.flattened.y;
                attribute(member_extent, "cx") = member.flattened.width;
                attribute(member_extent, "cy") = member.flattened.height;
                if (member.flattened.update_rotation)
                    attribute(location, "rot") = member.flattened.rotation;
            }
            tree.remove_child(group);
        }
    }
}
