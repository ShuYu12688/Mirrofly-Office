#include "presentation_text_style.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
    bool bounded(double value, double minimum, double maximum)
    {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    }

    bool color(const std::string& value)
    {
        return value.size() == 7 && value.front() == '#' &&
            std::all_of(value.begin() + 1, value.end(), [](char character)
        {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F');
        });
    }

    bool fill(const mirrorfly::PresentationFill& value)
    {
        if ((!value.color.empty() && !color(value.color)) || !bounded(value.opacity, 0, 1) ||
            !bounded(value.angle_degrees, -360, 360) || !value.image_path.empty() ||
            value.stops.size() > 16 || (!value.stops.empty() && value.stops.size() < 2))
            return false;
        if (!mirrorfly::presentation_pattern_supported(value.pattern) ||
            (!value.pattern.empty() &&
                (!value.stops.empty() || !color(value.color) || !color(value.pattern_foreground_color) ||
                    !bounded(value.pattern_foreground_opacity, 0, 1))))
            return false;
        double previous = -1;
        for (const auto& stop : value.stops)
        {
            if (!color(stop.color) || !bounded(stop.opacity, 0, 1) || !bounded(stop.position, 0, 1) ||
                stop.position < previous)
                return false;
            previous = stop.position;
        }
        return true;
    }
}

namespace mirrorfly
{
    bool apply_presentation_text_style(PresentationText& text, const PresentationTextStylePatch& patch)
    {
        static const std::set<std::string> warps{"", "textNoShape", "textPlain", "textArchUp", "textArchDown",
            "textWave1", "textWave2", "textDoubleWave1", "textInflate", "textDeflate", "textSlantUp",
            "textSlantDown", "textChevron", "textChevronInverted", "textCircle"};
        static const std::set<std::string> directions{"horz", "vert", "vert270"};
        if ((patch.fill && !fill(*patch.fill)) ||
            (patch.outline && (!fill(patch.outline->fill) || !bounded(patch.outline->width, 0, 72))) ||
            (patch.shadow &&
                (!color(patch.shadow->color) || !bounded(patch.shadow->opacity, 0, 1) ||
                    !bounded(patch.shadow->blur, 0, 72) || !bounded(patch.shadow->x, -200, 200) ||
                    !bounded(patch.shadow->y, -200, 200))) ||
            (patch.glow &&
                (!color(patch.glow->color) || !bounded(patch.glow->opacity, 0, 1) ||
                    !bounded(patch.glow->radius, 0, 72))) ||
            (patch.reflection &&
                (!bounded(patch.reflection->opacity, 0, 1) || !bounded(patch.reflection->offset, 0, 200) ||
                    !bounded(patch.reflection->end_opacity, 0, 1) ||
                    !bounded(patch.reflection->start_position, 0, 1) ||
                    !bounded(patch.reflection->end_position, 0, 1) ||
                    patch.reflection->start_position >= patch.reflection->end_position)) ||
            (patch.warp && !warps.count(*patch.warp)) ||
            (patch.warp_adjustment && !bounded(*patch.warp_adjustment, 0.05, 0.45)) ||
            (patch.rotation && !bounded(*patch.rotation, -360, 360)) ||
            (patch.vertical && !directions.count(*patch.vertical)))
            return false;
        if (text.paragraphs.empty())
            text.paragraphs.emplace_back();
        for (auto& paragraph : text.paragraphs)
        {
            if (paragraph.runs.empty())
                paragraph.runs.emplace_back();
            for (auto& run : paragraph.runs)
            {
                if (patch.fill)
                {
                    run.fill = *patch.fill;
                    run.color = patch.fill->color.empty() ? "#000000" : patch.fill->color;
                    run.opacity =
                        patch.fill->color.empty() && patch.fill->stops.empty() ? 0 : patch.fill->opacity;
                }
                auto& effect = run.effects;
                if (patch.outline)
                {
                    effect.outline_fill = patch.outline->fill;
                    effect.outline_color = patch.outline->fill.color;
                    effect.outline_opacity = patch.outline->fill.opacity;
                    effect.outline_width = patch.outline->width;
                }
                if (patch.shadow)
                {
                    effect.shadow_color = patch.shadow->color;
                    effect.shadow_opacity = patch.shadow->opacity;
                    effect.shadow_blur = patch.shadow->blur;
                    effect.shadow_x = patch.shadow->x;
                    effect.shadow_y = patch.shadow->y;
                }
                if (patch.glow)
                {
                    effect.glow_color = patch.glow->color;
                    effect.glow_opacity = patch.glow->opacity;
                    effect.glow_radius = patch.glow->radius;
                }
                if (patch.reflection)
                {
                    effect.reflection_opacity = patch.reflection->opacity;
                    effect.reflection_offset = patch.reflection->offset;
                    effect.reflection_end_opacity = patch.reflection->end_opacity;
                    effect.reflection_start_position = patch.reflection->start_position;
                    effect.reflection_end_position = patch.reflection->end_position;
                }
            }
        }
        if (patch.warp)
            text.warp = *patch.warp;
        if (patch.warp_adjustment)
            text.warp_adjustment = *patch.warp_adjustment;
        if (patch.rotation)
            text.rotation = *patch.rotation;
        if (patch.vertical)
            text.vertical = *patch.vertical;
        if (patch.clip_vertical)
            text.clip_vertical = *patch.clip_vertical;
        if (patch.clip_horizontal)
            text.clip_horizontal = *patch.clip_horizontal;
        return true;
    }
}
