#include "mirrorfly/presentation.hpp"
#include "presentation_animation_parser.hpp"
#include "presentation_chart.hpp"
#include "presentation_group_edit.hpp"
#include "presentation_group_transform.hpp"
#include "presentation_math.hpp"
#include "presentation_notes.hpp"
#include "presentation_parse_package.hpp"
#include "presentation_parse_style.hpp"
#include "presentation_preservation.hpp"
#include "presentation_sections.hpp"
#include "presentation_table.hpp"
#include "presentation_transition.hpp"

#include <pugixml.hpp>
#include <utf8/checked.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace
{

    using Node = pugi::xml_node;
    using Matrix = std::array<double, 6>;
    constexpr double emu_per_point = 12700.0;
    constexpr double pi = 3.14159265358979323846;
    constexpr std::size_t maximum_text_bytes = 8 * 1024 * 1024;

    using mirrorfly::presentation_parse_package::attribute;
    using mirrorfly::presentation_parse_package::child;
    using mirrorfly::presentation_parse_package::ends_with;
    using mirrorfly::presentation_parse_package::Failure;
    using mirrorfly::presentation_parse_package::local_name;
    using mirrorfly::presentation_parse_package::number;
    using mirrorfly::presentation_parse_package::Package;
    using mirrorfly::presentation_parse_package::related;
    using mirrorfly::presentation_parse_package::relationships;
    using mirrorfly::presentation_parse_package::valid_part_path;
    using mirrorfly::presentation_parse_package::xml;
    using mirrorfly::presentation_parse_style::apply_color_map;
    using mirrorfly::presentation_parse_style::read_color;
    using mirrorfly::presentation_parse_style::read_theme;
    using mirrorfly::presentation_parse_style::Theme;

    struct SlideContext
    {
        Package* package = nullptr;
        mirrorfly::PresentationSlide* slide = nullptr;
        const std::map<std::string, int>* slide_indices = nullptr;
        Theme theme;
        Node master;
        Node layout;
        Node default_text;
        Node table_styles;
        std::string master_path;
        std::string layout_path;
    };

    void warning(std::vector<std::string>& warnings, const std::string& text)
    {
        if (warnings.size() < 80 && std::find(warnings.begin(), warnings.end(), text) == warnings.end())
        {
            warnings.push_back(text);
        }
    }

    void warn(SlideContext& context, const std::string& text)
    {
        warning(context.slide->warnings, text);
    }

    bool boolean(pugi::xml_attribute value, bool fallback)
    {
        if (!value)
        {
            return fallback;
        }
        const std::string text = value.value();
        return text == "true" || text == "1" || text == "on";
    }

    mirrorfly::PresentationClickAction read_click_action(
        Node properties, const std::string& source, SlideContext& context)
    {
        mirrorfly::PresentationClickAction result;
        const auto link = child(properties, "hlinkClick");
        if (!link)
            return result;
        const auto& relations = relationships(*context.package, source);
        const auto relation = relations.find(attribute(link, "id").value());
        if (relation != relations.end() && !relation->second.fragment.empty())
        {
            warn(context, "内部书签超链接已保留，当前放映暂不支持定位到文字锚点。");
            return result;
        }
        const std::string action = link.attribute("action").value();
        constexpr const char* show_jump = "ppaction://hlinkshowjump?jump=";
        if (action == "ppaction://hlinksldjump")
        {
            if (relation != relations.end() && !relation->second.external &&
                ends_with(relation->second.type, "/slide") && context.slide_indices)
            {
                const auto target = context.slide_indices->find(relation->second.target);
                if (target != context.slide_indices->end())
                {
                    result.kind = "slide";
                    result.target_slide = target->second;
                }
            }
            return result;
        }
        if (action.compare(0, std::strlen(show_jump), show_jump) != 0)
            return result;
        const auto jump = action.substr(std::strlen(show_jump));
        if (jump == "firstslide" || jump == "lastslide" || jump == "nextslide" || jump == "previousslide" ||
            jump == "lastslideviewed" || jump == "endshow")
            result.kind = jump;
        else if (!jump.empty())
        {
            int one_based = 0;
            const auto parsed = std::from_chars(jump.data(), jump.data() + jump.size(), one_based);
            if (parsed.ec == std::errc{} && parsed.ptr == jump.data() + jump.size() && one_based > 0 &&
                context.slide_indices && one_based <= static_cast<int>(context.slide_indices->size()))
            {
                result.kind = "slide";
                result.target_slide = one_based - 1;
            }
        }
        return result;
    }

    Matrix multiply(const Matrix& left, const Matrix& right)
    {
        return {left[0] * right[0] + left[2] * right[1], left[1] * right[0] + left[3] * right[1],
            left[0] * right[2] + left[2] * right[3], left[1] * right[2] + left[3] * right[3],
            left[0] * right[4] + left[2] * right[5] + left[4],
            left[1] * right[4] + left[3] * right[5] + left[5]};
    }

    Matrix translation(double x, double y)
    {
        return {1, 0, 0, 1, x, y};
    }

    Matrix orientation(Node transform, double x, double y, double width, double height)
    {
        const double angle = number(transform.attribute("rot")) / 60000.0 * pi / 180;
        const double flip_x = boolean(transform.attribute("flipH"), false) ? -1 : 1;
        const double flip_y = boolean(transform.attribute("flipV"), false) ? -1 : 1;
        const Matrix rotation{std::cos(angle) * flip_x, std::sin(angle) * flip_x, -std::sin(angle) * flip_y,
            std::cos(angle) * flip_y, 0, 0};
        return multiply(multiply(translation(x + width / 2, y + height / 2), rotation),
            translation(-width / 2, -height / 2));
    }

    Node indexed_child(Node parent, unsigned index)
    {
        unsigned current = 1;
        for (auto entry : parent.children())
        {
            if (current++ == index)
            {
                return entry;
            }
        }
        return {};
    }

    std::string node_part_path(const Package& package, Node node)
    {
        const auto found = package.document_paths.find(node.root().internal_object());
        return found == package.document_paths.end() ? std::string{} : found->second;
    }

    mirrorfly::PresentationFill fill_node(
        Node node, SlideContext& context, const std::string& placeholder = "#000000")
    {
        mirrorfly::PresentationFill result;
        const auto kind = local_name(node.name());
        if (kind == "solidFill")
        {
            const auto color = read_color(node, context.theme, placeholder);
            result.color = color.first;
            result.opacity = color.second;
        }
        else if (kind == "gradFill")
        {
            for (auto stop : child(node, "gsLst").children())
            {
                const auto color = read_color(stop, context.theme, placeholder);
                result.stops.push_back({std::clamp(number(stop.attribute("pos")) / 100000.0, 0.0, 1.0),
                    color.first, color.second});
                if (result.stops.size() > 64)
                {
                    throw Failure{mirrorfly::PresentationError::TooLarge, "渐变层次超过预览限制。"};
                }
            }
            std::stable_sort(result.stops.begin(), result.stops.end(), [](const auto& left, const auto& right)
            {
                return left.position < right.position;
            });
            if (!result.stops.empty() &&
                std::all_of(result.stops.begin(), result.stops.end(), [&result](const auto& stop)
            {
                return std::abs(stop.opacity - result.stops.front().opacity) < 1e-8;
            }))
            {
                result.opacity = result.stops.front().opacity;
                for (auto& stop : result.stops)
                {
                    stop.opacity = 1;
                }
            }
            result.angle_degrees = number(child(node, "lin").attribute("ang")) / 60000;
            if (child(node, "path"))
            {
                warn(context, "路径渐变按线性渐变显示。");
            }
        }
        else if (kind == "pattFill")
        {
            const auto color = read_color(child(node, "bgClr"), context.theme, placeholder);
            result.color = color.first;
            result.opacity = color.second;
            const auto foreground = read_color(child(node, "fgClr"), context.theme, placeholder);
            result.pattern = node.attribute("prst").as_string();
            result.pattern_foreground_color = foreground.first;
            result.pattern_foreground_opacity = foreground.second;
            if (!mirrorfly::presentation_pattern_supported(result.pattern))
                warn(context, "此图案填充尚未预览，仅显示背景色；原始图案与前景色保留。");
        }
        else if (kind == "blipFill")
        {
            const auto source_path = node_part_path(*context.package, node);
            const auto blip = child(node, "blip");
            const auto& relations = relationships(*context.package, source_path);
            const auto found = relations.find(attribute(blip, "embed").value());
            if (found == relations.end() || found->second.external ||
                !ends_with(found->second.type, "/image") ||
                !context.package->parts.count(found->second.target))
            {
                warn(context, "图片填充的资源缺失或为外部链接；原始定义保留。");
                return result;
            }
            result.image_path = found->second.target;
            context.package->used_images.insert(result.image_path);
            const auto crop = child(node, "srcRect");
            const auto target = child(child(node, "stretch"), "fillRect");
            const char* sides[]{"l", "t", "r", "b"};
            for (std::size_t index = 0; index < 4; ++index)
            {
                result.image_crop[index] =
                    std::clamp(number(crop.attribute(sides[index])) / 100000, 0.0, 0.999);
                result.image_fill_rect[index] =
                    std::clamp(number(target.attribute(sides[index])) / 100000, -10.0, 0.999);
            }
            if (result.image_crop[0] + result.image_crop[2] >= 1 ||
                result.image_crop[1] + result.image_crop[3] >= 1)
                result.image_crop.fill(0);
            if (result.image_fill_rect[0] + result.image_fill_rect[2] >= 1 ||
                result.image_fill_rect[1] + result.image_fill_rect[3] >= 1)
                result.image_fill_rect.fill(0);
            result.opacity =
                std::clamp(number(child(blip, "alphaModFix").attribute("amt"), 100000) / 100000, 0.0, 1.0);
            if (const auto tile = child(node, "tile"))
            {
                result.image_tile = true;
                result.image_scale = {
                    std::clamp(number(tile.attribute("sx"), 100000) / 100000, 0.001, 1000.0),
                    std::clamp(number(tile.attribute("sy"), 100000) / 100000, 0.001, 1000.0)};
                result.image_offset = {number(tile.attribute("tx")) / emu_per_point,
                    number(tile.attribute("ty")) / emu_per_point};
                result.image_alignment = tile.attribute("algn").as_string("tl");
                result.image_flip = tile.attribute("flip").value();
            }
            result.image_dpi = std::clamp(number(node.attribute("dpi")), 0.0, 2400.0);
        }
        return result;
    }

    Node find_fill(Node properties)
    {
        for (auto candidate : properties.children())
        {
            const auto name = local_name(candidate.name());
            if (name == "noFill" || name == "solidFill" || name == "gradFill" || name == "blipFill" ||
                name == "pattFill" || name == "grpFill")
            {
                return candidate;
            }
        }
        return {};
    }

    Node placeholder(Node shape)
    {
        for (auto node : shape.children())
        {
            if (local_name(node.name()).rfind("nv", 0) == 0)
            {
                return child(child(node, "nvPr"), "ph");
            }
        }
        return {};
    }

    Node shape_tree(Node root)
    {
        return child(child(root, "cSld"), "spTree");
    }

    Node matching_placeholder(Node root, Node requested, bool match_type)
    {
        if (!requested)
        {
            return {};
        }
        std::string requested_type = requested.attribute("type").as_string("obj");
        if (requested_type == "ctrTitle")
        {
            requested_type = "title";
        }
        if (match_type &&
            (requested_type == "obj" || requested_type == "subTitle" || requested_type == "pic" ||
                requested_type == "chart" || requested_type == "tbl" || requested_type == "dgm" ||
                requested_type == "media" || requested_type == "clipArt"))
            requested_type = "body";
        for (auto shape : shape_tree(root).children())
        {
            const auto candidate = placeholder(shape);
            if (!candidate)
            {
                continue;
            }
            if (!match_type && candidate.attribute("idx").as_uint() == requested.attribute("idx").as_uint())
            {
                return shape;
            }
            std::string candidate_type = candidate.attribute("type").as_string("obj");
            if (candidate_type == "ctrTitle")
            {
                candidate_type = "title";
            }
            if (match_type && candidate_type == requested_type)
            {
                return shape;
            }
        }
        return {};
    }

    bool specified_shape_fill(Node shape)
    {
        return find_fill(child(shape, "spPr")) || child(child(shape, "style"), "fillRef");
    }

    bool specified_shape_outline(Node shape)
    {
        return child(child(shape, "spPr"), "ln") || child(child(shape, "style"), "lnRef");
    }

    std::string resolve_font(const std::string& value, const Theme& theme)
    {
        if (value == "+mj-lt")
        {
            return theme.major_font;
        }
        if (value == "+mn-lt")
        {
            return theme.minor_font;
        }
        if (value == "+mj-ea")
        {
            return theme.major_east_asian;
        }
        if (value == "+mn-ea")
        {
            return theme.minor_east_asian;
        }
        return value;
    }

    void apply_run(
        mirrorfly::PresentationRun& run, Node properties, SlideContext& context, const char* source)
    {
        const auto& theme = context.theme;
        const bool local_override =
            std::strcmp(source, "slide") == 0 || std::strcmp(source, "tableCell") == 0;
        if (properties.attribute("sz"))
        {
            run.font_size = std::clamp(number(properties.attribute("sz")) / 100, 1.0, 400.0);
            run.font_size_source = source;
            run.local_size_override = run.local_size_override || local_override;
        }
        run.bold = boolean(properties.attribute("b"), run.bold);
        run.italic = boolean(properties.attribute("i"), run.italic);
        if (properties.attribute("u"))
        {
            run.underline = std::string(properties.attribute("u").value()) != "none";
        }
        const auto latin = child(properties, "latin").attribute("typeface");
        const auto asian = child(properties, "ea").attribute("typeface");
        if (latin && *latin.value())
        {
            const auto resolved = resolve_font(latin.value(), theme);
            if (!resolved.empty())
            {
                run.font_family = resolved;
                run.font_family_source = source;
                run.local_font_override = run.local_font_override || local_override;
            }
        }
        if (asian && *asian.value())
        {
            run.east_asian_font_family = resolve_font(asian.value(), theme);
            if (!run.east_asian_font_family.empty())
            {
                run.east_asian_font_source = source;
                run.local_font_override = run.local_font_override || local_override;
            }
        }
        const auto color = read_color(child(properties, "solidFill"), theme);
        if (!color.first.empty())
        {
            run.color = color.first;
            run.opacity = color.second;
            run.fill = {};
            run.color_source = source;
            run.local_color_override = run.local_color_override || local_override;
        }
        if (child(properties, "noFill"))
        {
            run.opacity = 0;
            run.fill = {};
            run.color_source = source;
            run.local_color_override = run.local_color_override || local_override;
        }
        if (const auto gradient = child(properties, "gradFill"))
        {
            run.fill = fill_node(gradient, context);
            run.opacity = 1;
            run.color_source = source;
            run.local_color_override = run.local_color_override || local_override;
        }
        if (properties.attribute("spc"))
            run.spacing = std::clamp(number(properties.attribute("spc")) / 100, -50.0, 200.0);
        if (properties.attribute("baseline"))
            run.baseline = std::clamp(number(properties.attribute("baseline")) / 100000, -1.0, 1.0);
        if (properties.attribute("strike"))
            run.strike = std::string(properties.attribute("strike").value()) != "noStrike";
        if (const auto line = child(properties, "ln"))
        {
            const auto outline = read_color(child(line, "solidFill"), theme);
            run.effects.outline_color = outline.first;
            run.effects.outline_opacity = outline.second;
            run.effects.outline_fill = {};
            if (const auto fill = find_fill(line))
                run.effects.outline_fill = fill_node(fill, context);
            run.effects.outline_width =
                std::clamp(number(line.attribute("w"), 12700) / emu_per_point, 0.0, 72.0);
        }
        if (const auto effects = child(properties, "effectLst"))
        {
            auto& output = run.effects;
            output.shadow_opacity = output.glow_opacity = output.reflection_opacity = 0;
            if (const auto shadow = child(effects, "outerShdw"))
            {
                const auto value = read_color(shadow, theme);
                output.shadow_color = value.first;
                output.shadow_opacity = value.second;
                output.shadow_blur =
                    std::clamp(number(shadow.attribute("blurRad")) / emu_per_point, 0.0, 72.0);
                const double distance =
                    std::clamp(number(shadow.attribute("dist")) / emu_per_point, 0.0, 200.0);
                const double angle = number(shadow.attribute("dir")) / 60000 * pi / 180;
                output.shadow_x = distance * std::cos(angle);
                output.shadow_y = distance * std::sin(angle);
            }
            if (const auto glow = child(effects, "glow"))
            {
                const auto value = read_color(glow, theme);
                output.glow_color = value.first;
                output.glow_opacity = value.second;
                output.glow_radius = std::clamp(number(glow.attribute("rad")) / emu_per_point, 0.0, 72.0);
            }
            if (const auto reflection = child(effects, "reflection"))
            {
                output.reflection_opacity =
                    std::clamp(number(reflection.attribute("stA"), 50000) / 100000, 0.0, 1.0);
                output.reflection_offset =
                    std::clamp(number(reflection.attribute("dist")) / emu_per_point, 0.0, 200.0);
                output.reflection_end_opacity =
                    std::clamp(number(reflection.attribute("endA")) / 100000, 0.0, 1.0);
                output.reflection_start_position =
                    std::clamp(number(reflection.attribute("stPos")) / 100000, 0.0, 1.0);
                output.reflection_end_position =
                    std::clamp(number(reflection.attribute("endPos"), 100000) / 100000, 0.0, 1.0);
            }
            if (child(effects, "innerShdw") || child(effects, "softEdge") || child(properties, "scene3d"))
                warn(context, "文字内部阴影、柔化及三维参数暂未完整显示；原始样式保留。");
        }
    }

    double spacing(Node node, double fallback)
    {
        const auto points = child(node, "spcPts");
        return points ? std::clamp(number(points.attribute("val")) / 100, 0.0, 10000.0) : fallback;
    }

    void apply_paragraph(mirrorfly::PresentationParagraph& paragraph, Node properties, SlideContext& context)
    {
        const std::string alignment = properties.attribute("algn").value();
        if (!alignment.empty())
        {
            paragraph.alignment = "left";
            if (alignment == "ctr")
            {
                paragraph.alignment = "center";
            }
            else if (alignment == "r")
            {
                paragraph.alignment = "right";
            }
            else if (alignment == "just" || alignment == "dist")
            {
                paragraph.alignment = "justify";
            }
        }
        if (properties.attribute("marL"))
        {
            paragraph.margin_left =
                std::clamp(number(properties.attribute("marL")) / emu_per_point, -10000.0, 10000.0);
        }
        if (properties.attribute("indent"))
        {
            paragraph.first_line_indent =
                std::clamp(number(properties.attribute("indent")) / emu_per_point, -10000.0, 10000.0);
        }
        if (child(properties, "buNone"))
        {
            paragraph.bullet.clear();
            paragraph.bullet_image_path.clear();
            paragraph.numbered = false;
        }
        if (const auto bullet = child(properties, "buChar"))
        {
            paragraph.bullet = bullet.attribute("char").value();
            paragraph.bullet_image_path.clear();
            paragraph.numbered = false;
        }
        if (const auto numbered = child(properties, "buAutoNum"))
        {
            paragraph.numbered = true;
            paragraph.bullet.clear();
            paragraph.bullet_image_path.clear();
            paragraph.number_start =
                static_cast<int>(std::clamp(number(numbered.attribute("startAt"), 1), 1.0, 32767.0));
            paragraph.number_format = numbered.attribute("type").as_string("arabicPeriod");
        }
        if (const auto picture = child(properties, "buBlip"))
        {
            paragraph.bullet.clear();
            paragraph.bullet_image_path.clear();
            paragraph.numbered = false;
            const auto blip = child(picture, "blip");
            const auto source_path = node_part_path(*context.package, properties);
            const auto& source_relations = relationships(*context.package, source_path);
            const auto found = source_relations.find(attribute(blip, "embed").value());
            if (found != source_relations.end() && !found->second.external &&
                ends_with(found->second.type, "/image") && context.package->parts.count(found->second.target))
            {
                paragraph.bullet_image_path = found->second.target;
                context.package->used_images.insert(paragraph.bullet_image_path);
            }
            else
            {
                paragraph.bullet = "•";
                warn(context, "图片项目符号资源缺失，已使用普通圆点。");
            }
        }
        const auto line = child(properties, "lnSpc");
        if (const auto percent = child(line, "spcPct"))
        {
            paragraph.line_spacing = std::clamp(number(percent.attribute("val")) / 100000, 0.1, 10.0);
            paragraph.fixed_line_spacing = 0;
        }
        if (child(line, "spcPts"))
        {
            paragraph.fixed_line_spacing = spacing(line, 0);
        }
        const auto apply_spacing = [&](const char* name, double& points, double& percent)
        {
            const auto source = child(properties, name);
            if (const auto relative = child(source, "spcPct"))
            {
                percent = std::clamp(number(relative.attribute("val")) / 100000, 0.0, 10.0);
                points = 0;
            }
            else if (child(source, "spcPts"))
            {
                points = spacing(source, 0);
                percent = -1;
            }
        };
        apply_spacing("spcBef", paragraph.space_before, paragraph.space_before_percent);
        apply_spacing("spcAft", paragraph.space_after, paragraph.space_after_percent);
        if (child(properties, "buClrTx"))
        {
            paragraph.bullet_color.clear();
            paragraph.bullet_opacity = 1;
        }
        if (const auto color = child(properties, "buClr"))
        {
            const auto value = read_color(color, context.theme);
            paragraph.bullet_color = value.first;
            paragraph.bullet_opacity = value.second;
        }
        if (child(properties, "buFontTx"))
            paragraph.bullet_font.clear();
        if (const auto font = child(properties, "buFont"))
            paragraph.bullet_font = resolve_font(font.attribute("typeface").value(), context.theme);
        if (child(properties, "buSzTx"))
        {
            paragraph.bullet_size_percent = 1;
            paragraph.bullet_size_points = 0;
        }
        if (const auto size = child(properties, "buSzPct"))
        {
            paragraph.bullet_size_percent = std::clamp(number(size.attribute("val")) / 100000, 0.25, 4.0);
            paragraph.bullet_size_points = 0;
        }
        if (const auto size = child(properties, "buSzPts"))
            paragraph.bullet_size_points = std::clamp(number(size.attribute("val")) / 100, 1.0, 400.0);
    }

    void apply_body(mirrorfly::PresentationText& text, Node properties, SlideContext& context)
    {
        const std::pair<const char*, double*> insets[]{{"lIns", &text.inset_left},
            {"rIns", &text.inset_right}, {"tIns", &text.inset_top}, {"bIns", &text.inset_bottom}};
        for (const auto& entry : insets)
        {
            if (properties.attribute(entry.first))
            {
                *entry.second =
                    std::clamp(number(properties.attribute(entry.first)) / emu_per_point, 0.0, 10000.0);
            }
        }
        if (const auto anchor = properties.attribute("anchor"))
        {
            const std::string value = anchor.value();
            text.vertical_alignment = value == "ctr" ? "center" : value == "b" ? "bottom" : "top";
        }
        if (properties.attribute("wrap"))
        {
            text.wrap = std::string(properties.attribute("wrap").value()) != "none";
        }
        if (const auto overflow = properties.attribute("vertOverflow"))
        {
            text.clip_vertical = std::string(overflow.value()) != "overflow";
            if (std::string(overflow.value()) == "ellipsis")
                warn(context, "文本框省略号溢出暂按裁剪显示；原始设置保留。");
        }
        if (const auto overflow = properties.attribute("horzOverflow"))
            text.clip_horizontal = std::string(overflow.value()) == "clip";
        if (const auto fit = child(properties, "normAutofit"))
        {
            text.auto_fit = true;
            text.font_scale = std::clamp(number(fit.attribute("fontScale"), 100000) / 100000, 0.01, 1.0);
            text.line_spacing_reduction =
                std::clamp(number(fit.attribute("lnSpcReduction")) / 100000, 0.0, 0.9);
        }
        else if (child(properties, "noAutofit") || child(properties, "spAutoFit"))
        {
            text.auto_fit = false;
            text.font_scale = 1;
            text.line_spacing_reduction = 0;
        }
        if (number(properties.attribute("numCol"), 1) > 1)
        {
            warn(context, "多栏文本框暂按单栏显示。");
        }
        if (properties.attribute("vert"))
            text.vertical = properties.attribute("vert").value();
        if (properties.attribute("rot"))
            text.rotation = number(properties.attribute("rot")) / 60000;
        if (const auto warp = child(properties, "prstTxWarp"))
        {
            text.warp = warp.attribute("prst").value();
            for (auto adjustment : child(warp, "avLst").children())
            {
                const std::string formula = adjustment.attribute("fmla").value();
                if (formula.rfind("val ", 0) == 0)
                {
                    char* end = nullptr;
                    const double value = std::strtod(formula.c_str() + 4, &end);
                    if (end != formula.c_str() + 4 && *end == 0 && std::isfinite(value))
                        text.warp_adjustment = std::clamp(value / 100000, 0.0, 1.0);
                }
            }
            if (text.warp != "textNoShape" && text.warp != "textPlain")
                warn(context, "艺术字曲线使用近似变形，原始预设与控制点保留。");
        }
        if (!text.vertical.empty() && text.vertical != "horz" && text.vertical != "vert" &&
            text.vertical != "vert270")
            warn(context, "部分东亚竖排规则暂按旋转文字显示；原始方向保留。");
    }

    void read_text(mirrorfly::PresentationShape& result, Node shape, const std::vector<Node>& inherited,
        SlideContext& context, const std::vector<const char*>& inherited_sources = {})
    {
        const auto body = child(shape, "txBody");
        if (!body)
        {
            return;
        }
        std::vector<Node> sources = inherited;
        sources.push_back(shape);
        const char* own_source = "tableCell";
        if (!result.source_part.empty())
            own_source = result.source_part == context.slide->source_part ? "slide" : "inherited";
        for (const auto source : sources)
        {
            apply_body(result.text, child(child(source, "txBody"), "bodyPr"), context);
        }
        const auto ph = placeholder(shape);
        std::string type = ph.attribute("type").as_string("obj");
        for (auto source = inherited.rbegin(); source != inherited.rend() && type == "obj"; ++source)
        {
            type = placeholder(*source).attribute("type").as_string("obj");
        }
        const char* style_name = "otherStyle";
        if (type == "title" || type == "ctrTitle")
        {
            style_name = "titleStyle";
        }
        else if (ph && (type == "body" || type == "obj"))
        {
            style_name = "bodyStyle";
        }
        const auto master_style = child(child(context.master, "txStyles"), style_name);
        for (auto paragraph_node : body.children())
        {
            if (local_name(paragraph_node.name()) != "p")
            {
                continue;
            }
            mirrorfly::PresentationParagraph paragraph;
            mirrorfly::PresentationRun defaults;
            defaults.font_family =
                type == "title" || type == "ctrTitle" ? context.theme.major_font : context.theme.minor_font;
            defaults.font_family_source = "theme";
            defaults.east_asian_font_family = type == "title" || type == "ctrTitle"
                ? context.theme.major_east_asian
                : context.theme.minor_east_asian;
            defaults.east_asian_font_source = "theme";
            const auto properties = child(paragraph_node, "pPr");
            const int level = static_cast<int>(std::clamp(number(properties.attribute("lvl")), 0.0, 8.0));
            paragraph.list_level = level;
            const auto level_name = "lvl" + std::to_string(level + 1) + "pPr";
            std::vector<Node> styles{child(context.default_text, "defPPr"),
                child(context.default_text, level_name), child(master_style, "defPPr"),
                child(master_style, level_name)};
            const std::vector<const char*> style_sources{"presentation", "presentation", "master", "master"};
            for (std::size_t index = 0; index < styles.size(); ++index)
            {
                const auto style = styles[index];
                apply_paragraph(paragraph, style, context);
                apply_run(defaults, child(style, "defRPr"), context, style_sources[index]);
            }
            const auto apply_style = [&](Node style, const char* source)
            {
                styles.push_back(style);
                apply_paragraph(paragraph, style, context);
                apply_run(defaults, child(style, "defRPr"), context, source);
            };
            for (std::size_t index = 0; index < sources.size(); ++index)
            {
                const auto source = sources[index];
                const char* source_name = index == inherited.size() ? own_source
                    : index < inherited_sources.size()              ? inherited_sources[index]
                                                                    : "tableStyle";
                const auto reference = child(child(source, "style"), "fontRef");
                if (reference)
                {
                    const bool major = std::string(reference.attribute("idx").value()) == "major";
                    defaults.font_family = major ? context.theme.major_font : context.theme.minor_font;
                    defaults.font_family_source = source_name;
                    defaults.east_asian_font_family =
                        major ? context.theme.major_east_asian : context.theme.minor_east_asian;
                    defaults.east_asian_font_source = source_name;
                    const auto color = read_color(reference, context.theme);
                    if (!color.first.empty())
                    {
                        defaults.color = color.first;
                        defaults.opacity = color.second;
                        defaults.fill = {};
                        defaults.color_source = source_name;
                    }
                }
                const auto list_style = child(child(source, "txBody"), "lstStyle");
                apply_style(child(list_style, "defPPr"), source_name);
                apply_style(child(list_style, level_name), source_name);
                if (source != shape)
                {
                    for (auto inherited_paragraph : child(source, "txBody").children())
                        if (local_name(inherited_paragraph.name()) == "p" &&
                            child(inherited_paragraph, "pPr").attribute("lvl").as_int() == level)
                        {
                            apply_style(child(inherited_paragraph, "pPr"), source_name);
                            break;
                        }
                }
            }
            apply_style(properties, own_source);
            const bool has_custom_tabs = std::any_of(styles.begin(), styles.end(), [](Node style)
            {
                return child(style, "tabLst");
            });
            for (auto run_node : paragraph_node.children())
            {
                const auto kind = local_name(run_node.name());
                const bool math = kind == "m" && (child(run_node, "oMath") || child(run_node, "oMathPara"));
                if (kind != "r" && kind != "fld" && kind != "br" && kind != "tab" && !math)
                {
                    continue;
                }
                auto run = defaults;
                run.plain_text = kind == "r";
                apply_run(run, child(run_node, "rPr"), context, own_source);
                const auto run_properties = child(run_node, "rPr");
                if (!result.source_part.empty())
                {
                    run.click_action = read_click_action(run_properties, result.source_part, context);
                    run.click_action.from_text = !run.click_action.kind.empty();
                }
                if (child(run_properties, "highlight"))
                {
                    warn(context, "文字高亮背景暂未显示；原始格式保留。");
                }
                if (math)
                {
                    try
                    {
                        mirrorfly::presentation_math::append_text(run_node, run.text, 0);
                    }
                    catch (const mirrorfly::presentation_math::Limit& limit)
                    {
                        throw Failure{mirrorfly::PresentationError::TooLarge, limit.message};
                    }
                    run.font_family = "Cambria Math";
                    run.font_family_source = "approximation";
                    warn(context, "公式使用只读文字近似显示；原始公式结构保留。");
                }
                else
                {
                    run.text = kind == "br" ? "\n"
                        : kind == "tab"     ? "\t"
                                            : child(run_node, "t").text().as_string();
                }
                context.package->text_bytes += run.text.size();
                if (context.package->text_bytes > maximum_text_bytes)
                {
                    throw Failure{mirrorfly::PresentationError::TooLarge, "演示文件的文字超过预览限制。"};
                }
                paragraph.runs.push_back(std::move(run));
            }
            if (paragraph.numbered && paragraph.number_format != "arabicPeriod" &&
                paragraph.number_format != "arabicParenR")
            {
                warn(context, "部分编号格式使用简化排版。");
            }
            if (has_custom_tabs &&
                std::any_of(paragraph.runs.begin(), paragraph.runs.end(), [](const auto& run)
            {
                return run.text.find('\t') != std::string::npos;
            }))
            {
                warn(context, "包含正文制表符的自定义制表位使用简化排版。");
            }
            if (paragraph.runs.empty())
            {
                apply_run(defaults, child(paragraph_node, "endParaRPr"), context, own_source);
                paragraph.runs.push_back(std::move(defaults));
            }
            result.text.paragraphs.push_back(std::move(paragraph));
        }
    }

    void apply_shape_style(mirrorfly::PresentationShape& shape, Node source, SlideContext& context)
    {
        const auto properties = child(source, "spPr");
        const auto style = child(source, "style");
        if (const auto reference = child(style, "fillRef"))
        {
            const auto index = reference.attribute("idx").as_uint();
            const auto list = child(context.theme.format, index >= 1001 ? "bgFillStyleLst" : "fillStyleLst");
            const auto node = indexed_child(list, index >= 1001 ? index - 1000 : index);
            const auto color = read_color(reference, context.theme);
            shape.fill = fill_node(node, context, color.first);
        }
        if (const auto fill = find_fill(properties))
        {
            shape.fill = fill_node(fill, context);
        }
        Node line;
        std::string placeholder_color = "#000000";
        if (const auto reference = child(style, "lnRef"))
        {
            line = indexed_child(
                child(context.theme.format, "lnStyleLst"), reference.attribute("idx").as_uint());
            placeholder_color = read_color(reference, context.theme).first;
        }
        const auto local_line = child(properties, "ln");
        for (auto line_node : {line, local_line})
        {
            if (line_node.attribute("w"))
            {
                shape.outline_width =
                    std::clamp(number(line_node.attribute("w")) / emu_per_point, 0.0, 100.0);
            }
            if (const auto fill = find_fill(line_node))
            {
                const auto parsed = fill_node(fill, context, placeholder_color);
                shape.outline_color = parsed.color;
                shape.outline_opacity = parsed.opacity;
                shape.outline_fill = parsed;
            }
            auto& outline = shape.line_style;
            if (line_node.attribute("cap"))
                outline.cap = line_node.attribute("cap").value();
            for (const auto* join : {"round", "bevel", "miter"})
                if (child(line_node, join))
                    outline.join = join;
            for (const auto* end : {"headEnd", "tailEnd"})
                if (const auto node = child(line_node, end))
                {
                    auto& target = std::string(end) == "headEnd" ? outline.head : outline.tail;
                    target.type = node.attribute("type").as_string("none");
                    target.width = node.attribute("w").as_string("med");
                    target.length = node.attribute("len").as_string("med");
                }
            if (const auto dash = child(line_node, "prstDash"))
            {
                const std::string value = dash.attribute("val").value();
                const auto& presets = mirrorfly::presentation_line_dash_presets();
                if (std::find(presets.begin(), presets.end(), value) != presets.end())
                    outline.dashes = mirrorfly::presentation_line_dash_pattern(value);
            }
            if (const auto dash = child(line_node, "custDash"))
            {
                outline.dashes.clear();
                for (const auto stop : dash.children())
                {
                    if (local_name(stop.name()) != "ds")
                        continue;
                    if (outline.dashes.size() >= 64)
                    {
                        warn(context, "自定义虚线超过 32 组，显示前 32 组；原始定义保留。");
                        break;
                    }
                    outline.dashes.push_back(std::clamp(number(stop.attribute("d")) / 100000, 0.01, 1000.0));
                    outline.dashes.push_back(std::clamp(number(stop.attribute("sp")) / 100000, 0.01, 1000.0));
                }
            }
        }
        auto geometry = child(properties, "prstGeom");
        const auto custom = child(properties, "custGeom");
        if (geometry || custom)
        {
            shape.geometry = custom ? "custom" : geometry.attribute("prst").as_string("rect");
            pugi::xml_document definition;
            auto copied = definition.append_copy(custom ? custom : geometry);
            std::vector<Node> nodes{copied};
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                auto item = nodes[index];
                item.set_name(("a:" + local_name(item.name())).c_str());
                for (auto sub : item.children())
                    if (sub.type() == pugi::node_element)
                        nodes.push_back(sub);
            }
            auto space = copied.attribute("xmlns:a");
            if (!space)
                space = copied.append_attribute("xmlns:a");
            space = "http://schemas.openxmlformats.org/drawingml/2006/main";
            std::ostringstream output;
            copied.print(output, "", pugi::format_raw);
            shape.geometry_definition = output.str();
        }
        const auto effect_reference = child(style, "effectRef");
        const auto effect_styles = child(context.theme.format, "effectStyleLst");
        const auto effect_style_index = effect_reference.attribute("idx").as_uint();
        const auto effect_style =
            effect_style_index ? indexed_child(effect_styles, effect_style_index) : Node{};
        const auto inherited_effects = child(effect_style, "effectLst");
        const auto direct_effects = child(properties, "effectLst");
        const auto effects = direct_effects ? direct_effects : inherited_effects;
        shape.effects_source = direct_effects ? "direct" : inherited_effects ? "theme" : "none";
        shape.theme_effect_style_index = effect_style_index;
        const auto referenced_color =
            effect_reference ? read_color(effect_reference, context.theme).first : std::string{};
        const auto effect_color = referenced_color.empty() ? "#000000" : referenced_color;
        if (effects)
        {
            auto& output = shape.effects;
            output.shadow_opacity = output.glow_opacity = output.reflection_opacity = 0;
            Node shadow = child(effects, "outerShdw");
            shape.inner_shadow = false;
            if (!shadow)
            {
                shadow = child(effects, "innerShdw");
                shape.inner_shadow = static_cast<bool>(shadow);
            }
            if (shadow)
            {
                const auto value = read_color(shadow, context.theme, effect_color);
                output.shadow_color = value.first;
                output.shadow_opacity = value.second;
                output.shadow_blur =
                    std::clamp(number(shadow.attribute("blurRad")) / emu_per_point, 0.0, 72.0);
                const double distance =
                    std::clamp(number(shadow.attribute("dist")) / emu_per_point, 0.0, 200.0);
                const double angle = number(shadow.attribute("dir")) / 60000 * pi / 180;
                output.shadow_x = distance * std::cos(angle);
                output.shadow_y = distance * std::sin(angle);
            }
            if (const auto glow = child(effects, "glow"))
            {
                const auto value = read_color(glow, context.theme, effect_color);
                output.glow_color = value.first;
                output.glow_opacity = value.second;
                output.glow_radius = std::clamp(number(glow.attribute("rad")) / emu_per_point, 0.0, 72.0);
            }
            if (const auto reflection = child(effects, "reflection"))
            {
                output.reflection_opacity =
                    std::clamp(number(reflection.attribute("stA"), 50000) / 100000, 0.0, 1.0);
                output.reflection_offset =
                    std::clamp(number(reflection.attribute("dist")) / emu_per_point, 0.0, 200.0);
            }
            if (const auto soft_edge = child(effects, "softEdge"))
            {
                shape.soft_edge_radius =
                    std::clamp(number(soft_edge.attribute("rad")) / emu_per_point, 0.0, 72.0);
            }
        }
        shape.approximate_3d =
            static_cast<bool>(child(properties, "scene3d")) || static_cast<bool>(child(properties, "sp3d"));
        if (child(properties, "effectDag"))
        {
            warn(context, "复合效果图暂未完整显示；原始定义继续保留。");
        }
        if (shape.inner_shadow || shape.soft_edge_radius > 0 || shape.approximate_3d)
        {
            warn(context, "内部阴影、柔化与三维场景使用有界二维近似。");
        }
    }

    void physical_shape_dimensions(mirrorfly::PresentationShape& shape)
    {
        auto& matrix = shape.transform;
        const double x_scale = std::hypot(matrix[0], matrix[1]);
        const double y_scale = std::hypot(matrix[2], matrix[3]);
        const double width = shape.width * x_scale;
        const double height = shape.height * y_scale;
        if (x_scale <= 0 || y_scale <= 0 || !std::isfinite(width) || !std::isfinite(height) || width < 0 ||
            height < 0 || width > 100000 || height > 100000)
            throw Failure{mirrorfly::PresentationError::InvalidXml, "组合内对象的实际尺寸超过范围。"};
        // Group child coordinates can use arbitrary units; fonts, insets and strokes use physical points.
        shape.width = width;
        shape.height = height;
        matrix[0] /= x_scale;
        matrix[1] /= x_scale;
        matrix[2] /= y_scale;
        matrix[3] /= y_scale;
    }

    bool editable_shape_path(Node node)
    {
        for (auto parent = node.parent(); parent; parent = parent.parent())
        {
            if (local_name(parent.name()) == "spTree")
                return true;
            if (local_name(parent.name()) != "grpSp")
                return false;
        }
        return false;
    }

    bool read_shape(
        Node node, const std::string& path, const Matrix& parent, bool inherit, SlideContext& context)
    {
        const auto kind = local_name(node.name());
        if (kind != "sp" && kind != "pic" && kind != "cxnSp")
        {
            return false;
        }
        if (++context.package->shape_count > mirrorfly::maximum_presentation_shapes)
        {
            throw Failure{mirrorfly::PresentationError::TooLarge, "演示文件的图形数量超过预览限制。"};
        }
        const auto ph = placeholder(node);
        std::vector<Node> inherited;
        std::vector<const char*> inherited_sources;
        Node layout;
        Node master;
        if (inherit && ph)
        {
            layout = matching_placeholder(context.layout, ph, false);
            master = matching_placeholder(context.master, layout ? placeholder(layout) : ph, true);
            if (master)
            {
                inherited.push_back(master);
                inherited_sources.push_back("master");
            }
            if (layout)
            {
                inherited.push_back(layout);
                inherited_sources.push_back("layout");
            }
        }
        Node transform;
        mirrorfly::PresentationShape result;
        result.source_part = path;
        if (ph && path == context.slide->source_part)
        {
            mirrorfly::PresentationPlaceholderInfo info;
            info.type = ph.attribute("type").as_string("obj");
            if (info.type == "obj" && layout)
                info.type = placeholder(layout).attribute("type").as_string("obj");
            if (info.type == "obj" && master)
                info.type = placeholder(master).attribute("type").as_string("obj");
            info.index = ph.attribute("idx").as_uint();
            info.has_layout = static_cast<bool>(layout);
            info.has_master = static_cast<bool>(master);
            info.local_fill_override = specified_shape_fill(node);
            info.inherited_fill_source = "none";
            if (specified_shape_fill(master))
                info.inherited_fill_source = "master";
            if (specified_shape_fill(layout))
                info.inherited_fill_source = "layout";
            info.fill_source = info.local_fill_override ? "slide" : info.inherited_fill_source;
            info.local_outline_override = specified_shape_outline(node);
            info.inherited_outline_source = "none";
            if (specified_shape_outline(master))
                info.inherited_outline_source = "master";
            if (specified_shape_outline(layout))
                info.inherited_outline_source = "layout";
            info.outline_source = info.local_outline_override ? "slide" : info.inherited_outline_source;
            result.placeholder = std::move(info);
        }
        result.editable = path == context.slide->source_part && editable_shape_path(node) &&
            !mirrorfly::presentation_math::contains(node, 0);
        result.source_parent_transform = parent;
        if (kind == "cxnSp")
        {
            result.geometry = "line";
        }
        for (auto source : inherited)
        {
            if (const auto specified = child(child(source, "spPr"), "xfrm"))
            {
                transform = specified;
            }
            apply_shape_style(result, source, context);
        }
        if (result.placeholder)
        {
            result.placeholder->inherited_fill = result.fill;
            result.placeholder->inherited_outline = {result.outline_color, result.outline_fill,
                result.outline_opacity, result.outline_width, result.line_style};
        }
        if (const auto specified = child(child(node, "spPr"), "xfrm"))
        {
            transform = specified;
        }
        if (!transform)
        {
            warn(context, "缺少位置信息的对象未显示。");
            return true;
        }
        const auto off = child(transform, "off");
        const auto ext = child(transform, "ext");
        const double x = number(off.attribute("x")) / emu_per_point;
        const double y = number(off.attribute("y")) / emu_per_point;
        result.width = number(ext.attribute("cx")) / emu_per_point;
        result.height = number(ext.attribute("cy")) / emu_per_point;
        if (result.width < 0 || result.height < 0 || result.width > 100000 || result.height > 100000 ||
            std::abs(x) > 100000 || std::abs(y) > 100000)
        {
            throw Failure{mirrorfly::PresentationError::InvalidXml, "演示文件包含超出范围的图形位置。"};
        }
        result.transform = multiply(parent, orientation(transform, x, y, result.width, result.height));
        for (const auto value : result.transform)
        {
            if (!std::isfinite(value) || std::abs(value) > 1e7)
            {
                throw Failure{mirrorfly::PresentationError::InvalidXml, "演示文件的组合变换超过范围。"};
            }
        }
        for (auto non_visual : node.children())
        {
            const auto properties = child(non_visual, "cNvPr");
            if (properties.attribute("id"))
                result.source_id = properties.attribute("id").value();
            if (properties)
                result.click_action = read_click_action(properties, path, context);
            if (boolean(properties.attribute("hidden"), false))
            {
                return true;
            }
            if (properties.attribute("name"))
            {
                result.name = properties.attribute("name").value();
            }
        }
        apply_shape_style(result, node, context);
        read_text(result, node, inherited, context, inherited_sources);
        if (result.click_action.kind.empty())
        {
            mirrorfly::PresentationClickAction shared;
            bool all_linked = true;
            bool saw_text = false;
            for (const auto& paragraph : result.text.paragraphs)
                for (const auto& run : paragraph.runs)
                {
                    if (run.text.empty())
                        continue;
                    saw_text = true;
                    if (run.click_action.kind.empty() ||
                        (!shared.kind.empty() &&
                            (run.click_action.kind != shared.kind ||
                                run.click_action.target_slide != shared.target_slide)))
                        all_linked = false;
                    if (shared.kind.empty())
                        shared = run.click_action;
                }
            if (saw_text && all_linked)
            {
                shared.from_text = true;
                result.click_action = std::move(shared);
            }
        }
        auto paths = mirrorfly::presentation_geometry(
            result.geometry, result.width, result.height, result.geometry_definition);
        if (paths.error.empty())
        {
            context.package->geometry_bytes += result.geometry_definition.size();
            for (const auto& path_data : paths.paths)
                context.package->geometry_bytes += sizeof(mirrorfly::PresentationPath) +
                    path_data.commands.size() * sizeof(mirrorfly::PresentationPathCommand);
            if (context.package->geometry_bytes > mirrorfly::maximum_presentation_expanded_bytes)
                throw Failure{mirrorfly::PresentationError::TooLarge, "演示图形路径超过内存预算。"};
            result.path_geometry = std::make_shared<const mirrorfly::PresentationGeometry>(std::move(paths));
        }
        else
            warn(context, paths.error);
        physical_shape_dimensions(result);
        if (kind == "pic")
        {
            const auto blip_fill = child(node, "blipFill");
            const auto blip = child(blip_fill, "blip");
            const auto id = attribute(blip, "embed").value();
            const auto& relations = relationships(*context.package, path);
            const auto relation = relations.find(id);
            if (relation == relations.end() || relation->second.external ||
                !ends_with(relation->second.type, "/image") ||
                !context.package->parts.count(relation->second.target))
            {
                warn(context, "缺失或外部链接图片未加载。");
            }
            else
            {
                result.image_path = relation->second.target;
                context.package->used_images.insert(result.image_path);
            }
            const auto crop = child(blip_fill, "srcRect");
            const char* sides[]{"l", "t", "r", "b"};
            for (std::size_t index = 0; index < 4; ++index)
            {
                result.image_crop[index] =
                    std::clamp(number(crop.attribute(sides[index])) / 100000.0, 0.0, 0.999);
            }
            if (result.image_crop[0] + result.image_crop[2] >= 1 ||
                result.image_crop[1] + result.image_crop[3] >= 1)
            {
                result.image_crop.fill(0);
                warn(context, "无效的图片裁切已忽略。");
            }
            bool unsupported_effect = false;
            for (auto effect : blip.children())
            {
                const auto effect_name = local_name(effect.name());
                if (effect_name == "alphaModFix")
                {
                    result.image_opacity =
                        std::clamp(number(effect.attribute("amt"), 100000) / 100000.0, 0.0, 1.0);
                }
                else if (effect_name == "extLst")
                {
                    for (auto extension : effect.children())
                    {
                        for (auto payload : extension.children())
                        {
                            unsupported_effect =
                                unsupported_effect || local_name(payload.name()) != "useLocalDpi";
                        }
                    }
                }
                else
                {
                    unsupported_effect = true;
                }
            }
            if (child(blip_fill, "tile") || unsupported_effect)
            {
                warn(context, "图片平铺与图片特效暂未显示。");
            }
        }
        const auto& relations = relationships(*context.package, path);
        std::vector<Node> media_nodes;
        for (auto non_visual : node.children())
            if (const auto properties = child(non_visual, "nvPr"))
                media_nodes.push_back(properties);
        bool media_found = false;
        for (std::size_t index = 0; index < media_nodes.size(); ++index)
        {
            const auto entry = media_nodes[index];
            const auto tag = local_name(entry.name());
            if (tag == "videoFile" || tag == "audioFile" || tag == "wavAudioFile" || tag == "media")
            {
                media_found = true;
                auto id = attribute(entry, "embed");
                if (!id)
                    id = attribute(entry, "link");
                const auto found = relations.find(id.value());
                if (found != relations.end() && !found->second.external &&
                    context.package->parts.count(found->second.target) &&
                    (ends_with(found->second.type, "/video") || ends_with(found->second.type, "/audio") ||
                        ends_with(found->second.type, "/media")))
                {
                    result.media_path = found->second.target;
                    context.package->used_media.insert(result.media_path);
                }
            }
            for (auto sub : entry.children())
                if (sub.type() == pugi::node_element)
                    media_nodes.push_back(sub);
        }
        if (media_found && result.media_path.empty())
            warn(context, "媒体缺失或仅有外部链接，当前保留海报；需要本地媒体才能播放。");
        std::string type = ph.attribute("type").value();
        if (type.empty() && !inherited.empty())
        {
            type = placeholder(inherited.back()).attribute("type").value();
        }
        if (context.slide->title.empty() && (type == "title" || type == "ctrTitle"))
        {
            for (const auto& paragraph : result.text.paragraphs)
            {
                for (const auto& run : paragraph.runs)
                {
                    context.slide->title += run.text;
                }
                context.slide->title += " ";
            }
        }
        context.slide->shapes.push_back(std::move(result));
        return true;
    }

    mirrorfly::PresentationShape graphic_frame(Node node, const std::string& path, const Matrix& parent)
    {
        const auto transform = child(node, "xfrm");
        const auto off = child(transform, "off");
        const auto ext = child(transform, "ext");
        mirrorfly::PresentationShape frame;
        frame.width = number(ext.attribute("cx")) / emu_per_point;
        frame.height = number(ext.attribute("cy")) / emu_per_point;
        frame.transform = multiply(parent,
            orientation(transform, number(off.attribute("x")) / emu_per_point,
                number(off.attribute("y")) / emu_per_point, frame.width, frame.height));
        frame.source_part = path;
        frame.source_id = child(child(node, "nvGraphicFramePr"), "cNvPr").attribute("id").value();
        if (frame.width > 100000 || frame.height > 100000 ||
            std::any_of(frame.transform.begin(), frame.transform.end(), [](double item)
        {
            return !std::isfinite(item) || std::abs(item) > 1e7;
        }))
            throw Failure{mirrorfly::PresentationError::InvalidXml, "嵌入图形的尺寸或组合变换超过范围。"};
        physical_shape_dimensions(frame);
        return frame;
    }

    bool read_table(Node node, const std::string& path, const Matrix& parent, SlideContext& context)
    {
        const auto table = child(child(child(node, "graphic"), "graphicData"), "tbl");
        if (!table)
            return false;
        auto frame = graphic_frame(node, path, parent);
        frame.editable = path == context.slide->source_part && editable_shape_path(node) &&
            !mirrorfly::presentation_math::contains(table, 0);
        if (frame.width <= 0 || frame.height <= 0)
        {
            warn(context, "表格尺寸无效，原始对象保留。");
            return true;
        }
        context.slide->groups.push_back(
            {frame.source_id, frame.source_part, frame.transform, frame.width, frame.height});
        mirrorfly::PresentationTableReader reader;
        reader.header_fill.color = context.theme.colors.at("accent1");
        pugi::xml_document band;
        auto fill = band.append_child("solidFill");
        auto scheme = fill.append_child("schemeClr");
        scheme.append_attribute("val") = "accent1";
        scheme.append_child("tint").append_attribute("val") = "80000";
        reader.band_fill = fill_node(fill, context);
        reader.fill = [&context](Node source)
        {
            return fill_node(source, context);
        };
        reader.color = [&context](Node source)
        {
            return read_color(source, context.theme);
        };
        reader.text = [&context](Node cell, const std::vector<Node>& styles)
        {
            pugi::xml_document defaults;
            std::vector<Node> inherited;
            for (auto style : styles)
            {
                if (!style)
                    continue;
                auto source = defaults.append_child("p:sp");
                auto list = source.append_child("a:txBody").append_child("a:lstStyle");
                auto properties = list.append_child("a:lvl1pPr").append_child("a:defRPr");
                for (auto value : style.attributes())
                    if (local_name(value.name()) == "b" || local_name(value.name()) == "i")
                        properties.append_copy(value);
                const auto reference = child(style, "fontRef");
                if (reference)
                {
                    const bool major = std::string(reference.attribute("idx").value()) == "major";
                    properties.append_child("a:latin").append_attribute("typeface") =
                        (major ? context.theme.major_font : context.theme.minor_font).c_str();
                    properties.append_child("a:ea").append_attribute("typeface") =
                        (major ? context.theme.major_east_asian : context.theme.minor_east_asian).c_str();
                }
                auto color = read_color(style, context.theme);
                if (color.first.empty())
                    color = read_color(reference, context.theme);
                if (!color.first.empty())
                    properties.append_child("a:solidFill").append_child("a:srgbClr").append_attribute("val") =
                        color.first.substr(1).c_str();
                inherited.push_back(source);
            }
            mirrorfly::PresentationShape shape;
            read_text(shape, cell, inherited, context);
            return shape.text;
        };
        auto shapes = mirrorfly::presentation_table_shapes(
            table, frame, context.table_styles, reader, context.slide->warnings);
        if (context.package->shape_count + shapes.size() > mirrorfly::maximum_presentation_shapes)
            throw Failure{mirrorfly::PresentationError::TooLarge, "表格展开后的对象数量超过上限。"};
        context.package->shape_count += shapes.size();
        for (auto& shape : shapes)
            context.slide->shapes.push_back(std::move(shape));
        return true;
    }

    void read_tree(Node tree, const std::string& path, const Matrix& parent, bool inherit,
        bool skip_placeholders, unsigned depth, SlideContext& context);

    bool read_chart(Node node, const std::string& path, const Matrix& parent, SlideContext& context)
    {
        const auto chart = child(child(child(node, "graphic"), "graphicData"), "chart");
        if (!chart)
            return false;
        const auto frame = graphic_frame(node, path, parent);
        const auto& relations = relationships(*context.package, path);
        const auto relation = relations.find(attribute(chart, "id").value());
        const bool standard = relation != relations.end() && ends_with(relation->second.type, "/chart");
        const bool extended = relation != relations.end() && ends_with(relation->second.type, "/chartEx");
        if (relation == relations.end() || relation->second.external || (!standard && !extended) ||
            frame.width <= 0 || frame.height <= 0)
        {
            warn(context, "图表关联或尺寸无效；原始对象保留。");
            return true;
        }
        const auto space = xml(*context.package, relation->second.target, false);
        context.slide->groups.push_back(
            {frame.source_id, frame.source_part, frame.transform, frame.width, frame.height});
        if (extended)
        {
            std::string fallback_id = attribute(space, "fallbackImg").value();
            const auto fallback = child(space, "fallbackImg");
            if (fallback_id.empty() && fallback)
            {
                fallback_id = attribute(fallback, "id").value();
                if (fallback_id.empty())
                {
                    fallback_id = attribute(fallback, "embed").value();
                }
            }
            if (!fallback_id.empty())
            {
                const auto& chart_relations = relationships(*context.package, relation->second.target);
                const auto image = chart_relations.find(fallback_id);
                if (image != chart_relations.end() && !image->second.external &&
                    ends_with(image->second.type, "/image") &&
                    context.package->parts.count(image->second.target))
                {
                    auto result = frame;
                    result.source_id += ":chart:fallback";
                    result.editable = false;
                    result.source_groups = {frame.source_id};
                    result.image_path = image->second.target;
                    context.package->used_images.insert(result.image_path);
                    if (++context.package->shape_count > mirrorfly::maximum_presentation_shapes)
                    {
                        throw Failure{
                            mirrorfly::PresentationError::TooLarge, "图表展开后的对象数量超过上限。"};
                    }
                    context.slide->shapes.push_back(std::move(result));
                    warn(context, "扩展图表使用文档内回退图显示；原始图表数据保留。");
                    return true;
                }
                warn(context, "扩展图表回退图缺失，已尝试按缓存数据近似显示。");
            }
        }
        mirrorfly::PresentationChartReader reader;
        for (int index = 1; index <= 6; ++index)
            reader.colors.push_back(context.theme.colors.at("accent" + std::to_string(index)));
        reader.fill = [&context](Node source)
        {
            return fill_node(source, context);
        };
        reader.text = [&context](Node source)
        {
            pugi::xml_document document;
            auto shape_node = document.append_child("sp");
            if (source)
                shape_node.append_copy(source).set_name("txBody");
            mirrorfly::PresentationShape shape;
            read_text(shape, shape_node, {}, context);
            return shape.text;
        };
        auto shapes = extended
            ? mirrorfly::presentation_chart_ex_shapes(space, frame, reader, context.slide->warnings)
            : mirrorfly::presentation_chart_shapes(space, frame, reader, context.slide->warnings);
        if (context.package->shape_count + shapes.size() > mirrorfly::maximum_presentation_shapes)
            throw Failure{mirrorfly::PresentationError::TooLarge, "图表展开后的对象数量超过上限。"};
        context.package->shape_count += shapes.size();
        for (auto& shape : shapes)
        {
            for (const auto& paragraph : shape.text.paragraphs)
                for (const auto& run : paragraph.runs)
                    context.package->text_bytes += run.text.size();
            if (shape.path_geometry)
                for (const auto& item : shape.path_geometry->paths)
                    context.package->geometry_bytes += sizeof(mirrorfly::PresentationPath) +
                        item.commands.size() * sizeof(mirrorfly::PresentationPathCommand);
            if (context.package->text_bytes > maximum_text_bytes ||
                context.package->geometry_bytes > mirrorfly::maximum_presentation_expanded_bytes)
                throw Failure{mirrorfly::PresentationError::TooLarge, "图表展开后的文字或路径超过内存预算。"};
            context.slide->shapes.push_back(std::move(shape));
        }
        return true;
    }

    bool read_diagram(
        Node node, const std::string& path, const Matrix& parent, unsigned depth, SlideContext& context)
    {
        const auto ids = child(child(child(node, "graphic"), "graphicData"), "relIds");
        if (!ids)
            return false;
        const auto& relations = relationships(*context.package, path);
        const auto relation = relations.find(attribute(ids, "dm").value());
        if (relation == relations.end() || relation->second.external ||
            !ends_with(relation->second.type, "/diagramData"))
        {
            warn(context, "SmartArt 缺少可用的数据关联，原始对象保留。");
            return true;
        }
        const auto drawing_path = related(*context.package, relation->second.target, "diagramDrawing");
        const auto drawing = xml(*context.package, drawing_path, false);
        const auto tree = child(drawing, "spTree");
        const auto frame = graphic_frame(node, path, parent);
        if (frame.width <= 0 || frame.height <= 0)
        {
            warn(context, "SmartArt 包含无效画布尺寸，暂未显示；原始对象保留。");
            return true;
        }
        if (!tree)
        {
            const auto data = xml(*context.package, relation->second.target, false);
            const auto points = child(data, "ptLst");
            struct DiagramNode
            {
                Node source;
                std::string id;
                std::size_t parent = std::numeric_limits<std::size_t>::max();
                unsigned level = 0;
                double x = 0;
                double y = 0;
                double width = 0;
                double height = 0;
            };
            std::vector<DiagramNode> nodes;
            std::map<std::string, std::size_t> indexes;
            bool limited = false;
            for (const auto point : points.children())
            {
                if (local_name(point.name()) != "pt")
                {
                    continue;
                }
                const std::string type = attribute(point, "type").as_string("node");
                if (type != "node" && type != "asst")
                {
                    continue;
                }
                const std::string id = attribute(point, "modelId").value();
                if (id.empty() || indexes.count(id))
                {
                    continue;
                }
                if (nodes.size() >= 64)
                {
                    limited = true;
                    break;
                }
                indexes[id] = nodes.size();
                nodes.push_back({point, id});
            }
            if (nodes.empty())
            {
                warn(context, "SmartArt 未提供绘图缓存或可显示的数据节点；原始对象保留。");
                return true;
            }
            const auto connections = child(data, "cxnLst");
            for (const auto connection : connections.children())
            {
                const std::string connection_type = attribute(connection, "type").as_string("parOf");
                if (local_name(connection.name()) != "cxn" || connection_type != "parOf")
                {
                    continue;
                }
                const auto source = indexes.find(attribute(connection, "srcId").value());
                const auto destination = indexes.find(attribute(connection, "destId").value());
                if (source == indexes.end() || destination == indexes.end() ||
                    source->second == destination->second)
                {
                    continue;
                }
                auto& target = nodes[destination->second];
                if (target.parent == std::numeric_limits<std::size_t>::max())
                {
                    target.parent = source->second;
                }
            }
            for (std::size_t pass = 0; pass < nodes.size(); ++pass)
            {
                bool changed = false;
                for (auto& item : nodes)
                {
                    if (item.parent >= nodes.size())
                    {
                        continue;
                    }
                    const unsigned candidate = nodes[item.parent].level + 1;
                    if (candidate > item.level && candidate <= nodes.size())
                    {
                        item.level = candidate;
                        changed = true;
                    }
                }
                if (!changed)
                {
                    break;
                }
            }
            unsigned maximum_level = 0;
            for (const auto& item : nodes)
            {
                maximum_level = std::max(maximum_level, item.level);
            }
            if (maximum_level >= nodes.size())
            {
                for (auto& item : nodes)
                {
                    item.parent = std::numeric_limits<std::size_t>::max();
                    item.level = 0;
                }
                maximum_level = 0;
                warn(context, "SmartArt 数据关系包含循环，节点使用平铺近似；原始关系保留。");
            }
            if (maximum_level == 0)
            {
                const double aspect = std::max(0.25, frame.width / std::max(1.0, frame.height));
                const auto columns = std::max<std::size_t>(
                    1, static_cast<std::size_t>(std::ceil(std::sqrt(nodes.size() * aspect))));
                const auto rows = (nodes.size() + columns - 1) / columns;
                const double cell_width = frame.width / columns;
                const double cell_height = frame.height / rows;
                for (std::size_t index = 0; index < nodes.size(); ++index)
                {
                    auto& item = nodes[index];
                    item.width = std::max(1.0, cell_width * 0.72);
                    item.height = std::max(1.0, std::min(54.0, cell_height * 0.56));
                    item.x = cell_width * (index % columns + 0.5) - item.width / 2;
                    item.y = cell_height * (index / columns + 0.5) - item.height / 2;
                }
            }
            else
            {
                std::vector<std::vector<std::size_t>> levels(maximum_level + 1);
                for (std::size_t index = 0; index < nodes.size(); ++index)
                {
                    levels[nodes[index].level].push_back(index);
                }
                const double row_height = frame.height / levels.size();
                for (std::size_t level = 0; level < levels.size(); ++level)
                {
                    const double cell_width = frame.width / std::max<std::size_t>(1, levels[level].size());
                    for (std::size_t position = 0; position < levels[level].size(); ++position)
                    {
                        auto& item = nodes[levels[level][position]];
                        item.width = std::max(1.0, cell_width * 0.68);
                        item.height = std::max(1.0, std::min(48.0, row_height * 0.52));
                        item.x = cell_width * (position + 0.5) - item.width / 2;
                        item.y = row_height * (level + 0.5) - item.height / 2;
                    }
                }
            }
            std::vector<mirrorfly::PresentationShape> generated;
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                const auto& item = nodes[index];
                if (item.parent >= nodes.size())
                {
                    continue;
                }
                const auto& source = nodes[item.parent];
                mirrorfly::PresentationShape connector;
                connector.editable = false;
                connector.source_part = frame.source_part;
                connector.source_id =
                    frame.source_id + ":diagram-fallback:connector:" + std::to_string(index);
                connector.source_groups = {frame.source_id};
                connector.transform = frame.transform;
                connector.width = frame.width;
                connector.height = frame.height;
                connector.outline_color = context.theme.colors.at("accent1");
                connector.outline_opacity = 0.7;
                connector.outline_width = 1.25;
                mirrorfly::PresentationGeometry geometry;
                geometry.width = frame.width;
                geometry.height = frame.height;
                mirrorfly::PresentationPath connector_path;
                connector_path.width = frame.width;
                connector_path.height = frame.height;
                connector_path.commands.push_back({mirrorfly::PresentationPathAction::Move,
                    {source.x + source.width / 2, source.y + source.height / 2}});
                connector_path.commands.push_back({mirrorfly::PresentationPathAction::Line,
                    {item.x + item.width / 2, item.y + item.height / 2}});
                geometry.paths.push_back(std::move(connector_path));
                connector.path_geometry =
                    std::make_shared<const mirrorfly::PresentationGeometry>(std::move(geometry));
                generated.push_back(std::move(connector));
            }
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                const auto& item = nodes[index];
                mirrorfly::PresentationShape shape;
                shape.editable = false;
                shape.source_part = frame.source_part;
                shape.source_id = frame.source_id + ":diagram-fallback:" + item.id;
                shape.source_groups = {frame.source_id};
                shape.transform = frame.transform;
                shape.transform[4] += frame.transform[0] * item.x + frame.transform[2] * item.y;
                shape.transform[5] += frame.transform[1] * item.x + frame.transform[3] * item.y;
                shape.width = item.width;
                shape.height = item.height;
                shape.geometry = "roundRect";
                shape.fill.color = context.theme.colors.at("accent" + std::to_string(index % 6 + 1));
                shape.fill.opacity = 0.18;
                shape.outline_color = context.theme.colors.at("accent" + std::to_string(index % 6 + 1));
                shape.outline_width = 1.25;
                apply_shape_style(shape, item.source, context);
                if (const auto text = child(item.source, "t"))
                {
                    pugi::xml_document temporary;
                    auto source = temporary.append_child("sp");
                    source.append_copy(text).set_name("txBody");
                    read_text(shape, source, {}, context);
                }
                shape.text.vertical_alignment = "center";
                shape.text.wrap = true;
                shape.text.auto_fit = true;
                shape.text.inset_left = shape.text.inset_right = 5;
                shape.text.inset_top = shape.text.inset_bottom = 3;
                generated.push_back(std::move(shape));
            }
            if (context.package->shape_count + generated.size() > mirrorfly::maximum_presentation_shapes)
            {
                throw Failure{
                    mirrorfly::PresentationError::TooLarge, "SmartArt 数据模型展开后的对象数量超过上限。"};
            }
            context.package->shape_count += generated.size();
            for (const auto& shape : generated)
            {
                if (shape.path_geometry)
                {
                    for (const auto& item : shape.path_geometry->paths)
                    {
                        context.package->geometry_bytes += sizeof(mirrorfly::PresentationPath) +
                            item.commands.size() * sizeof(mirrorfly::PresentationPathCommand);
                    }
                }
            }
            if (context.package->geometry_bytes > mirrorfly::maximum_presentation_expanded_bytes)
            {
                throw Failure{
                    mirrorfly::PresentationError::TooLarge, "SmartArt 数据模型展开后的路径超过内存预算。"};
            }
            context.slide->groups.push_back(
                {frame.source_id, frame.source_part, frame.transform, frame.width, frame.height});
            context.slide->shapes.insert(context.slide->shapes.end(),
                std::make_move_iterator(generated.begin()), std::make_move_iterator(generated.end()));
            if (limited)
            {
                warn(context, "SmartArt 数据节点超过 64 个，只读预览显示前 64 个；原始数据保留。");
            }
            warn(context, "SmartArt 未提供绘图缓存，按数据模型关系只读近似布局；原始布局与样式保留。");
            return true;
        }
        const auto transform = child(child(tree, "grpSpPr"), "xfrm");
        const auto off = child(transform, "chOff");
        const auto ext = child(transform, "chExt");
        double width = number(ext.attribute("cx")) / emu_per_point;
        double height = number(ext.attribute("cy")) / emu_per_point;
        if (width <= 0 || height <= 0)
        {
            width = frame.width;
            height = frame.height;
        }
        const Matrix scaling{frame.width / width, 0, 0, frame.height / height,
            -number(off.attribute("x")) / emu_per_point * frame.width / width,
            -number(off.attribute("y")) / emu_per_point * frame.height / height};
        const auto start = context.slide->shapes.size();
        context.slide->groups.push_back(
            {frame.source_id, frame.source_part, frame.transform, frame.width, frame.height});
        read_tree(tree, drawing_path, multiply(frame.transform, scaling), false, false, depth + 1, context);
        for (auto index = start; index < context.slide->shapes.size(); ++index)
        {
            auto& shape = context.slide->shapes[index];
            shape.editable = false;
            shape.source_part = frame.source_part;
            shape.source_id = frame.source_id + ":diagram:" + shape.source_id;
            shape.source_groups = {frame.source_id};
        }
        warn(context, "SmartArt 使用文档内绘图缓存显示，布局编辑暂不支持。");
        return true;
    }

    void read_tree(Node tree, const std::string& path, const Matrix& parent, bool inherit,
        bool skip_placeholders, unsigned depth, SlideContext& context)
    {
        if (depth > 24)
        {
            throw Failure{mirrorfly::PresentationError::TooLarge, "演示文件的组合图形层级过深。"};
        }
        const bool top_level_slide = depth == 0 && path == context.slide->source_part;
        int layer_count = 0;
        bool known_layers = top_level_slide;
        if (top_level_slide)
            for (auto item : tree.children())
            {
                if (item.type() != pugi::node_element)
                    continue;
                const auto name = local_name(item.name());
                if (name == "sp" || name == "pic" || name == "cxnSp" || name == "graphicFrame" ||
                    name == "grpSp")
                    ++layer_count;
                else if (name != "nvGrpSpPr" && name != "grpSpPr")
                    known_layers = false;
            }
        int layer_index = 0;
        for (auto node : tree.children())
        {
            const auto kind = local_name(node.name());
            const bool layer_object =
                kind == "sp" || kind == "pic" || kind == "cxnSp" || kind == "graphicFrame" || kind == "grpSp";
            const int current_layer = layer_index;
            if (layer_object)
                ++layer_index;
            if (kind == "grpSp")
            {
                const auto transform = child(child(node, "grpSpPr"), "xfrm");
                const auto off = child(transform, "off");
                const auto ext = child(transform, "ext");
                const auto child_off = child(transform, "chOff");
                const auto child_ext = child(transform, "chExt");
                const double width = number(ext.attribute("cx")) / emu_per_point;
                const double height = number(ext.attribute("cy")) / emu_per_point;
                const double child_width = number(child_ext.attribute("cx")) / emu_per_point;
                const double child_height = number(child_ext.attribute("cy")) / emu_per_point;
                if (child_width <= 0 || child_height <= 0 || width <= 0 || height <= 0)
                {
                    warn(context, "无效尺寸的组合图形未显示。");
                    continue;
                }
                Matrix scaling{width / child_width, 0, 0, height / child_height,
                    -number(child_off.attribute("x")) / emu_per_point * width / child_width,
                    -number(child_off.attribute("y")) / emu_per_point * height / child_height};
                const auto group = multiply(orientation(transform, number(off.attribute("x")) / emu_per_point,
                                                number(off.attribute("y")) / emu_per_point, width, height),
                    scaling);
                const auto start = context.slide->shapes.size();
                const std::string group_id = child(child(node, "nvGrpSpPr"), "cNvPr").attribute("id").value();
                const auto frame_transform = multiply(parent,
                    orientation(transform, number(off.attribute("x")) / emu_per_point,
                        number(off.attribute("y")) / emu_per_point, width, height));
                if (context.slide->groups.size() >= mirrorfly::maximum_presentation_shapes)
                    throw Failure{mirrorfly::PresentationError::TooLarge, "页面组合图形数量超过上限。"};
                const bool editable_group =
                    depth == 0 && path == context.slide->source_part && !group_id.empty();
                bool ungroupable = editable_group && !transform.attribute("flipH").as_bool() &&
                    !transform.attribute("flipV").as_bool() && off.attribute("x") && off.attribute("y") &&
                    child_off.attribute("x") && child_off.attribute("y") && ext.attribute("cx") &&
                    ext.attribute("cy") && child_ext.attribute("cx") && child_ext.attribute("cy");
                const auto group_properties = child(child(node, "nvGrpSpPr"), "cNvPr");
                if (child(group_properties, "hlinkClick") || child(group_properties, "hlinkMouseOver"))
                    ungroupable = false;
                for (auto property : child(node, "grpSpPr").children())
                    if (property.type() == pugi::node_element && local_name(property.name()) != "xfrm")
                        ungroupable = false;
                const bool scaled = ext.attribute("cx").as_llong() != child_ext.attribute("cx").as_llong() ||
                    ext.attribute("cy").as_llong() != child_ext.attribute("cy").as_llong();
                const bool rotated = transform.attribute("rot").as_llong() % 21600000 != 0;
                for (auto member : node.children())
                {
                    if (member.type() != pugi::node_element)
                        continue;
                    const auto member_kind = local_name(member.name());
                    if (member_kind == "nvGrpSpPr" || member_kind == "grpSpPr")
                        continue;
                    if (((scaled || rotated) && member_kind != "pic") ||
                        (member_kind != "sp" && member_kind != "pic" && member_kind != "cxnSp" &&
                            member_kind != "graphicFrame"))
                    {
                        ungroupable = false;
                        break;
                    }
                    if ((scaled || rotated) && !child(child(member, "blipFill"), "stretch"))
                        ungroupable = false;
                    if (scaled || rotated)
                        for (auto property : child(member, "spPr").children())
                            if (property.type() == pugi::node_element &&
                                ((local_name(property.name()) == "ln" && !child(property, "noFill")) ||
                                    local_name(property.name()) == "effectLst" ||
                                    local_name(property.name()) == "effectDag" ||
                                    local_name(property.name()) == "scene3d" ||
                                    local_name(property.name()) == "sp3d"))
                                ungroupable = false;
                    const auto member_transform = member_kind == "graphicFrame"
                        ? child(member, "xfrm")
                        : child(child(member, "spPr"), "xfrm");
                    if (!mirrorfly::detail::flatten_presentation_group_member(transform, member_transform))
                    {
                        ungroupable = false;
                        break;
                    }
                }
                context.slide->groups.push_back(
                    {group_id, path, frame_transform, width, height, editable_group, ungroupable,
                        editable_group && mirrorfly::detail::presentation_group_is_extendable(node),
                        known_layers ? current_layer : -1, known_layers ? layer_count : 0});
                read_tree(
                    node, path, multiply(parent, group), inherit, skip_placeholders, depth + 1, context);
                for (auto index = start; index < context.slide->shapes.size(); ++index)
                    if (!group_id.empty())
                        context.slide->shapes[index].source_groups.push_back(group_id);
            }
            else if (kind == "AlternateContent")
            {
                const auto fallback = child(node, "Fallback");
                if (fallback)
                {
                    read_tree(fallback, path, parent, inherit, skip_placeholders, depth + 1, context);
                    warn(context, "扩展对象使用文档内提供的兼容内容。");
                }
                else
                {
                    bool handled = false;
                    for (const auto choice : node.children())
                    {
                        if (local_name(choice.name()) == "Choice" &&
                            mirrorfly::presentation_math::contains(choice, 0))
                        {
                            read_tree(choice, path, parent, inherit, skip_placeholders, depth + 1, context);
                            handled = true;
                            break;
                        }
                    }
                    if (!handled)
                    {
                        warn(context, "无兼容内容的扩展对象暂未显示。");
                    }
                }
            }
            else if (skip_placeholders && placeholder(node))
            {
                continue;
            }
            else if (read_shape(node, path, parent, inherit, context))
            {
                continue;
            }
            else if (kind == "graphicFrame" && read_table(node, path, parent, context))
            {
                continue;
            }
            else if (kind == "graphicFrame" && read_diagram(node, path, parent, depth, context))
            {
                continue;
            }
            else if (kind == "graphicFrame" && read_chart(node, path, parent, context))
            {
                continue;
            }
            else if (kind == "graphicFrame" || kind == "contentPart")
            {
                warn(context, "图表、SmartArt 与其他嵌入对象暂未显示。");
            }
        }
    }

    void read_background(Node root, SlideContext& context)
    {
        const auto background = child(child(root, "cSld"), "bg");
        if (const auto fill = find_fill(child(background, "bgPr")))
        {
            context.slide->background = fill_node(fill, context);
        }
        else if (const auto reference = child(background, "bgRef"))
        {
            const auto index = reference.attribute("idx").as_uint();
            const auto list = child(context.theme.format, index >= 1001 ? "bgFillStyleLst" : "fillStyleLst");
            const auto color = read_color(reference, context.theme);
            context.slide->background =
                fill_node(indexed_child(list, index >= 1001 ? index - 1000 : index), context, color.first);
        }
    }

    void read_transition(Node slide_root, SlideContext& context)
    {
        context.slide->transition.editable = mirrorfly::editable_presentation_transition(slide_root);
        Node node = child(slide_root, "transition");
        if (!node)
        {
            Node fallback;
            for (auto alternate : slide_root.children())
            {
                if (local_name(alternate.name()) != "AlternateContent")
                {
                    continue;
                }
                for (auto branch : alternate.children())
                {
                    const auto candidate = child(branch, "transition");
                    if (!candidate)
                    {
                        continue;
                    }
                    if (local_name(branch.name()) == "Choice")
                    {
                        node = candidate;
                        break;
                    }
                    if (!fallback)
                    {
                        fallback = candidate;
                    }
                }
                if (node)
                {
                    break;
                }
            }
            if (!node)
            {
                node = fallback;
            }
        }
        if (!node)
        {
            return;
        }
        auto& result = context.slide->transition;
        const std::string speed = attribute(node, "spd").value();
        result.duration = speed == "slow" ? 1.0 : speed == "fast" ? 0.3 : 0.5;
        if (const auto duration = attribute(node, "dur"))
        {
            const double milliseconds = number(duration, result.duration * 1000);
            if (milliseconds >= 0 && milliseconds <= 10000)
            {
                result.duration = milliseconds / 1000;
            }
        }
        result.advance_on_click = boolean(attribute(node, "advClick"), true);
        if (const auto advance = attribute(node, "advTm"))
        {
            const double milliseconds = number(advance, -1);
            if (milliseconds >= 0 && milliseconds <= 86400000)
            {
                result.advance_after = milliseconds / 1000;
            }
        }
        Node effect;
        for (auto candidate : node.children())
        {
            if (candidate.type() != pugi::node_element)
            {
                continue;
            }
            const std::string name = local_name(candidate.name());
            if (name != "sndAc" && name != "extLst")
            {
                effect = candidate;
                break;
            }
        }
        result.type = effect ? local_name(effect.name()) : "cut";
        result.direction = attribute(effect, "dir").value();
        result.orientation = attribute(effect, "orient").value();
        static const std::set<std::string> supported{
            "cover", "cut", "fade", "pull", "push", "split", "wipe", "zoom"};
        result.approximate = supported.find(result.type) == supported.end();
        if (result.approximate)
        {
            warn(context, "部分页面切换使用淡化近似；原始切换定义继续保留。");
        }
    }

    std::string image_mime(const std::string& path)
    {
        auto extension =
            path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.') + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
        static const std::map<std::string, std::string> types{{"png", "image/png"}, {"jpg", "image/jpeg"},
            {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"bmp", "image/bmp"}, {"tif", "image/tiff"},
            {"tiff", "image/tiff"}, {"wdp", "image/vnd.ms-photo"}, {"jxr", "image/jxr"},
            {"hdp", "image/vnd.ms-photo"}, {"svg", "image/svg+xml"}, {"emf", "image/x-emf"},
            {"wmf", "image/x-wmf"}};
        const auto found = types.find(extension);
        return found == types.end() ? "application/octet-stream" : found->second;
    }

    void report_progress(const mirrorfly::PresentationParseProgress& progress, std::size_t completed,
        std::size_t total) noexcept
    {
        if (!progress)
        {
            return;
        }
        try
        {
            progress(completed, total);
        }
        catch (...)
        {
            // Progress reporting must never make a valid document fail to load.
        }
    }

    mirrorfly::PresentationScene parse_package(
        std::vector<mirrorfly::PresentationPart>& parts, const mirrorfly::PresentationParseProgress& progress)
    {
        if (parts.size() > mirrorfly::maximum_presentation_parts)
        {
            throw Failure{mirrorfly::PresentationError::TooLarge, "演示文件包含过多条目。"};
        }
        Package package;
        std::size_t bytes = 0;
        for (auto& part : parts)
        {
            if (!valid_part_path(part.path) || !package.parts.emplace(part.path, &part).second)
            {
                throw Failure{mirrorfly::PresentationError::InvalidPackage, "演示文件包含重复或非法路径。"};
            }
            bytes += part.bytes.size();
            if (part.bytes.size() > mirrorfly::maximum_presentation_part_bytes ||
                bytes > mirrorfly::maximum_presentation_expanded_bytes)
            {
                throw Failure{mirrorfly::PresentationError::TooLarge, "演示文件解压后的内容超过限制。"};
            }
        }
        const auto presentation_path = related(package, "", "officeDocument");
        if (presentation_path.empty())
        {
            throw Failure{mirrorfly::PresentationError::InvalidPackage, "没有找到 PPTX 演示文稿入口。"};
        }
        const auto presentation = xml(package, presentation_path);
        if (local_name(presentation.name()) != "presentation")
        {
            throw Failure{mirrorfly::PresentationError::InvalidPackage, "文件内容不是 PPTX 演示文稿。"};
        }
        mirrorfly::PresentationScene scene;
        auto source = std::make_shared<mirrorfly::PresentationPackageState>();
        source->presentation_part = presentation_path;
        const auto size = child(presentation, "sldSz");
        scene.width = number(size.attribute("cx")) / emu_per_point;
        scene.height = number(size.attribute("cy")) / emu_per_point;
        if (scene.width <= 0 || scene.height <= 0 || scene.width > 20000 || scene.height > 20000)
        {
            throw Failure{mirrorfly::PresentationError::InvalidPackage, "演示文件的页面尺寸无效。"};
        }
        const auto& slide_relations = relationships(package, presentation_path);
        const auto embedded_font_list = child(presentation, "embeddedFontLst");
        static const std::pair<const char*, const char*> embedded_font_styles[]{
            {"regular", "regular"}, {"bold", "bold"}, {"italic", "italic"}, {"boldItalic", "boldItalic"}};
        for (auto embedded : embedded_font_list.children())
        {
            if (local_name(embedded.name()) != "embeddedFont" || scene.embedded_fonts.size() >= 64)
            {
                continue;
            }
            const std::string family = child(embedded, "font").attribute("typeface").value();
            if (family.empty())
            {
                continue;
            }
            for (const auto& style : embedded_font_styles)
            {
                const auto variant = child(embedded, style.first);
                if (!variant || scene.embedded_fonts.size() >= 64)
                {
                    continue;
                }
                const auto relation = slide_relations.find(attribute(variant, "id").value());
                if (relation == slide_relations.end() || relation->second.external ||
                    !ends_with(relation->second.type, "/font") ||
                    package.parts.find(relation->second.target) == package.parts.end())
                {
                    warning(scene.warnings, "部分嵌入字体关系无效，已使用安全字体回退。");
                    continue;
                }
                scene.embedded_fonts.push_back({family, style.second, relation->second.target, {}});
            }
        }
        std::vector<Node> slide_ids;
        std::vector<std::string> numeric_slide_ids;
        for (auto id : child(presentation, "sldIdLst").children())
        {
            if (local_name(id.name()) == "sldId")
            {
                slide_ids.push_back(id);
                numeric_slide_ids.emplace_back(id.attribute("id").value());
            }
        }
        const auto section_membership = read_presentation_sections(presentation, numeric_slide_ids, scene);
        std::map<std::string, int> slide_indices;
        for (std::size_t index = 0; index < slide_ids.size(); ++index)
        {
            std::string relationship_id;
            for (auto candidate : slide_ids[index].attributes())
                if (local_name(candidate.name()) == "id" && std::strchr(candidate.name(), ':'))
                    relationship_id = candidate.value();
            const auto relation = slide_relations.find(relationship_id);
            if (relation != slide_relations.end() && !relation->second.external &&
                ends_with(relation->second.type, "/slide"))
                slide_indices.emplace(relation->second.target, static_cast<int>(index));
        }
        report_progress(progress, 0, slide_ids.size());
        const auto default_text = child(presentation, "defaultTextStyle");
        const auto table_styles = xml(package, related(package, presentation_path, "tableStyles"), false);
        std::map<std::string, Theme> themes;
        std::map<std::string, int> theme_indices;
        std::set<std::string> used_slides;
        for (auto id : slide_ids)
        {
            if (scene.slides.size() >= mirrorfly::maximum_presentation_slides)
            {
                throw Failure{mirrorfly::PresentationError::TooLarge, "当前最多预览 200 张幻灯片。"};
            }
            // The relationship namespace distinguishes r:id from the numeric slide id.
            std::string relationship_id;
            for (auto candidate : id.attributes())
            {
                if (local_name(candidate.name()) == "id" && std::strchr(candidate.name(), ':'))
                {
                    relationship_id = candidate.value();
                }
            }
            const auto relation = slide_relations.find(relationship_id);
            if (relation == slide_relations.end() || relation->second.external ||
                !ends_with(relation->second.type, "/slide") ||
                !used_slides.insert(relation->second.target).second)
            {
                throw Failure{mirrorfly::PresentationError::InvalidPackage, "幻灯片顺序包含无效关联。"};
            }
            const auto& slide_path = relation->second.target;
            const auto slide_root = xml(package, slide_path);
            if (local_name(slide_root.name()) != "sld")
            {
                throw Failure{mirrorfly::PresentationError::InvalidXml, "幻灯片内容类型无效。"};
            }
            scene.slides.emplace_back();
            SlideContext context;
            context.package = &package;
            context.slide = &scene.slides.back();
            context.slide_indices = &slide_indices;
            context.slide->source_part = slide_path;
            context.slide->speaker_notes = mirrorfly::read_presentation_speaker_notes(
                xml(package, related(package, slide_path, "notesSlide"), false));
            context.slide->section_id = section_membership[scene.slides.size() - 1];
            context.default_text = default_text;
            context.table_styles = table_styles;
            context.layout_path = related(package, slide_path, "slideLayout");
            context.layout = xml(package, context.layout_path, false);
            context.master_path = context.layout ? related(package, context.layout_path, "slideMaster") : "";
            context.master = xml(package, context.master_path, false);
            const auto theme_path = context.master ? related(package, context.master_path, "theme") : "";
            const auto theme_root = xml(package, theme_path, false);
            const auto theme_elements = child(theme_root, "themeElements");
            const auto color_scheme = child(theme_elements, "clrScheme");
            const auto font_scheme = child(theme_elements, "fontScheme");
            context.slide->theme_available = theme_root && color_scheme && font_scheme;
            auto theme = themes.find(theme_path);
            if (theme == themes.end())
            {
                theme = themes.emplace(theme_path, read_theme(theme_root)).first;
            }
            if (context.slide->theme_available)
            {
                auto index = theme_indices.find(theme_path);
                if (index == theme_indices.end())
                {
                    mirrorfly::PresentationThemeDefinition definition;
                    definition.name = theme_root.attribute("name").value();
                    definition.colors = theme->second.colors;
                    definition.fonts = {{"majorLatin", theme->second.major_font},
                        {"minorLatin", theme->second.minor_font},
                        {"majorEastAsian", theme->second.major_east_asian},
                        {"minorEastAsian", theme->second.minor_east_asian}};
                    for (const auto& entry : definition.colors)
                        if (child(color_scheme, entry.first))
                            definition.editable_color_slots.insert(entry.first);
                    for (const auto& entry : definition.fonts)
                    {
                        const auto& slot = entry.first;
                        const bool major = slot.compare(0, 5, "major") == 0;
                        const bool east_asian = slot.find("EastAsian") != std::string::npos;
                        const auto collection = child(font_scheme, major ? "majorFont" : "minorFont");
                        if (child(collection, east_asian ? "ea" : "latin"))
                            definition.editable_font_slots.insert(slot);
                    }
                    const int new_index = static_cast<int>(scene.themes.size());
                    scene.themes.push_back(std::move(definition));
                    index = theme_indices.emplace(theme_path, new_index).first;
                }
                context.slide->theme_index = index->second;
            }
            context.theme = theme->second;
            if (!context.layout || !context.master || !theme_root)
            {
                warn(context, "未找到完整的版式、母版或主题，缺省样式使用系统字体与标准颜色。");
            }
            apply_color_map(context.theme, child(context.master, "clrMap"));
            const auto master_aliases = context.theme.aliases;
            apply_color_map(context.theme, child(child(context.layout, "clrMapOvr"), "overrideClrMapping"));
            if (child(child(slide_root, "clrMapOvr"), "masterClrMapping"))
            {
                context.theme.aliases = master_aliases;
            }
            apply_color_map(context.theme, child(child(slide_root, "clrMapOvr"), "overrideClrMapping"));
            context.slide->hidden = !boolean(slide_root.attribute("show"), true);
            read_background(context.master, context);
            read_background(context.layout, context);
            read_background(slide_root, context);
            read_transition(slide_root, context);
            const Matrix identity{1, 0, 0, 1, 0, 0};
            if (boolean(slide_root.attribute("showMasterSp"), true))
            {
                if (boolean(context.layout.attribute("showMasterSp"), true))
                {
                    read_tree(
                        shape_tree(context.master), context.master_path, identity, false, true, 0, context);
                }
                read_tree(shape_tree(context.layout), context.layout_path, identity, false, true, 0, context);
            }
            read_tree(shape_tree(slide_root), slide_path, identity, true, false, 0, context);
            mirrorfly::read_presentation_animations(slide_root, *context.slide);
            if (context.theme.approximated_color)
            {
                warn(context, "部分特殊颜色名称或颜色变换使用近似颜色。");
            }
            if (context.slide->title.empty())
            {
                context.slide->title = "幻灯片 " + std::to_string(scene.slides.size());
            }
            report_progress(progress, scene.slides.size(), slide_ids.size());
        }
        if (scene.slides.empty())
        {
            throw Failure{mirrorfly::PresentationError::InvalidPackage, "演示文件中没有幻灯片。"};
        }
        for (const auto& path : package.used_images)
        {
            const auto mime = image_mime(path);
            if (mime == "application/octet-stream")
            {
                warning(scene.warnings, "部分图片格式可能无法显示；建议在源文件中转换为 PNG 或 JPEG。");
            }
            scene.images.emplace_back(path, mime, std::shared_ptr<const std::string>{});
        }
        scene.native_editable = false;
        for (const auto& path : package.used_media)
        {
            auto suffix = path.substr(path.find_last_of('.') + 1);
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
            static const std::map<std::string, std::string> media_types{{"mp4", "video/mp4"},
                {"m4v", "video/mp4"}, {"mov", "video/quicktime"}, {"wmv", "video/x-ms-wmv"},
                {"avi", "video/x-msvideo"}, {"webm", "video/webm"}, {"mp3", "audio/mpeg"},
                {"wav", "audio/wav"}, {"m4a", "audio/mp4"}, {"wma", "audio/x-ms-wma"}};
            const auto type = media_types.find(suffix);
            scene.media.push_back(
                {path, type == media_types.end() ? "application/octet-stream" : type->second, {}});
        }
        for (auto& slide : scene.slides)
        {
            for (auto& shape : slide.shapes)
            {
                shape.id = scene.next_shape_id++;
            }
        }
        const char* font_warning = scene.embedded_fonts.empty()
            ? "预览使用本机字体；缺少原字体时换行与字距可能不同。"
            : "文档嵌入字体仅在当前会话内用于显示；不可用字体将安全回退。";
        warning(scene.warnings, font_warning);
        for (auto& part : parts)
            source->parts.emplace(part.path, std::make_shared<const std::string>(std::move(part.bytes)));
        for (auto& image : scene.images)
            image.bytes = source->parts.at(image.path);
        for (auto& media : scene.media)
            media.bytes = source->parts.at(media.path);
        for (auto& font : scene.embedded_fonts)
            font.bytes = source->parts.at(font.path);
        scene.source_package = std::move(source);
        return scene;
    }

}

namespace mirrorfly
{

    bool is_presentation_path(std::string path)
    {
        if (path.find('\0') != std::string::npos || !utf8::is_valid(path.begin(), path.end()))
        {
            return false;
        }
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
        return ends_with(path, ".pptx");
    }

    PresentationResult parse_presentation(
        std::vector<PresentationPart> parts, const PresentationParseProgress& progress)
    {
        try
        {
            PresentationResult result;
            result.scene = parse_package(parts, progress);
            return result;
        }
        catch (const Failure& failure)
        {
            return {failure.error, failure.message, {}};
        }
        catch (const std::bad_alloc&)
        {
            return {PresentationError::TooLarge, "没有足够的内存预览此演示文件。", {}};
        }
    }

}
