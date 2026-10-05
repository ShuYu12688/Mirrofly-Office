#include "presentation_text_style_adapter.hpp"

#include <cmath>

namespace
{
    QVariantMap fill_state(const mirrorfly::PresentationFill& fill)
    {
        QVariantList stops;
        for (const auto& stop : fill.stops)
            stops.append(QVariantMap{{"position", stop.position},
                {"color", QString::fromStdString(stop.color)}, {"opacity", stop.opacity}});
        return {{"color", QString::fromStdString(fill.color)}, {"opacity", fill.opacity},
            {"pattern", QString::fromStdString(fill.pattern)},
            {"patternForegroundColor", QString::fromStdString(fill.pattern_foreground_color)},
            {"patternForegroundOpacity", fill.pattern_foreground_opacity}, {"angle", fill.angle_degrees},
            {"stops", stops}};
    }

    bool number(const QVariantMap& map, const QString& key, double& value)
    {
        if (!map.contains(key))
            return true;
        bool converted = false;
        value = map.value(key).toDouble(&converted);
        return converted && std::isfinite(value);
    }

    bool allowed(const QVariantMap& map, const QStringList& keys)
    {
        for (auto key = map.cbegin(); key != map.cend(); ++key)
            if (!keys.contains(key.key()))
                return false;
        return true;
    }

    bool read_fill(const QVariantMap& map, mirrorfly::PresentationFill& fill)
    {
        if (!allowed(map,
                {"color", "opacity", "angle", "stops", "pattern", "patternForegroundColor",
                    "patternForegroundOpacity"}) ||
            !number(map, "opacity", fill.opacity) ||
            !number(map, "patternForegroundOpacity", fill.pattern_foreground_opacity) ||
            !number(map, "angle", fill.angle_degrees))
            return false;
        fill.color = map.value("color").toString().toStdString();
        fill.pattern = map.value("pattern").toString().toStdString();
        fill.pattern_foreground_color = map.value("patternForegroundColor").toString().toStdString();
        if (map.contains("stops") && map.value("stops").metaType().id() != QMetaType::QVariantList)
            return false;
        const auto stops = map.value("stops").toList();
        if (stops.size() > 16)
            return false;
        for (const auto& value : stops)
        {
            if (value.metaType().id() != QMetaType::QVariantMap)
                return false;
            const auto stop = value.toMap();
            mirrorfly::PresentationGradientStop output;
            if (!allowed(stop, {"color", "position", "opacity"}) || !stop.contains("color") ||
                !stop.contains("position") || !number(stop, "position", output.position) ||
                !number(stop, "opacity", output.opacity))
                return false;
            output.color = stop.value("color").toString().toStdString();
            fill.stops.push_back(std::move(output));
        }
        return true;
    }
}

namespace mirrorfly
{
    QVariantMap presentation_text_style_state(const PresentationShape& shape)
    {
        const PresentationRun empty;
        const auto& run = shape.text.paragraphs.empty() || shape.text.paragraphs.front().runs.empty()
            ? empty
            : shape.text.paragraphs.front().runs.front();
        const auto& effect = run.effects;
        const auto fill = run.fill.stops.empty() && run.fill.pattern.empty()
            ? PresentationFill{run.color, run.opacity}
            : run.fill;
        const auto outline = effect.outline_fill.stops.empty() && effect.outline_fill.pattern.empty()
            ? PresentationFill{effect.outline_color, effect.outline_opacity}
            : effect.outline_fill;
        return {{"fill", fill_state(fill)},
            {"outline", QVariantMap{{"fill", fill_state(outline)}, {"width", effect.outline_width}}},
            {"shadow",
                QVariantMap{{"color",
                                QString::fromStdString(
                                    effect.shadow_color.empty() ? "#000000" : effect.shadow_color)},
                    {"opacity", effect.shadow_opacity}, {"blur", effect.shadow_blur}, {"x", effect.shadow_x},
                    {"y", effect.shadow_y}}},
            {"glow",
                QVariantMap{
                    {"color",
                        QString::fromStdString(effect.glow_color.empty() ? "#4B8CFF" : effect.glow_color)},
                    {"opacity", effect.glow_opacity}, {"radius", effect.glow_radius}}},
            {"reflection",
                QVariantMap{{"opacity", effect.reflection_opacity}, {"offset", effect.reflection_offset},
                    {"endOpacity", effect.reflection_end_opacity},
                    {"startPosition", effect.reflection_start_position},
                    {"endPosition", effect.reflection_end_position}}},
            {"warp", QString::fromStdString(shape.text.warp)}, {"warpAdjustment", shape.text.warp_adjustment},
            {"rotation", shape.text.rotation}, {"vertical", QString::fromStdString(shape.text.vertical)},
            {"clipVertical", shape.text.clip_vertical}, {"clipHorizontal", shape.text.clip_horizontal}};
    }

