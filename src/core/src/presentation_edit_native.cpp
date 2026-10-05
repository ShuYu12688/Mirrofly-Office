#include "presentation_edit_native.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <sstream>

namespace mirrorfly::presentation_edit_native
{
    constexpr double emu_per_point = 12700.0;

    static bool finite(double value)
    {
        return std::isfinite(value);
    }

    bool valid_color(const std::string& color)
    {
        if (color.size() != 7 || color.front() != '#')
        {
            return false;
        }
        return std::all_of(color.begin() + 1, color.end(), [](unsigned char value)
        {
            return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') ||
                (value >= 'a' && value <= 'f');
        });
    }

    static std::string color_value(const std::string& color)
    {
        std::string value = color.substr(1);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::toupper(character));
        });
        return value;
    }

    static std::string xml_escape(const std::string& text)
    {
        std::string result;
        result.reserve(text.size());
        for (const char character : text)
        {
            switch (character)
            {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '\"':
                result += "&quot;";
                break;
            case '\'':
                result += "&apos;";
                break;
            default:
                result += character;
                break;
            }
        }
        return result;
    }

    long long emu(double points)
    {
        return std::llround(points * emu_per_point);
    }

    static int hundredths(double points)
    {
        return static_cast<int>(std::clamp(std::llround(points * 100), 100LL, 40000LL));
    }

    std::optional<TransformedBounds> transformed_bounds(const mirrorfly::PresentationShape& shape)
    {
        if (!finite(shape.width) || !finite(shape.height) || shape.width <= 0 || shape.height <= 0)
        {
            return std::nullopt;
        }
        const auto& matrix = shape.transform;
        if (!std::all_of(matrix.begin(), matrix.end(), [](double value)
        {
            return finite(value);
        }))
        {
            return std::nullopt;
        }
        const std::array<double, 4> x{matrix[4], matrix[0] * shape.width + matrix[4],
            matrix[2] * shape.height + matrix[4],
            matrix[0] * shape.width + matrix[2] * shape.height + matrix[4]};
        const std::array<double, 4> y{matrix[5], matrix[1] * shape.width + matrix[5],
            matrix[3] * shape.height + matrix[5],
            matrix[1] * shape.width + matrix[3] * shape.height + matrix[5]};
        return TransformedBounds{*std::min_element(x.begin(), x.end()), *std::min_element(y.begin(), y.end()),
            *std::max_element(x.begin(), x.end()), *std::max_element(y.begin(), y.end())};
    }

    std::optional<DecomposedTransform> decompose_transform(const mirrorfly::PresentationShape& shape)
    {
        const auto& matrix = shape.transform;
        const double scale_x = std::hypot(matrix[0], matrix[1]);
        const double scale_y = std::hypot(matrix[2], matrix[3]);
        if (!finite(scale_x) || !finite(scale_y) || scale_x < 1e-9 || scale_y < 1e-9 || !finite(matrix[4]) ||
            !finite(matrix[5]) || !finite(shape.width) || !finite(shape.height) || shape.width <= 0 ||
            shape.height <= 0)
        {
            return std::nullopt;
        }
        const double dot = (matrix[0] * matrix[2] + matrix[1] * matrix[3]) / (scale_x * scale_y);
        if (std::abs(dot) > 1e-6)
        {
            return std::nullopt;
        }
        DecomposedTransform result;
        result.width = shape.width * scale_x;
        result.height = shape.height * scale_y;
        const double center_x = matrix[0] * shape.width / 2 + matrix[2] * shape.height / 2 + matrix[4];
        const double center_y = matrix[1] * shape.width / 2 + matrix[3] * shape.height / 2 + matrix[5];
        result.x = center_x - result.width / 2;
        result.y = center_y - result.height / 2;
        result.angle = std::atan2(matrix[1] / scale_x, matrix[0] / scale_x) * 180 / 3.14159265358979323846;
        result.flip_vertical = matrix[0] * matrix[3] - matrix[1] * matrix[2] < 0;
        return result;
    }

    void compose_transform(mirrorfly::PresentationShape& shape, const DecomposedTransform& transform)
    {
        const double radians = transform.angle * 3.14159265358979323846 / 180;
        const double flip_y = transform.flip_vertical ? -1 : 1;
        const double a = std::cos(radians);
        const double b = std::sin(radians);
        const double c = -std::sin(radians) * flip_y;
        const double d = std::cos(radians) * flip_y;
        shape.transform = {a, b, c, d,
            transform.x + transform.width / 2 - a * transform.width / 2 - c * transform.height / 2,
            transform.y + transform.height / 2 - b * transform.width / 2 - d * transform.height / 2};
        shape.width = transform.width;
        shape.height = transform.height;
    }

    std::string solid_fill(const mirrorfly::PresentationFill& fill)
    {
        if (!fill.pattern.empty())
        {
            std::ostringstream xml;
            xml << "<a:pattFill prst=\"" << xml_escape(fill.pattern) << "\">";
            const auto color = [&](const char* role, const std::string& value, double opacity)
            {
                xml << "<a:" << role << "><a:srgbClr val=\"" << color_value(value) << "\">"
                    << "<a:alpha val=\"" << std::llround(std::clamp(opacity, 0.0, 1.0) * 100000)
                    << "\"/></a:srgbClr></a:" << role << ">";
            };
            color("fgClr", fill.pattern_foreground_color, fill.pattern_foreground_opacity);
            color("bgClr", fill.color, fill.opacity);
            xml << "</a:pattFill>";
            return xml.str();
        }
        if (!fill.stops.empty())
        {
            std::ostringstream xml;
            xml << "<a:gradFill rotWithShape=\"1\"><a:gsLst>";
            for (const auto& stop : fill.stops)
            {
                xml << "<a:gs pos=\"" << std::llround(std::clamp(stop.position, 0.0, 1.0) * 100000)
                    << "\"><a:srgbClr val=\"" << color_value(stop.color) << "\"";
                const double opacity = std::clamp(stop.opacity * fill.opacity, 0.0, 1.0);
                if (opacity < 0.999)
                {
                    xml << "><a:alpha val=\"" << std::llround(opacity * 100000) << "\"/></a:srgbClr></a:gs>";
                }
                else
                {
                    xml << "/></a:gs>";
                }
            }
            const double angle = std::fmod(std::fmod(fill.angle_degrees, 360.0) + 360.0, 360.0);
            xml << "</a:gsLst><a:lin ang=\"" << std::llround(angle * 60000)
                << "\" scaled=\"1\"/></a:gradFill>";
            return xml.str();
        }
        if (fill.color.empty())
        {
            return "<a:noFill/>";
        }
        std::ostringstream xml;
        const bool linked = !fill.theme_slot.empty() && fill.color == fill.theme_reference_color;
        if (!linked)
            xml << "<a:solidFill><a:srgbClr val=\"" << color_value(fill.color) << "\"";
        else
            xml << "<a:solidFill><a:schemeClr val=\"" << fill.theme_slot << "\"";
        if (fill.opacity < 0.999)
        {
            xml << "><a:alpha val=\"" << std::llround(std::clamp(fill.opacity, 0.0, 1.0) * 100000)
                << "\"/></a:" << (linked ? "schemeClr" : "srgbClr") << "></a:solidFill>";
        }
        else
        {
            xml << "/></a:solidFill>";
        }
        return xml.str();
    }

    static std::string shape_transform(const mirrorfly::PresentationShape& shape)
    {
        const auto transform = *decompose_transform(shape);
        std::ostringstream xml;
        xml << "<a:xfrm";
        if (std::abs(transform.angle) > 1e-8)
        {
            xml << " rot=\"" << std::llround(transform.angle * 60000) << "\"";
        }
        if (transform.flip_vertical)
        {
            xml << " flipV=\"1\"";
        }
        xml << "><a:off x=\"" << emu(transform.x) << "\" y=\"" << emu(transform.y) << "\"/><a:ext cx=\""
            << emu(transform.width) << "\" cy=\"" << emu(transform.height) << "\"/></a:xfrm>";
        return xml.str();
    }

    static std::string text_effects_xml(const mirrorfly::PresentationTextEffects& effects)
    {
        if (effects.shadow_opacity <= 0 && effects.glow_opacity <= 0 && effects.reflection_opacity <= 0)
            return {};
        std::ostringstream xml;
        const auto color = [&xml](const std::string& value, double opacity)
        {
            xml << "<a:srgbClr val=\"" << color_value(value) << "\"><a:alpha val=\""
                << std::llround(std::clamp(opacity, 0.0, 1.0) * 100000) << "\"/></a:srgbClr>";
        };
        xml << "<a:effectLst>";
        if (effects.glow_opacity > 0 && valid_color(effects.glow_color))
        {
            xml << "<a:glow rad=\"" << emu(effects.glow_radius) << "\">";
            color(effects.glow_color, effects.glow_opacity);
            xml << "</a:glow>";
        }
        if (effects.shadow_opacity > 0 && valid_color(effects.shadow_color))
        {
            const double angle =
                std::atan2(effects.shadow_y, effects.shadow_x) * 180 / 3.14159265358979323846;
            xml << "<a:outerShdw blurRad=\"" << emu(effects.shadow_blur) << "\" dist=\""
                << emu(std::hypot(effects.shadow_x, effects.shadow_y)) << "\" dir=\""
                << std::llround((angle < 0 ? angle + 360 : angle) * 60000) << "\">";
            color(effects.shadow_color, effects.shadow_opacity);
            xml << "</a:outerShdw>";
        }
        if (effects.reflection_opacity > 0)
            xml << "<a:reflection stA=\"" << std::llround(effects.reflection_opacity * 100000) << "\" endA=\""
                << std::llround(effects.reflection_end_opacity * 100000) << "\" stPos=\""
                << std::llround(effects.reflection_start_position * 100000) << "\" endPos=\""
                << std::llround(effects.reflection_end_position * 100000) << "\" dist=\""
                << emu(effects.reflection_offset) << "\" sy=\"-100000\"/>";
        xml << "</a:effectLst>";
        return xml.str();
    }

    static std::string run_xml(const mirrorfly::PresentationRun& run)
    {
        std::ostringstream xml;
        xml << "<a:r><a:rPr lang=\"zh-CN\" sz=\"" << hundredths(run.font_size) << "\" b=\""
            << (run.bold ? 1 : 0) << "\" i=\"" << (run.italic ? 1 : 0) << "\" u=\""
            << (run.underline ? "sng" : "none") << "\" spc=\"" << std::llround(run.spacing * 100)
            << "\" baseline=\"" << std::llround(run.baseline * 100000) << "\" strike=\""
            << (run.strike ? "sngStrike" : "noStrike") << "\">";
        if ((!run.effects.outline_color.empty() && valid_color(run.effects.outline_color)) ||
            !run.effects.outline_fill.stops.empty() || !run.effects.outline_fill.pattern.empty())
        {
            mirrorfly::PresentationFill outline{run.effects.outline_color, run.effects.outline_opacity};
            if (!run.effects.outline_fill.stops.empty() || !run.effects.outline_fill.pattern.empty())
                outline = run.effects.outline_fill;
            xml << "<a:ln w=\"" << emu(run.effects.outline_width) << "\">" << solid_fill(outline)
                << "</a:ln>";
        }
        mirrorfly::PresentationFill text_fill;
        text_fill.color = run.color;
        text_fill.opacity = run.opacity;
        text_fill.theme_slot = run.color_theme_slot;
        text_fill.theme_reference_color = run.color_theme_reference;
        xml << solid_fill(run.fill.stops.empty() && run.fill.pattern.empty() ? text_fill : run.fill);
        xml << text_effects_xml(run.effects);
        if (!run.font_family.empty())
        {
            const auto font =
                run.font_family == run.font_theme_reference && run.font_theme_slot == "majorLatin" ? "+mj-lt"
                : run.font_family == run.font_theme_reference && run.font_theme_slot == "minorLatin"
                ? "+mn-lt"
                : run.font_family;
            xml << "<a:latin typeface=\"" << xml_escape(font) << "\"/>";
        }
        if (!run.east_asian_font_family.empty())
        {
            const auto font = run.east_asian_font_family == run.east_asian_theme_reference &&
                    run.east_asian_theme_slot == "majorEastAsian"
                ? "+mj-ea"
                : run.east_asian_font_family == run.east_asian_theme_reference &&
                    run.east_asian_theme_slot == "minorEastAsian"
                ? "+mn-ea"
                : run.east_asian_font_family;
            xml << "<a:ea typeface=\"" << xml_escape(font) << "\"/>";
        }
        xml << "</a:rPr><a:t>" << xml_escape(run.text) << "</a:t></a:r>";
        return xml.str();
    }

    static std::string text_body_xml(
        const mirrorfly::PresentationText& text, const std::map<std::string, std::string>& image_relations)
    {
        std::ostringstream xml;
        const std::string anchor = text.vertical_alignment == "middle" || text.vertical_alignment == "center"
            ? "ctr"
            : (text.vertical_alignment == "bottom" ? "b" : "t");
        xml << "<p:txBody><a:bodyPr lIns=\"" << emu(text.inset_left) << "\" rIns=\"" << emu(text.inset_right)
            << "\" tIns=\"" << emu(text.inset_top) << "\" bIns=\"" << emu(text.inset_bottom) << "\" wrap=\""
            << (text.wrap ? "square" : "none") << "\" anchor=\"" << anchor << "\" rot=\""
            << std::llround(text.rotation * 60000) << "\" vert=\""
            << xml_escape(text.vertical.empty() ? "horz" : text.vertical) << "\" vertOverflow=\""
            << (text.clip_vertical ? "clip" : "overflow") << "\" horzOverflow=\""
            << (text.clip_horizontal ? "clip" : "overflow") << "\">";
        if (!text.warp.empty())
            xml << "<a:prstTxWarp prst=\"" << xml_escape(text.warp)
                << "\"><a:avLst><a:gd name=\"adj\" fmla=\"val " << std::llround(text.warp_adjustment * 100000)
                << "\"/></a:avLst></a:prstTxWarp>";
        if (text.auto_fit || text.font_scale < 0.999)
        {
            xml << "<a:normAutofit fontScale=\""
                << std::llround(std::clamp(text.font_scale, 0.01, 1.0) * 100000);
            xml << "\" lnSpcReduction=\""
                << std::llround(std::clamp(text.line_spacing_reduction, 0.0, 0.9) * 100000) << "\"/>";
        }
        xml << "</a:bodyPr><a:lstStyle/>";
        if (text.paragraphs.empty())
        {
            xml << "<a:p><a:endParaRPr lang=\"zh-CN\"/></a:p>";
        }
        for (const auto& paragraph : text.paragraphs)
        {
            const std::string alignment = paragraph.alignment == "center"
                ? "ctr"
                : (paragraph.alignment == "right" ? "r" : (paragraph.alignment == "justify" ? "just" : "l"));
            xml << "<a:p><a:pPr";
            if (paragraph.list_level > 0)
                xml << " lvl=\"" << std::clamp(paragraph.list_level, 0, 8) << "\"";
            xml << " algn=\"" << alignment << "\" marL=\"" << emu(paragraph.margin_left) << "\" indent=\""
                << emu(paragraph.first_line_indent) << "\">";
            if (paragraph.fixed_line_spacing > 0)
            {
                xml << "<a:lnSpc><a:spcPts val=\"" << hundredths(paragraph.fixed_line_spacing)
                    << "\"/></a:lnSpc>";
            }
            else if (std::abs(paragraph.line_spacing - 1) > 1e-6)
            {
                xml << "<a:lnSpc><a:spcPct val=\""
                    << std::llround(std::clamp(paragraph.line_spacing, 0.1, 10.0) * 100000)
                    << "\"/></a:lnSpc>";
            }
            if (paragraph.space_before_percent >= 0)
                xml << "<a:spcBef><a:spcPct val=\""
                    << std::llround(std::clamp(paragraph.space_before_percent, 0.0, 10.0) * 100000)
                    << "\"/></a:spcBef>";
            else if (paragraph.space_before > 0)
            {
                xml << "<a:spcBef><a:spcPts val=\"" << hundredths(paragraph.space_before)
                    << "\"/></a:spcBef>";
            }
            if (paragraph.space_after_percent >= 0)
                xml << "<a:spcAft><a:spcPct val=\""
                    << std::llround(std::clamp(paragraph.space_after_percent, 0.0, 10.0) * 100000)
                    << "\"/></a:spcAft>";
            else if (paragraph.space_after > 0)
            {
                xml << "<a:spcAft><a:spcPts val=\"" << hundredths(paragraph.space_after) << "\"/></a:spcAft>";
            }
            if (!paragraph.bullet_color.empty())
                xml << "<a:buClr><a:srgbClr val=\"" << color_value(paragraph.bullet_color)
                    << "\"><a:alpha val=\""
                    << std::llround(std::clamp(paragraph.bullet_opacity, 0.0, 1.0) * 100000)
                    << "\"/></a:srgbClr></a:buClr>";
            if (paragraph.bullet_size_points > 0)
                xml << "<a:buSzPts val=\"" << hundredths(paragraph.bullet_size_points) << "\"/>";
            else if (std::abs(paragraph.bullet_size_percent - 1) > 1e-6)
                xml << "<a:buSzPct val=\""
                    << std::llround(std::clamp(paragraph.bullet_size_percent, 0.25, 4.0) * 100000) << "\"/>";
            if (!paragraph.bullet_font.empty())
                xml << "<a:buFont typeface=\"" << xml_escape(paragraph.bullet_font) << "\"/>";
            const auto bullet_image = image_relations.find(paragraph.bullet_image_path);
            if (bullet_image != image_relations.end())
            {
                xml << "<a:buBlip><a:blip r:embed=\"" << bullet_image->second << "\"/></a:buBlip>";
            }
            else if (paragraph.numbered)
            {
                const auto format =
                    paragraph.number_format == "arabicParenR" ? "arabicParenR" : "arabicPeriod";
                xml << "<a:buAutoNum type=\"" << format << "\" startAt=\""
                    << std::clamp(paragraph.number_start, 1, 32767) << "\"/>";
            }
            else if (!paragraph.bullet.empty())
            {
                xml << "<a:buChar char=\"" << xml_escape(paragraph.bullet) << "\"/>";
            }
            else
            {
                xml << "<a:buNone/>";
            }
            xml << "</a:pPr>";
            for (const auto& run : paragraph.runs)
            {
                xml << run_xml(run);
            }
            xml << "<a:endParaRPr lang=\"zh-CN\"/></a:p>";
        }
        xml << "</p:txBody>";
        return xml.str();
    }

    static std::string shape_line_xml(const mirrorfly::PresentationShape& shape)
    {
        std::ostringstream xml;
        if (shape.outline_color.empty() && shape.outline_fill.stops.empty() &&
            shape.outline_fill.pattern.empty())
        {
            xml << "<a:ln><a:noFill/></a:ln>";
        }
        else
        {
            mirrorfly::PresentationFill outline;
            outline.color = shape.outline_color;
            outline.opacity = shape.outline_opacity;
            if (!shape.outline_fill.stops.empty() || !shape.outline_fill.pattern.empty())
                outline = shape.outline_fill;
            const auto& style = shape.line_style;
            xml << "<a:ln w=\"" << emu(shape.outline_width) << "\" cap=\"" << xml_escape(style.cap) << "\">";
            xml << solid_fill(outline);
            const auto preset = mirrorfly::presentation_line_dash_name(style.dashes);
            if (preset != "custom" && preset != "solid")
                xml << "<a:prstDash val=\"" << preset << "\"/>";
            else if (!style.dashes.empty() && style.dashes.size() % 2 == 0 && style.dashes.size() <= 64)
            {
                xml << "<a:custDash>";
                for (std::size_t index = 0; index < style.dashes.size(); index += 2)
                {
                    if (!finite(style.dashes[index]) || !finite(style.dashes[index + 1]))
                        continue;
                    xml << "<a:ds d=\""
                        << std::llround(std::clamp(style.dashes[index], 0.01, 1000.0) * 100000);
                    xml << "\" sp=\""
                        << std::llround(std::clamp(style.dashes[index + 1], 0.01, 1000.0) * 100000) << "\"/>";
                }
                xml << "</a:custDash>";
            }
            const auto join = style.join == "bevel" || style.join == "miter" ? style.join : "round";
            xml << "<a:" << join << "/>";
            const auto end = [&](const char* name, const mirrorfly::PresentationLineEnd& value)
            {
                xml << "<a:" << name << " type=\"" << xml_escape(value.type) << "\" w=\""
                    << xml_escape(value.width);
                xml << "\" len=\"" << xml_escape(value.length) << "\"/>";
            };
            end("headEnd", style.head);
            end("tailEnd", style.tail);
            xml << "</a:ln>";
        }
        return xml.str();
    }

    std::string shape_xml(const mirrorfly::PresentationShape& shape, std::size_t shape_index,
        const std::map<std::string, std::string>& image_relations)
    {
        std::ostringstream xml;
        const auto image = image_relations.find(shape.image_path);
        const auto identifier = shape_index + 2;
        if (image != image_relations.end())
        {
            xml << "<p:pic><p:nvPicPr><p:cNvPr id=\"" << identifier << "\" name=\""
                << xml_escape(shape.name.empty() ? "图片" : shape.name)
                << "\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr><p:blipFill><a:blip r:embed=\"" << image->second
                << "\"";
            if (shape.image_opacity < 1)
            {
                xml << "><a:alphaModFix amt=\""
                    << std::llround(std::clamp(shape.image_opacity, 0.0, 1.0) * 100000) << "\"/></a:blip>";
            }
            else
            {
                xml << "/>";
            }
            xml << "<a:srcRect l=\"" << std::llround(shape.image_crop[0] * 100000) << "\" t=\""
                << std::llround(shape.image_crop[1] * 100000) << "\" r=\""
                << std::llround(shape.image_crop[2] * 100000) << "\" b=\""
                << std::llround(shape.image_crop[3] * 100000)
                << "\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>" << shape_transform(shape)
                << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
                << "<a:noFill/>" << shape_line_xml(shape) << text_effects_xml(shape.effects)
                << "</p:spPr></p:pic>";
            return xml.str();
        }

        const bool line = shape.geometry == "line";
        xml << (line ? "<p:cxnSp><p:nvCxnSpPr>" : "<p:sp><p:nvSpPr>") << "<p:cNvPr id=\"" << identifier
            << "\" name=\"" << xml_escape(shape.name.empty() ? "对象" : shape.name) << "\"/>"
            << (line ? "<p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr>" : "<p:cNvSpPr/><p:nvPr/></p:nvSpPr>")
            << "<p:spPr>" << shape_transform(shape);
        if (!shape.geometry_definition.empty())
            xml << shape.geometry_definition;
        else
            xml << "<a:prstGeom prst=\"" << (line ? "line" : shape.geometry) << "\"><a:avLst/></a:prstGeom>";
        if (line)
        {
            xml << "<a:noFill/>";
        }
        else
        {
            xml << solid_fill(shape.fill);
        }
        xml << shape_line_xml(shape);
        xml << text_effects_xml(shape.effects);
        xml << "</p:spPr>";
        if (!line && !shape.text.paragraphs.empty())
        {
            xml << text_body_xml(shape.text, image_relations);
        }
        xml << (line ? "</p:cxnSp>" : "</p:sp>");
        return xml.str();
    }

}
