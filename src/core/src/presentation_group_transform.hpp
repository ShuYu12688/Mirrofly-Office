#pragma once

#include <pugixml.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace mirrorfly::detail
{
    struct FlattenedGroupMember
    {
        long long x = 0;
        long long y = 0;
        long long width = 0;
        long long height = 0;
        long long rotation = 0;
        bool update_rotation = false;
    };

    inline pugi::xml_node group_transform_child(pugi::xml_node parent, const char* name)
    {
        for (auto node : parent.children())
        {
            const auto colon = std::strrchr(node.name(), ':');
            if (std::strcmp(colon ? colon + 1 : node.name(), name) == 0)
                return node;
        }
        return {};
    }

    // OOXML rotates each rectangle around its centre. A nonuniformly scaled rotated child would shear.
    inline std::optional<FlattenedGroupMember> flatten_presentation_group_member(
        pugi::xml_node group, pugi::xml_node member)
    {
        const auto off = group_transform_child(group, "off");
        const auto ext = group_transform_child(group, "ext");
        const auto child_off = group_transform_child(group, "chOff");
        const auto child_ext = group_transform_child(group, "chExt");
        const auto member_off = group_transform_child(member, "off");
        const auto member_ext = group_transform_child(member, "ext");
        if (!off.attribute("x") || !off.attribute("y") || !ext.attribute("cx") || !ext.attribute("cy") ||
            !child_off.attribute("x") || !child_off.attribute("y") || !child_ext.attribute("cx") ||
            !child_ext.attribute("cy") || !member_off.attribute("x") || !member_off.attribute("y") ||
            !member_ext.attribute("cx") || !member_ext.attribute("cy") ||
            group.attribute("flipH").as_bool() || group.attribute("flipV").as_bool())
            return std::nullopt;
        constexpr long long maximum_exact_coordinate = 9007199254740991LL;
        const pugi::xml_attribute coordinates[]{off.attribute("x"), off.attribute("y"), ext.attribute("cx"),
            ext.attribute("cy"), child_off.attribute("x"), child_off.attribute("y"),
            child_ext.attribute("cx"), child_ext.attribute("cy"), member_off.attribute("x"),
            member_off.attribute("y"), member_ext.attribute("cx"), member_ext.attribute("cy")};
        for (const auto coordinate : coordinates)
        {
            const char* text = coordinate.value();
            long long value = 0;
            const auto end = text + std::strlen(text);
            const auto parsed = std::from_chars(text, end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != end || value < -maximum_exact_coordinate ||
                value > maximum_exact_coordinate)
                return std::nullopt;
        }
        const auto group_width = ext.attribute("cx").as_llong();
        const auto group_height = ext.attribute("cy").as_llong();
        const auto source_width = child_ext.attribute("cx").as_llong();
        const auto source_height = child_ext.attribute("cy").as_llong();
        const auto member_width = member_ext.attribute("cx").as_llong();
        const auto member_height = member_ext.attribute("cy").as_llong();
        if (group_width <= 0 || group_height <= 0 || source_width <= 0 || source_height <= 0 ||
            member_width <= 0 || member_height <= 0)
            return std::nullopt;
        const long double scale_x = static_cast<long double>(group_width) / source_width;
        const long double scale_y = static_cast<long double>(group_height) / source_height;
        const auto member_rotation = member.attribute("rot").as_llong();
        if (std::abs(scale_x - scale_y) >= 1e-9L && member_rotation % 21600000 != 0)
            return std::nullopt;
        const auto rounded = [](long double value) -> std::optional<long long>
        {
            if (!std::isfinite(value) ||
                value <= static_cast<long double>(std::numeric_limits<long long>::min()) ||
                value >= static_cast<long double>(std::numeric_limits<long long>::max()))
                return std::nullopt;
            return std::llround(value);
        };
        const long double group_x = static_cast<long double>(off.attribute("x").as_llong());
        const long double group_y = static_cast<long double>(off.attribute("y").as_llong());
        const long double width = member_width * scale_x;
        const long double height = member_height * scale_y;
        long double x = group_x +
            (static_cast<long double>(member_off.attribute("x").as_llong()) -
                child_off.attribute("x").as_llong()) *
                scale_x;
        long double y = group_y +
            (static_cast<long double>(member_off.attribute("y").as_llong()) -
                child_off.attribute("y").as_llong()) *
                scale_y;
        const auto group_rotation = group.attribute("rot").as_llong() % 21600000;
        if (group_rotation != 0)
        {
            constexpr long double pi = 3.141592653589793238462643383279502884L;
            const long double angle = group_rotation / 60000.0L * pi / 180.0L;
            const long double centre_x = group_x + group_width / 2.0L;
            const long double centre_y = group_y + group_height / 2.0L;
            const long double relative_x = x + width / 2.0L - centre_x;
            const long double relative_y = y + height / 2.0L - centre_y;
            x = centre_x + std::cos(angle) * relative_x - std::sin(angle) * relative_y - width / 2.0L;
            y = centre_y + std::sin(angle) * relative_x + std::cos(angle) * relative_y - height / 2.0L;
        }
        const auto final_x = rounded(x);
        const auto final_y = rounded(y);
        const auto final_width = rounded(width);
        const auto final_height = rounded(height);
        if (!final_x || !final_y || !final_width || !final_height || *final_width <= 0 || *final_height <= 0)
            return std::nullopt;
        FlattenedGroupMember result{*final_x, *final_y, *final_width, *final_height};
        result.update_rotation = group_rotation != 0;
        if (result.update_rotation)
        {
            result.rotation = (group_rotation + member_rotation % 21600000) % 21600000;
            if (result.rotation < 0)
                result.rotation += 21600000;
        }
        return result;
    }
}
