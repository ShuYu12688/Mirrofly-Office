#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;

    void check(bool value, const char* description)
    {
        if (!value)
        {
            std::cerr << description << '\n';
            ++failures;
        }
    }

    PresentationScene reopen(const PresentationScene& scene)
    {
        const auto serialized = serialize_presentation(scene);
        check(serialized.error == PresentationError::None, "style package serializes");
        auto parsed = parse_presentation(serialized.parts);
        check(parsed.error == PresentationError::None, "style package reparses");
        parsed.scene.native_editable = true;
        return std::move(parsed.scene);
    }

    const PresentationRun& first(const PresentationScene& scene)
    {
        return scene.slides.front().shapes.front().text.paragraphs.front().runs.front();
    }

    int run_tests()
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand add;
        add.action = PresentationEditAction::AddText;
        add.text = "艺术字 Office";
        check(apply_presentation_edit(scene, add).error == PresentationEditError::None, "add text");
        auto& shape = scene.slides.front().shapes.front();
        auto& runs = shape.text.paragraphs.front().runs;
        runs.push_back(runs.front());
        runs.back().text = " mixed";
        runs.back().bold = true;
        runs.back().effects.glow_color = "#12AABB";
        runs.back().effects.glow_opacity = 0.35;
        runs.back().effects.glow_radius = 5;
        shape.outline_width = 2;
        shape.outline_fill.stops = {{0, "#FF0000", 1}, {1, "#0000FF", 0.5}};

        PresentationEditCommand command;
        command.action = PresentationEditAction::FormatTextStyle;
        command.text_style.fill = PresentationFill{};
        command.text_style.fill->stops = {{0, "#E11222", 0.8}, {0.4, "#FFFF00", 1}, {1, "#0022FF", 1}};
        command.text_style.fill->angle_degrees = -90;
        command.text_style.outline = PresentationOutlineStyle{PresentationFill{"#223344", 0.75}, 1.5};
        command.text_style.shadow = PresentationShadowStyle{"#112233", 0.45, 4, -2, 3};
        command.text_style.reflection = PresentationReflectionStyle{0.55, 2, 0.003, 0.1, 0.455};
        command.text_style.warp = "textWave1";
        command.text_style.warp_adjustment = 0.3;
        command.text_style.rotation = 25;
        command.text_style.vertical = "vert270";
        command.text_style.clip_vertical = true;
        command.text_style.clip_horizontal = true;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "native whole-object style transaction");
        auto imported = reopen(scene);
        const auto& run = first(imported);
        check(run.fill.stops.size() == 3 && std::abs(run.fill.angle_degrees - 270) < 0.001,
            "multistop gradient and normalized angle survive reopen");
        check(std::abs(run.effects.outline_width - 1.5) < 0.001 && run.effects.outline_color == "#223344",
            "text outline survives reopen");
        check(std::abs(run.effects.reflection_end_position - 0.455) < 0.00001 &&
                std::abs(run.effects.reflection_end_opacity - 0.003) < 0.00001,
            "reflection fade parameters survive reopen");
        const auto& imported_shape = imported.slides.front().shapes.front();
        check(imported_shape.text.clip_vertical && imported_shape.text.clip_horizontal,
            "explicit text clipping survives reopen");
        check(imported_shape.outline_fill.stops.size() == 2, "shape gradient outline survives reopen");
        check(imported_shape.text.warp == "textWave1" && imported_shape.text.rotation == 25 &&
                imported_shape.text.vertical == "vert270",
            "text transforms survive reopen");

        // Unknown XML and links must stay untouched when editing one known property group.
        auto package = serialize_presentation(scene);
        for (auto& part : package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document xml;
                xml.load_string(part.bytes.c_str());
                auto properties = xml.child("p:sld")
                                      .child("p:cSld")
                                      .child("p:spTree")
                                      .child("p:sp")
                                      .child("p:txBody")
                                      .child("a:p")
                                      .child("a:r")
                                      .child("a:rPr");
                properties.append_child("a:hlinkClick").append_attribute("action") = "keep-link";
                properties.append_child("a:extLst").append_child("a:ext").append_attribute("uri") =
                    "keep-extension";
                std::ostringstream output;
                xml.save(output);
                part.bytes = output.str();
            }
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None, "extended package parses");
        imported = std::move(parsed.scene);
        imported.native_editable = true;
        command.text_style = {};
        command.text_style.shadow = PresentationShadowStyle{"#AA2200", 0.7, 2, 3, 4};
        check(apply_presentation_edit(imported, command).error == PresentationEditError::None,
            "imported shadow patch succeeds");
        auto patched = serialize_presentation(imported);
        for (const auto& original : package.parts)
        {
            const auto found = std::find_if(patched.parts.begin(), patched.parts.end(), [&](const auto& part)
            {
                return part.path == original.path;
            });
            check(found != patched.parts.end(), "package part retained");
            if (found == patched.parts.end())
                continue;
            if (original.path != "ppt/slides/slide1.xml")
                check(found->bytes == original.bytes, "unrelated package part byte-identical");
            else
                check(found->bytes.find("keep-link") != std::string::npos &&
                        found->bytes.find("keep-extension") != std::string::npos,
                    "unrelated run XML retained");
        }
        imported = reopen(imported);
        const auto& mixed = imported.slides.front().shapes.front().text.paragraphs.front().runs;
        check(mixed.size() == 2 && mixed.back().bold && mixed.back().effects.glow_color == "#12AABB" &&
                mixed.front().effects.glow_opacity == 0,
            "shadow patch keeps mixed font and glow styles");
        check(first(imported).fill.stops.size() == 3, "shadow patch preserves text gradient");

        command.text_style = {};
        command.text_style.outline = PresentationOutlineStyle{};
        command.text_style.shadow = PresentationShadowStyle{"#000000", 0, 0, 0, 0};
        command.text_style.glow = PresentationGlowStyle{"#000000", 0, 0};
        command.text_style.reflection = PresentationReflectionStyle{0, 0};
        command.text_style.warp = "";
        check(apply_presentation_edit(imported, command).error == PresentationEditError::None,
            "disable effects without clearing text");
        imported = reopen(imported);
        check(first(imported).effects.shadow_opacity == 0 && first(imported).effects.glow_opacity == 0 &&
                first(imported).effects.reflection_opacity == 0 &&
                first(imported).effects.outline_color.empty(),
            "disabled effects do not reappear through inheritance");

        for (int invalid = 0; invalid < 6; ++invalid)
        {
            command.text_style = {};
            if (invalid == 0)
                command.text_style.rotation = std::numeric_limits<double>::quiet_NaN();
            if (invalid == 1)
                command.text_style.warp = "not-a-preset";
            if (invalid == 2)
                command.text_style.reflection = PresentationReflectionStyle{0.5, 0, 0, 0.8, 0.2};
            if (invalid == 3)
                command.text_style.fill = PresentationFill{"not-a-color"};
            if (invalid == 4)
                command.text_style.outline = PresentationOutlineStyle{PresentationFill{"#000000"}, 100};
            if (invalid == 5)
                command.text_style.shadow = PresentationShadowStyle{"#000000", 0.5, 5, 201, 0};
            const auto before = serialize_presentation(imported);
            check(apply_presentation_edit(imported, command).error == PresentationEditError::InvalidValue,
                "invalid style rejected atomically");
            const auto after = serialize_presentation(imported);
            check(before.parts.size() == after.parts.size() &&
                    std::equal(before.parts.begin(), before.parts.end(), after.parts.begin(),
                        [](const auto& a, const auto& b)
            {
                return a.path == b.path && a.bytes == b.bytes;
            }),
                "rejected edit leaves all parts intact");
        }
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_tests();
}