    std::optional<PresentationTextStylePatch> presentation_text_style_command(const QVariantMap& options)
    {
        if (options.isEmpty() ||
            !allowed(options,
                {"fill", "outline", "shadow", "glow", "reflection", "warp", "warpAdjustment", "rotation",
                    "vertical", "clipVertical", "clipHorizontal"}))
            return std::nullopt;
        PresentationTextStylePatch result;
        for (const auto& key : {"fill", "outline", "shadow", "glow", "reflection"})
            if (options.contains(key) && options.value(key).metaType().id() != QMetaType::QVariantMap)
                return std::nullopt;
        if (options.contains("fill"))
        {
            result.fill.emplace();
            if (!read_fill(options.value("fill").toMap(), *result.fill))
                return std::nullopt;
        }
        if (options.contains("outline"))
        {
            const auto map = options.value("outline").toMap();
            result.outline.emplace();
            if (!allowed(map, {"fill", "width"}) ||
                map.value("fill").metaType().id() != QMetaType::QVariantMap ||
                !read_fill(map.value("fill").toMap(), result.outline->fill) ||
                !number(map, "width", result.outline->width))
                return std::nullopt;
        }
        if (options.contains("shadow"))
        {
            const auto map = options.value("shadow").toMap();
            result.shadow.emplace();
            if (map.contains("color"))
                result.shadow->color = map.value("color").toString().toStdString();
            if (!allowed(map, {"color", "opacity", "blur", "x", "y"}) ||
                !number(map, "opacity", result.shadow->opacity) ||
                !number(map, "blur", result.shadow->blur) || !number(map, "x", result.shadow->x) ||
                !number(map, "y", result.shadow->y))
                return std::nullopt;
        }
        if (options.contains("glow"))
        {
            const auto map = options.value("glow").toMap();
            result.glow.emplace();
            if (map.contains("color"))
                result.glow->color = map.value("color").toString().toStdString();
            if (!allowed(map, {"color", "opacity", "radius"}) ||
                !number(map, "opacity", result.glow->opacity) || !number(map, "radius", result.glow->radius))
                return std::nullopt;
        }
        if (options.contains("reflection"))
        {
            const auto map = options.value("reflection").toMap();
            result.reflection.emplace();
            if (!allowed(map, {"opacity", "offset", "endOpacity", "startPosition", "endPosition"}) ||
                !number(map, "opacity", result.reflection->opacity) ||
                !number(map, "offset", result.reflection->offset) ||
                !number(map, "endOpacity", result.reflection->end_opacity) ||
                !number(map, "startPosition", result.reflection->start_position) ||
                !number(map, "endPosition", result.reflection->end_position))
                return std::nullopt;
        }
        if (options.contains("warp"))
            result.warp = options.value("warp").toString().toStdString();
        if (options.contains("vertical"))
            result.vertical = options.value("vertical").toString().toStdString();
        if (options.contains("warpAdjustment"))
        {
            result.warp_adjustment = 0;
            if (!number(options, "warpAdjustment", *result.warp_adjustment))
                return std::nullopt;
        }
        if (options.contains("rotation"))
        {
            result.rotation = 0;
            if (!number(options, "rotation", *result.rotation))
                return std::nullopt;
        }
        for (const auto* key : {"clipVertical", "clipHorizontal"})
            if (options.contains(key) && options.value(key).metaType().id() != QMetaType::Bool)
                return std::nullopt;
        if (options.contains("clipVertical"))
            result.clip_vertical = options.value("clipVertical").toBool();
        if (options.contains("clipHorizontal"))
            result.clip_horizontal = options.value("clipHorizontal").toBool();
        return result;
    }
}
