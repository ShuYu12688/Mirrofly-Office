#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;

    void check(bool value, const std::string& label)
    {
        if (!value)
        {
            std::cerr << label << '\n';
            ++failures;
        }
    }

    std::map<std::string, std::string> parts(const PresentationScene& scene)
    {
        const auto package = serialize_presentation(scene);
        check(package.error == PresentationError::None, "preserved package serializes: " + package.message);
        std::map<std::string, std::string> result;
        for (const auto& part : package.parts)
            result.emplace(part.path, part.bytes);
        return result;
    }

    PresentationScene reopen(const PresentationScene& scene)
    {
        const auto package = serialize_presentation(scene);
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None, "preserved package reopens: " + parsed.message);
        parsed.scene.native_editable = true;
        return parsed.scene;
    }

    bool edit(PresentationScene& scene, PresentationEditCommand command)
    {
        const auto result = apply_presentation_edit(scene, command);
        check(result.error == PresentationEditError::None, "edit succeeds: " + result.message);
        return result.error == PresentationEditError::None;
    }

    PresentationScene fixture();

    void test_model_only_edit_stages_package()
    {
        auto scene = fixture();
        const auto package_before = parts(scene);
        const auto text_before = scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text;
        PresentationEditCommand command;
        command.action = PresentationEditAction::UpdateText;
        command.text = "Optimistic model";
        const auto result = apply_presentation_model_edit(scene, command);
        check(result.error == PresentationEditError::None &&
                scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text == "Optimistic model" &&
                scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text != text_before,
            "model-only edit updates the visible scene");
        check(parts(scene) == package_before,
            "model-only edit leaves preserved package bytes for a later ordered commit");
    }

    PresentationScene fixture()
    {
        auto native = make_presentation(PresentationSlideLayout::Title);
        auto package = serialize_presentation(native);
        for (auto& part : package.parts)
        {
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto root = document.document_element();
                root.append_attribute("xmlns:p14") =
                    "http://schemas.microsoft.com/office/powerpoint/2010/main";
                auto shape = root.child("p:cSld").child("p:spTree").child("p:sp");
                shape.child("p:nvSpPr")
                    .child("p:nvPr")
                    .append_child("a:videoFile")
                    .append_attribute("r:link") = "clip";
                auto effects = shape.child("p:spPr").append_child("a:effectLst");
                effects.append_child("a:outerShdw").append_attribute("blurRad") = "25400";
                effects.append_child("a:softEdge").append_attribute("rad") = "38100";
                shape.child("p:txBody")
                    .child("a:bodyPr")
                    .append_child("a:prstTxWarp")
                    .append_attribute("prst") = "textWave1";
                auto run = shape.child("p:txBody").child("a:p").child("a:r").child("a:rPr");
                run.append_child("a:effectLst").append_child("a:glow").append_attribute("rad") = "10000";
                root.append_child("p:timing")
                    .append_child("p:tnLst")
                    .append_child("p:par")
                    .append_child("p:cTn")
                    .append_attribute("id") = "71";
                root.append_child("p:transition").append_child("p:fade");
                root.append_child("p:extLst").append_child("p:ext").append_attribute("uri") = "unknown-data";
                auto unknown = root.child("p:cSld").child("p:spTree").append_child("p:graphicFrame");
                unknown.append_child("p:nvGraphicFramePr").append_child("p:cNvPr").append_attribute("id") =
                    "91";
                std::ostringstream stream;
                document.save(stream);
                part.bytes = stream.str();
            }
            if (part.path == "ppt/slides/_rels/slide1.xml.rels")
                part.bytes.insert(part.bytes.find("</Relationships>"),
                    "<Relationship Id='clip' "
                    "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/video' "
                    "Target='../media/clip.mp4'/>"
                    "<Relationship Id='note' "
                    "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesSlide' "
                    "Target='../notesSlides/notesSlide1.xml'/>");
        }
        package.parts.push_back({"ppt/media/clip.mp4", std::string("opaque\0video", 12)});
        package.parts.push_back({"custom/untouched.xml", "<unknown><value>keep exactly</value></unknown>"});
        package.parts.push_back({"ppt/notesSlides/notesSlide1.xml",
            "<p:notes "
            "xmlns:p='http://schemas.openxmlformats.org/presentationml/2006/main' "
            "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'>"
            "<p:cSld><p:spTree>"
            "<p:sp><p:nvSpPr><p:nvPr><p:ph type='sldNum'/></p:nvPr></p:nvSpPr>"
            "<p:txBody><a:p><a:r><a:t>42</a:t></a:r></a:p></p:txBody></p:sp>"
            "<p:sp><p:nvSpPr><p:nvPr><p:ph type='body'/></p:nvPr></p:nvSpPr>"
            "<p:txBody><a:p><a:r><a:t>Speak slowly</a:t></a:r></a:p>"
            "<a:p><a:r><a:t>Show the chart</a:t></a:r></a:p></p:txBody></p:sp>"
            "</p:spTree></p:cSld></p:notes>"});
        package.parts.push_back({"ppt/notesSlides/_rels/notesSlide1.xml.rels",
            "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
            "<Relationship Id='back' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide' "
            "Target='../slides/slide1.xml'/></Relationships>"});
        auto imported = parse_presentation(package.parts);
        check(imported.error == PresentationError::None, "fixture parsed");
        imported.scene.native_editable = true;
        const auto copied = parts(imported.scene);
        for (const auto& part : package.parts)
            check(copied.at(part.path) == part.bytes, "unmodified copy preserves part bytes: " + part.path);
        return imported.scene;
    }

    void test_speaker_notes()
    {
        auto scene = fixture();
        check(scene.slides[0].speaker_notes == "Speak slowly\nShow the chart",
            "notes body excludes the slide number placeholder");
        scene = reopen(scene);
        check(scene.slides[0].speaker_notes == "Speak slowly\nShow the chart",
            "speaker notes survive a saved package readback");
    }

    void test_image_fills()
    {
        auto package = serialize_presentation(make_presentation(PresentationSlideLayout::Title));
        const std::string fill =
            "<a:blipFill dpi='72'><a:blip r:embed='fillImage'><a:alphaModFix amt='50000'/></a:blip>"
            "<a:srcRect l='25000'/><a:tile sx='200000' sy='50000' tx='12700' ty='25400' algn='br' flip='xy'/>"
            "</a:blipFill>";
        for (auto& part : package.parts)
        {
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_string(part.bytes.c_str());
                const auto common = document.child("p:sld").child("p:cSld");
                auto background = common.child("p:bg").child("p:bgPr");
                background.remove_children();
                background.append_buffer(fill.data(), fill.size());
                auto properties = common.child("p:spTree").child("p:sp").child("p:spPr");
                properties.remove_child("a:noFill");
                properties.remove_child("a:solidFill");
                properties.append_buffer(fill.data(), fill.size());
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
            else if (part.path == "ppt/slides/_rels/slide1.xml.rels")
                part.bytes.insert(part.bytes.find("</Relationships>"),
                    "<Relationship Id='fillImage' "
                    "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/image' "
                    "Target='../media/fill.png'/>");
        }
        package.parts.push_back({"ppt/media/fill.png", "fixture-image"});
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None && parsed.scene.images.size() == 1,
            "fill image relationship registers one shared resource");
        if (parsed.error != PresentationError::None)
            return;
        const auto& background = parsed.scene.slides[0].background;
        check(background.image_path == "ppt/media/fill.png" && background.image_crop[0] == 0.25 &&
                background.image_tile && background.image_scale[0] == 2 && background.image_scale[1] == 0.5 &&
                background.image_offset[0] == 1 && background.image_offset[1] == 2 &&
                background.image_alignment == "br" && background.image_flip == "xy" &&
                background.opacity == 0.5,
            "image fill reads crop tile physical offsets alignment and opacity");
        check(parsed.scene.slides[0].shapes[0].fill.image_path == background.image_path,
            "shape and background use the same image fill parser");
        parsed.scene.native_editable = true;
        const auto saved = parts(parsed.scene);
        for (const auto& part : package.parts)
            check(saved.at(part.path) == part.bytes, "image fill source part preserved");
        PresentationEditCommand command;
        command.action = PresentationEditAction::SetBackground;
        command.background_color = "#123456";
        edit(parsed.scene, command);
        check(parsed.scene.slides[0].background.image_path.empty() &&
                parsed.scene.slides[0].background.opacity == 1,
            "changing background to solid removes texture and opacity");
        command = {};
        command.action = PresentationEditAction::FormatShape;
        command.fill_color = "#654321";
        edit(parsed.scene, command);
        const auto reread = reopen(parsed.scene);
        check(reread.slides[0].shapes[0].fill.image_path.empty() &&
                reread.slides[0].shapes[0].fill.color == "#654321" &&
                reread.slides[0].background.color == "#123456",
            "image fill replacement persists without changing unrelated package media");
    }

    void test_theme_inheritance_edit()
    {
        auto package = serialize_presentation(make_presentation(PresentationSlideLayout::Title));
        package.parts.push_back({"custom/untouched.xml", "<unknown>keep</unknown>"});
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document document;
            document.load_buffer(part.bytes.data(), part.bytes.size());
            auto shape = document.child("p:sld").child("p:cSld").child("p:spTree").child("p:sp");
            auto properties = shape.child("p:spPr");
            properties.remove_child("a:noFill");
            properties.remove_child("a:solidFill");
            properties.append_child("a:solidFill").append_child("a:schemeClr").append_attribute("val") =
                "accent1";
            auto run = shape.child("p:txBody").child("a:p").child("a:r").child("a:rPr");
            if (!run)
                run = shape.child("p:txBody").child("a:p").child("a:r").prepend_child("a:rPr");
            run.remove_child("a:latin");
            run.append_child("a:latin").append_attribute("typeface") = "+mj-lt";
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto imported = parse_presentation(package.parts);
        check(imported.error == PresentationError::None, "theme edit fixture parses");
        if (imported.error != PresentationError::None)
            return;
        auto scene = std::move(imported.scene);
        scene.native_editable = true;
        const auto original = parts(scene);
        PresentationEditCommand invalid;
        invalid.action = PresentationEditAction::ApplyTheme;
        invalid.theme_colors = {{"wrongSlot", "#123456"}};
        const auto rejected = apply_presentation_edit(scene, invalid);
        check(rejected.error == PresentationEditError::InvalidValue && parts(scene) == original,
            "invalid theme slot rejects atomically");
        PresentationEditCommand command;
        command.action = PresentationEditAction::ApplyTheme;
        command.theme_colors = {{"accent1", "#123456"}};
        command.theme_fonts = {{"majorLatin", "Georgia"}};
        check(apply_presentation_model_edit(scene, command).error == PresentationEditError::None &&
                parts(scene) == original,
            "theme model stage leaves package unchanged while synchronization runs");
        if (!edit(scene, command))
            return;
        check(scene.slides[0].shapes[0].fill.color == "#123456" &&
                scene.slides[0].shapes[0].text.paragraphs[0].runs[0].font_family == "Georgia",
            "theme linked fill and font update together");
        const auto saved = parts(scene);
        check(saved.at("ppt/slides/slide1.xml") == original.at("ppt/slides/slide1.xml") &&
                saved.at("custom/untouched.xml") == original.at("custom/untouched.xml"),
            "theme edit preserves linked slide and unrelated part bytes");
        const auto reopened = reopen(scene);
        check(reopened.slides[0].shapes[0].fill.color == "#123456" &&
                reopened.slides[0].shapes[0].text.paragraphs[0].runs[0].font_family == "Georgia",
            "theme linked styles survive save and reopen");
    }

    void test_theme_effect_reference()
    {
        auto package = serialize_presentation(make_presentation(PresentationSlideLayout::Title));
        package.parts.push_back({"custom/untouched.xml", "<unknown>keep</unknown>"});
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/theme/theme1.xml" && part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document document;
            document.load_buffer(part.bytes.data(), part.bytes.size());
            if (part.path == "ppt/theme/theme1.xml")
            {
                auto effects = document.child("a:theme")
                                   .child("a:themeElements")
                                   .child("a:fmtScheme")
                                   .child("a:effectStyleLst")
                                   .child("a:effectStyle")
                                   .child("a:effectLst");
                auto shadow = effects.append_child("a:outerShdw");
                shadow.append_attribute("blurRad") = 127000;
                shadow.append_attribute("dist") = 63500;
                shadow.append_attribute("dir") = 0;
                shadow.append_child("a:schemeClr").append_attribute("val") = "phClr";
            }
            else
            {
                auto tree = document.child("p:sld").child("p:cSld").child("p:spTree");
                auto first = tree.child("p:sp");
                auto second = first.next_sibling("p:sp");
                auto style = first.child("p:style");
                if (!style)
                    style = first.append_child("p:style");
                style.append_child("a:effectRef").append_attribute("idx") = 1;
                style.child("a:effectRef").append_child("a:schemeClr").append_attribute("val") = "accent1";
                first.child("p:spPr").remove_child("a:effectLst");
                auto second_properties = second.child("p:spPr");
                second_properties.remove_child("a:effectLst");
                auto direct = second_properties.append_child("a:effectLst").append_child("a:outerShdw");
                direct.append_attribute("blurRad") = 127000;
                direct.append_child("a:srgbClr").append_attribute("val") = "AA5500";
                auto second_style = second.child("p:style");
                if (!second_style)
                    second_style = second.append_child("p:style");
                second_style.append_child("a:effectRef").append_attribute("idx") = 1;
                second_style.child("a:effectRef").append_child("a:schemeClr").append_attribute("val") =
                    "accent1";
            }
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto imported = parse_presentation(package.parts);
        check(imported.error == PresentationError::None, "theme effect reference fixture parses");
        if (imported.error != PresentationError::None)
            return;
        auto scene = std::move(imported.scene);
        scene.native_editable = true;
        check(scene.slides[0].shapes[0].effects_source == "theme" &&
                scene.slides[0].shapes[0].theme_effect_style_index == 1 &&
                scene.slides[0].shapes[1].effects_source == "direct" &&
                scene.slides[0].shapes[0].effects.shadow_opacity > 0 &&
                scene.slides[0].shapes[0].effects.shadow_blur == 10 &&
                scene.slides[0].shapes[1].effects.shadow_color == "#AA5500",
            "theme effect reference is displayed and direct effect takes precedence");
        auto disabled = scene;
        PresentationEditCommand disable_shadow;
        disable_shadow.action = PresentationEditAction::FormatShape;
        disable_shadow.shape_index = 0;
        disable_shadow.shadow_enabled = false;
        check(edit(disabled, disable_shadow) && disabled.slides[0].shapes[0].effects.shadow_opacity == 0 &&
                disabled.slides[0].shapes[0].effects_source == "direct",
            "turning off a theme shadow creates a direct empty effect override");
        PresentationEditCommand changed_theme;
        changed_theme.action = PresentationEditAction::ApplyTheme;
        changed_theme.theme_colors = {{"accent1", "#123456"}};
        check(edit(disabled, changed_theme) && disabled.slides[0].shapes[0].effects.shadow_opacity == 0 &&
                reopen(disabled).slides[0].shapes[0].effects.shadow_opacity == 0,
            "disabled theme shadow stays off after theme edit and reopen");
        const auto original = parts(scene);
        PresentationEditCommand command;
        command.action = PresentationEditAction::ApplyTheme;
        command.theme_colors = {{"accent1", "#123456"}};
        if (!edit(scene, command))
            return;
        check(scene.slides[0].shapes[0].effects.shadow_color == "#123456" &&
                scene.slides[0].shapes[0].effects.shadow_blur == 10 &&
                scene.slides[0].shapes[1].effects.shadow_color == "#AA5500" &&
                scene.slides[0].shapes[0].effects_source == "theme",
            "theme shadow follows theme color while direct shadow stays fixed");
        const auto saved = parts(scene);
        check(saved.at("ppt/slides/slide1.xml") == original.at("ppt/slides/slide1.xml") &&
                saved.at("custom/untouched.xml") == original.at("custom/untouched.xml") &&
                reopen(scene).slides[0].shapes[0].effects.shadow_color == "#123456",
            "theme effect linkage survives save and reopen without changing slide parts");
    }

    void test_placeholder_style_inheritance()
    {
        auto package = serialize_presentation(make_presentation(PresentationSlideLayout::Blank));
        package.parts.push_back({"custom/untouched.xml", "<unknown>keep</unknown>"});
        const auto append_placeholder = [](pugi::xml_node tree, bool local, bool master)
        {
            auto shape = tree.append_child("p:sp");
            auto non_visual = shape.append_child("p:nvSpPr");
            auto properties = non_visual.append_child("p:cNvPr");
            properties.append_attribute("id") = master ? 20 : 2;
            properties.append_attribute("name") = "Body placeholder";
            non_visual.append_child("p:cNvSpPr");
            auto ph = non_visual.append_child("p:nvPr").append_child("p:ph");
            ph.append_attribute("type") = "body";
            ph.append_attribute("idx") = 1;
            auto style = shape.append_child("p:spPr");
            if (!local && !master)
            {
                auto transform = style.append_child("a:xfrm");
                auto offset = transform.append_child("a:off");
                offset.append_attribute("x") = 127000;
                offset.append_attribute("y") = 127000;
                auto extent = transform.append_child("a:ext");
                extent.append_attribute("cx") = 2540000;
                extent.append_attribute("cy") = 1270000;
            }
            if (!master)
                style.append_child("a:solidFill")
                    .append_child(local ? "a:srgbClr" : "a:schemeClr")
                    .append_attribute("val") = local ? "ABCDEF" : "accent1";
            if (local || master)
            {
                auto line = style.append_child("a:ln");
                line.append_attribute("w") = local ? 25400 : 12700;
                line.append_child("a:solidFill")
                    .append_child(local ? "a:srgbClr" : "a:schemeClr")
                    .append_attribute("val") = local ? "FEDCBA" : "accent2";
            }
        };
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/slideLayouts/slideLayout1.xml" && part.path != "ppt/slides/slide1.xml" &&
                part.path != "ppt/slideMasters/slideMaster1.xml")
                continue;
            pugi::xml_document document;
            check(static_cast<bool>(document.load_buffer(part.bytes.data(), part.bytes.size())),
                "placeholder fixture XML parses");
            const bool local = part.path == "ppt/slides/slide1.xml";
            const bool master = part.path == "ppt/slideMasters/slideMaster1.xml";
            const auto tree = document.document_element().child("p:cSld").child("p:spTree");
            append_placeholder(tree, local, master);
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto imported = parse_presentation(package.parts);
        check(imported.error == PresentationError::None, "placeholder inheritance fixture parses");
        if (imported.error != PresentationError::None)
            return;
        auto scene = std::move(imported.scene);
        scene.native_editable = true;
        check(scene.slides[0].shapes.size() == 1 && scene.slides[0].shapes[0].placeholder &&
                scene.slides[0].shapes[0].placeholder->local_fill_override &&
                scene.slides[0].shapes[0].placeholder->inherited_fill_source == "layout" &&
                scene.slides[0].shapes[0].fill.color == "#ABCDEF" &&
                scene.slides[0].shapes[0].placeholder->local_outline_override &&
                scene.slides[0].shapes[0].placeholder->inherited_outline_source == "master" &&
                scene.slides[0].shapes[0].outline_color == "#FEDCBA",
            "local placeholder fill and outline are distinguished from layout inheritance");
        if (scene.slides[0].shapes.size() != 1 || !scene.slides[0].shapes[0].placeholder)
            return;
        const auto before = parts(scene);
        PresentationEditCommand reset;
        reset.action = PresentationEditAction::ResetPlaceholderFill;
        reset.shape_index = 0;
        auto staged = scene;
        check(apply_presentation_model_edit(staged, reset).error == PresentationEditError::None &&
                staged.slides[0].shapes[0].fill.color == "#B77746" && parts(staged) == before,
            "model stage restores inherited pixels without changing package bytes");
        if (!edit(scene, reset))
            return;
        const auto restored = parts(scene);
        check(scene.slides[0].shapes[0].fill.color == "#B77746" &&
                !scene.slides[0].shapes[0].placeholder->local_fill_override &&
                scene.slides[0].shapes[0].outline_color == "#FEDCBA" &&
                scene.slides[0].shapes[0].placeholder->local_outline_override &&
                restored.at("ppt/slideLayouts/slideLayout1.xml") ==
                    before.at("ppt/slideLayouts/slideLayout1.xml") &&
                restored.at("custom/untouched.xml") == before.at("custom/untouched.xml") &&
                restored.at("ppt/slides/slide1.xml").find("ABCDEF") == std::string::npos,
            "restoring fill changes only the slide override and preserves layout and unknown parts");
        auto saved = reopen(scene);
        check(saved.slides[0].shapes[0].fill.color == "#B77746" &&
                saved.slides[0].shapes[0].placeholder->fill_source == "layout",
            "placeholder fill inheritance survives save and reopen");
        PresentationEditCommand color;
        color.action = PresentationEditAction::FormatShape;
        color.fill_color = "#654321";
        check(edit(scene, color) && scene.slides[0].shapes[0].placeholder->local_fill_override,
            "editing placeholder fill creates an explicit local override");
        PresentationEditCommand theme;
        theme.action = PresentationEditAction::ApplyTheme;
        theme.theme_colors = {{"accent1", "#224466"}, {"accent2", "#335577"}};
        if (!edit(scene, theme))
            return;
        check(scene.slides[0].shapes[0].fill.color == "#654321",
            "theme change preserves the explicit placeholder fill override");
        check(edit(scene, reset) && scene.slides[0].shapes[0].fill.color == "#224466" &&
                reopen(scene).slides[0].shapes[0].fill.color == "#224466",
            "reset resumes live theme-linked layout inheritance");
        PresentationEditCommand reset_outline;
        reset_outline.action = PresentationEditAction::ResetPlaceholderOutline;
        reset_outline.shape_index = 0;
        auto staged_outline = scene;
        check(apply_presentation_model_edit(staged_outline, reset_outline).error ==
                    PresentationEditError::None &&
                staged_outline.slides[0].shapes[0].outline_color == "#335577" &&
                parts(staged_outline) == parts(scene),
            "outline model stage restores inherited pixels without changing package bytes");
        const auto before_outline = parts(scene);
        if (!edit(scene, reset_outline))
            return;
        check(scene.slides[0].shapes[0].outline_color == "#335577" &&
                !scene.slides[0].shapes[0].placeholder->local_outline_override &&
                scene.slides[0].shapes[0].placeholder->outline_source == "master" &&
                scene.slides[0].shapes[0].fill.color == "#224466" &&
                parts(scene).at("ppt/slideLayouts/slideLayout1.xml") ==
                    before_outline.at("ppt/slideLayouts/slideLayout1.xml") &&
                parts(scene).at("ppt/slideMasters/slideMaster1.xml") ==
                    before_outline.at("ppt/slideMasters/slideMaster1.xml") &&
                parts(scene).at("custom/untouched.xml") == before_outline.at("custom/untouched.xml") &&
                reopen(scene).slides[0].shapes[0].outline_color == "#335577",
            "outline reset removes only the slide override and survives save and reopen");
        PresentationEditCommand outline;
        outline.action = PresentationEditAction::FormatShape;
        outline.shape_index = 0;
        outline.outline_color = "#445566";
        check(edit(scene, outline) && scene.slides[0].shapes[0].placeholder->local_outline_override,
            "editing placeholder outline creates an explicit local override");
        theme.theme_colors = {{"accent2", "#446688"}};
        if (!edit(scene, theme))
            return;
        check(scene.slides[0].shapes[0].outline_color == "#445566",
            "theme change preserves the explicit placeholder outline override");
        check(edit(scene, reset_outline) && scene.slides[0].shapes[0].outline_color == "#446688" &&
                reopen(scene).slides[0].shapes[0].outline_color == "#446688",
            "outline reset resumes live theme-linked master inheritance");
    }

    void test_text_style_source_layers()
    {
        auto package = serialize_presentation(make_presentation(PresentationSlideLayout::Blank));
        for (auto& part : package.parts)
        {
            const bool slide = part.path == "ppt/slides/slide1.xml";
            const bool layout = part.path == "ppt/slideLayouts/slideLayout1.xml";
            const bool master = part.path == "ppt/slideMasters/slideMaster1.xml";
            if (!slide && !layout && !master)
                continue;
            pugi::xml_document document;
            check(static_cast<bool>(document.load_buffer(part.bytes.data(), part.bytes.size())),
                "text style source fixture XML parses");
            auto shape = document.document_element().child("p:cSld").child("p:spTree").append_child("p:sp");
            auto non_visual = shape.append_child("p:nvSpPr");
            non_visual.append_child("p:cNvPr").append_attribute("id") = slide ? 80 : 81;
            non_visual.append_child("p:cNvSpPr");
            auto ph = non_visual.append_child("p:nvPr").append_child("p:ph");
            ph.append_attribute("type") = "body";
            ph.append_attribute("idx") = 7;
            auto shape_properties = shape.append_child("p:spPr");
            if (layout)
            {
                auto transform = shape_properties.append_child("a:xfrm");
                transform.append_child("a:off").append_attribute("x") = 127000;
                transform.child("a:off").append_attribute("y") = 127000;
                transform.append_child("a:ext").append_attribute("cx") = 2540000;
                transform.child("a:ext").append_attribute("cy") = 1270000;
            }
            auto body = shape.append_child("p:txBody");
            body.append_child("a:bodyPr");
            if (!slide)
            {
                auto properties =
                    body.append_child("a:lstStyle").append_child("a:lvl1pPr").append_child("a:defRPr");
                properties.append_attribute("sz") = master ? 1800 : 2800;
                if (master)
                    properties.append_child("a:latin").append_attribute("typeface") = "+mj-lt";
                properties.append_child("a:solidFill").append_child("a:srgbClr").append_attribute("val") =
                    master ? "223344" : "345678";
            }
            auto paragraph = body.append_child("a:p");
            if (slide)
            {
                auto run = paragraph.append_child("a:r");
                run.append_child("a:rPr").append_child("a:latin").append_attribute("typeface") = "Arial";
                run.append_child("a:t").text().set("Source");
            }
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto imported = parse_presentation(package.parts);
        check(imported.error == PresentationError::None, "layered text style fixture parses");
        if (imported.error != PresentationError::None || imported.scene.slides[0].shapes.empty())
            return;
        auto scene = std::move(imported.scene);
        scene.native_editable = true;
        const auto& run = scene.slides[0].shapes[0].text.paragraphs[0].runs[0];
        check(run.font_family == "Arial" && run.font_family_source == "slide" && run.font_size == 28 &&
                run.font_size_source == "layout" && run.color == "#345678" && run.color_source == "layout",
            "text style sources follow per-property slide and layout precedence");
        PresentationEditCommand format;
        format.action = PresentationEditAction::FormatText;
        format.shape_index = 0;
        format.font_size = 34;
        auto staged = scene;
        check(apply_presentation_model_edit(staged, format).error == PresentationEditError::None &&
                staged.slides[0].shapes[0].text.paragraphs[0].runs[0].font_size_source == "slide",
            "pending text edits report the local style source");
        if (!edit(scene, format))
            return;
        const auto reopened = reopen(scene);
        const auto& saved_run = reopened.slides[0].shapes[0].text.paragraphs[0].runs[0];
        check(saved_run.font_size == 34 && saved_run.font_size_source == "slide" &&
                saved_run.color_source == "layout",
            "local text size override survives save and reopen without changing color inheritance");
        PresentationEditCommand reset;
        reset.action = PresentationEditAction::ResetTextInheritance;
        reset.shape_index = 0;
        reset.reset_text_property = PresentationTextProperty::FontFamily;
        const auto before_reset = parts(scene);
        check(edit(scene, reset), "font family direct override can be reset");
        if (scene.slides[0].shapes.empty())
            return;
        const auto& inherited_font = scene.slides[0].shapes[0].text.paragraphs[0].runs[0];
        check(inherited_font.font_family_source == "master" && !inherited_font.local_font_override &&
                inherited_font.font_size == 34 && inherited_font.color == "#345678" &&
                parts(scene).at("ppt/slideLayouts/slideLayout1.xml") ==
                    before_reset.at("ppt/slideLayouts/slideLayout1.xml") &&
                parts(scene).at("ppt/slideMasters/slideMaster1.xml") ==
                    before_reset.at("ppt/slideMasters/slideMaster1.xml"),
            "font reset resumes master theme reference without changing size, color, or inherited parts");
        reset.reset_text_property = PresentationTextProperty::FontSize;
        check(edit(scene, reset) &&
                reopen(scene).slides[0].shapes[0].text.paragraphs[0].runs[0].font_size_source == "layout" &&
                scene.slides[0].shapes[0].text.paragraphs[0].runs[0].font_size == 28,
            "size reset resumes layout inheritance after save and reopen");
        format.font_size.reset();
        format.text_color = "#AABBCC";
        if (!edit(scene, format))
            return;
        reset.reset_text_property = PresentationTextProperty::Color;
        check(edit(scene, reset) &&
                reopen(scene).slides[0].shapes[0].text.paragraphs[0].runs[0].color == "#345678" &&
                scene.slides[0].shapes[0].text.paragraphs[0].runs[0].color_source == "layout",
            "color reset resumes layout inheritance without altering other text properties");
        const auto stable = parts(scene);
        check(apply_presentation_edit(scene, reset).error == PresentationEditError::ReadOnly &&
                parts(scene) == stable,
            "reset refuses a property without a direct local override and preserves package bytes");
    }

    void test_authored_text_inheritance_reset()
    {
        auto scene = make_presentation(PresentationSlideLayout::Title);
        PresentationEditCommand format;
        format.action = PresentationEditAction::FormatText;
        format.shape_index = 0;
        format.font_size = 36;
        if (!edit(scene, format))
            return;
        const auto actions = presentation_edit_capabilities(scene.slides[0].shapes[0]);
        check(std::find(actions.begin(), actions.end(), PresentationEditAction::ResetTextInheritance) !=
                actions.end(),
            "new presentation exposes inheritance reset after direct text formatting");
        PresentationEditCommand reset;
        reset.action = PresentationEditAction::ResetTextInheritance;
        reset.shape_index = 0;
        reset.reset_text_property = PresentationTextProperty::FontSize;
        check(edit(scene, reset) && scene.source_package &&
                !scene.slides[0].shapes[0].text.paragraphs[0].runs[0].local_size_override &&
                reopen(scene).slides[0].shapes[0].text.paragraphs[0].runs[0].font_size != 36,
            "new presentation becomes a linked package when resetting a direct size override");
    }

    void test_authored_theme_links()
    {
        auto scene = make_presentation(PresentationSlideLayout::Title);
        const auto authored_theme = presentation_theme_state(scene, 0);
        check(authored_theme.available && authored_theme.authored && authored_theme.linked_slide_count == 1 &&
                !presentation_theme_state(scene, 99).available,
            "authored theme preview is available before packaging and rejects invalid slide indices");
        PresentationEditCommand add;
        add.action = PresentationEditAction::AddSlide;
        add.slide_index = 1;
        add.layout = PresentationSlideLayout::ResearchStudio;
        add.template_palette = {"#243246", "#F7F8FA", "#FFFFFF", "#697484", "#426EA8", "#DFEAF5"};
        check(edit(scene, add), "authored theme fixture adds a template slide");
        add.slide_index = 2;
        check(edit(scene, add), "authored theme fixture adds a second matching slide");
        PresentationEditCommand override;
        override.action = PresentationEditAction::FormatShape;
        override.slide_index = 1;
        override.shape_index = 0;
        override.fill_color = "#777777";
        check(edit(scene, override), "authored theme fixture sets an explicit override");
        PresentationEditCommand theme;
        theme.action = PresentationEditAction::ApplyTheme;
        theme.slide_index = 1;
        theme.theme_colors = {{"accent1", "#123456"}};
        theme.theme_fonts = {{"majorLatin", "Georgia"}};
        if (!edit(scene, theme))
            return;
        check(scene.source_package && scene.slides[1].shapes[0].fill.color == "#777777" &&
                scene.slides[2].shapes[0].fill.color == "#123456" &&
                scene.slides[2].shapes[2].text.paragraphs[0].runs[0].font_family == "Georgia",
            "authored theme updates linked pages while preserving explicit overrides");
        const auto linked_theme = presentation_theme_state(scene, 1);
        check(linked_theme.available && !linked_theme.authored &&
                linked_theme.colors.at("accent1") == "#123456" &&
                linked_theme.fonts.at("majorLatin") == "Georgia" && linked_theme.linked_slide_count == 2 &&
                presentation_theme_state(scene, 0).linked_slide_count == 1,
            "parsed theme state reports effective colors, fonts and exact shared scope");
        const auto linked = parts(scene);
        check(linked.count("ppt/theme/theme1.xml") && linked.count("ppt/theme/theme2.xml") &&
                linked.at("ppt/slides/_rels/slide2.xml.rels").find("slideLayout2.xml") != std::string::npos &&
                linked.at("ppt/slides/_rels/slide3.xml.rels").find("slideLayout2.xml") != std::string::npos,
            "matching template palettes share one linked theme and distinct palettes stay separate");
        theme.theme_colors = {{"accent1", "#ABCDEF"}};
        theme.theme_fonts = {{"majorLatin", "Cambria"}};
        if (!edit(scene, theme))
            return;
        const auto saved = reopen(scene);
        check(saved.slides[1].shapes[0].fill.color == "#777777" &&
                saved.slides[2].shapes[0].fill.color == "#ABCDEF" &&
                saved.slides[2].shapes[2].text.paragraphs[0].runs[0].font_family == "Cambria",
            "authored theme linkage survives repeat edits and save/reopen");
        check(presentation_theme_state(saved, 2).colors.at("accent1") == "#ABCDEF" &&
                presentation_theme_state(saved, 2).linked_slide_count == 2,
            "theme preview survives save and reopen without reloading resources");
    }

    void test_theme_missing_slots()
    {
        auto package = serialize_presentation(make_presentation(PresentationSlideLayout::Title));
        check(package.error == PresentationError::None, "sparse theme fixture serializes");
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/theme/theme1.xml")
                continue;
            pugi::xml_document document;
            check(document.load_buffer(part.bytes.data(), part.bytes.size()), "theme fixture XML parses");
            auto elements = document.child("a:theme").child("a:themeElements");
            auto colors = elements.child("a:clrScheme");
            auto major_font = elements.child("a:fontScheme").child("a:majorFont");
            check(colors.remove_child("a:accent6") && major_font.remove_child("a:ea"),
                "theme fixture removes one color and one font slot");
            std::ostringstream stream;
            document.save(stream);
            part.bytes = stream.str();
        }
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None, "sparse theme fixture parses");
        if (parsed.error != PresentationError::None)
            return;
        auto& scene = parsed.scene;
        scene.native_editable = true;
        const auto state = presentation_theme_state(scene, 0);
        check(state.available && state.editable_color_slots.count("accent1") &&
                !state.editable_color_slots.count("accent6") &&
                state.editable_font_slots.count("majorLatin") &&
                !state.editable_font_slots.count("majorEastAsian"),
            "sparse imported theme exposes only existing XML slots");
        const auto stable = parts(scene);
        PresentationEditCommand command;
        command.action = PresentationEditAction::ApplyTheme;
        command.theme_colors = {{"accent6", "#123456"}};
        check(apply_presentation_edit(scene, command).error == PresentationEditError::ReadOnly &&
                parts(scene) == stable,
            "missing color slot rejects edit before package changes");
        command.theme_colors.clear();
        command.theme_fonts = {{"majorEastAsian", "Georgia"}};
        check(apply_presentation_edit(scene, command).error == PresentationEditError::ReadOnly &&
                parts(scene) == stable,
            "missing font slot rejects edit before package changes");
        command.theme_colors = {{"accent1", "#123456"}};
        command.theme_fonts = {{"majorLatin", "Georgia"}};
        if (!edit(scene, command))
            return;
        const auto saved = reopen(scene);
        const auto saved_state = presentation_theme_state(saved, 0);
        check(saved_state.colors.at("accent1") == "#123456" &&
                saved_state.fonts.at("majorLatin") == "Georgia" &&
                !saved_state.editable_color_slots.count("accent6") &&
                !saved_state.editable_font_slots.count("majorEastAsian"),
            "existing slots stay editable without synthesizing missing theme nodes");
    }

    void test_table_structure_edit()
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand insert;
        insert.action = PresentationEditAction::InsertTable;
        insert.table_rows = 3;
        insert.table_columns = 4;
        check(apply_presentation_edit(scene, insert).error == PresentationEditError::None,
            "native presentation inserts OOXML table");
        if (!scene.source_package)
            return;
        const auto cell_index = [&scene](std::size_t row, std::size_t column)
        {
            const auto& shapes = scene.slides[0].shapes;
            for (std::size_t index = 0; index < shapes.size(); ++index)
                if (shapes[index].table_cell && shapes[index].table_cell->row == row &&
                    shapes[index].table_cell->column == column)
                    return index;
            return shapes.size();
        };
        check(cell_index(2, 3) < scene.slides[0].shapes.size(), "inserted table has complete cell grid");
        PresentationEditCommand row;
        row.action = PresentationEditAction::InsertTableRow;
        row.shape_index = cell_index(0, 0);
        if (!edit(scene, row))
            return;
        check(cell_index(3, 3) < scene.slides[0].shapes.size(), "new table row survives reparse");
        PresentationEditCommand column;
        column.action = PresentationEditAction::InsertTableColumn;
        column.shape_index = cell_index(0, 0);
        if (!edit(scene, column))
            return;
        check(cell_index(3, 4) < scene.slides[0].shapes.size(), "new table column survives reparse");
        PresentationEditCommand update;
        update.action = PresentationEditAction::UpdateText;
        update.shape_index = cell_index(1, 0);
        update.text = "Deleted row cell";
        if (!edit(scene, update))
            return;
        PresentationEditCommand delete_row;
        delete_row.action = PresentationEditAction::DeleteTableRow;
        delete_row.shape_index = cell_index(1, 0);
        if (!edit(scene, delete_row))
            return;
        check(cell_index(3, 0) == scene.slides[0].shapes.size() &&
                scene.slides[0].shapes[cell_index(0, 0)].table_cell->row_count == 3 &&
                parts(scene).at("ppt/slides/slide1.xml").find("Deleted row cell") == std::string::npos,
            "delete selected table row shrinks the table and refreshes cell metadata");
        update.shape_index = cell_index(0, 1);
        update.text = "Deleted column cell";
        if (!edit(scene, update))
            return;
        PresentationEditCommand delete_column;
        delete_column.action = PresentationEditAction::DeleteTableColumn;
        delete_column.shape_index = cell_index(0, 1);
        if (!edit(scene, delete_column))
            return;
        check(cell_index(0, 4) == scene.slides[0].shapes.size() &&
                scene.slides[0].shapes[cell_index(0, 0)].table_cell->column_count == 4 &&
                parts(scene).at("ppt/slides/slide1.xml").find("Deleted column cell") == std::string::npos,
            "delete selected table column shrinks the grid and refreshes cell metadata");
        const auto reduced = reopen(scene);
        check(reduced.slides[0].shapes[0].table_cell &&
                reduced.slides[0].shapes[0].table_cell->row_count == 3 &&
                reduced.slides[0].shapes[0].table_cell->column_count == 4,
            "table row and column deletion survive save and reopen");
        PresentationEditCommand merge;
        merge.action = PresentationEditAction::MergeTableCell;
        merge.shape_index = cell_index(0, 0);
        merge.table_direction = "right";
        if (!edit(scene, merge))
            return;
        check(cell_index(0, 1) == scene.slides[0].shapes.size() &&
                scene.slides[0].shapes[cell_index(0, 0)].width > 100,
            "adjacent empty cells merge without losing table layout");
        PresentationEditCommand border;
        border.action = PresentationEditAction::FormatTableBorder;
        border.shape_index = cell_index(0, 0);
        border.outline_color = "#123456";
        border.outline_width = 2;
        if (!edit(scene, border))
            return;
        const auto reopened = reopen(scene);
        check(!reopened.slides.empty() &&
                std::any_of(reopened.slides[0].shapes.begin(), reopened.slides[0].shapes.end(),
                    [](const auto& shape)
        {
            return shape.geometry == "line" && shape.outline_color == "#123456" && shape.outline_width == 2;
        }),
            "table border survives save and reopen");
        const auto before = parts(scene);
        PresentationEditCommand invalid;
        invalid.action = PresentationEditAction::InsertTableColumn;
        invalid.shape_index = cell_index(0, 0);
        check(apply_presentation_edit(scene, invalid).error != PresentationEditError::None &&
                parts(scene) == before,
            "merged table rejects column insertion through its span atomically");
        delete_row.shape_index = cell_index(0, 0);
        check(apply_presentation_edit(scene, delete_row).error == PresentationEditError::ReadOnly &&
                parts(scene) == before,
            "merged table rejects row deletion before package mutation");

        check(scene.slides[0].shapes[cell_index(0, 0)].table_cell->unmergeable &&
                scene.slides[0].shapes[cell_index(0, 0)].table_cell->column_span == 2,
            "two-cell horizontal merge advertises safe unmerge");
        const auto merged_snapshot = scene;
        PresentationEditCommand unmerge;
        unmerge.action = PresentationEditAction::UnmergeTableCell;
        unmerge.shape_index = cell_index(0, 0);
        if (!edit(scene, unmerge))
            return;
        check(cell_index(0, 1) < scene.slides[0].shapes.size() &&
                !scene.slides[0].shapes[cell_index(0, 0)].table_cell->has_merges &&
                !scene.slides[0].shapes[cell_index(0, 0)].table_cell->unmergeable,
            "horizontal unmerge restores two independently editable cells");
        check(parts(merged_snapshot) == before &&
                reopen(scene).slides[0].shapes[cell_index(0, 1)].table_cell.has_value(),
            "unmerge preserves undo snapshot and survives save and reopen");
        const auto unmerged_parts = parts(scene);
        check(apply_presentation_edit(scene, unmerge).error == PresentationEditError::ReadOnly &&
                parts(scene) == unmerged_parts,
            "unmerge on a normal cell leaves the package unchanged");
        merge.shape_index = cell_index(0, 0);
        merge.table_direction = "down";
        if (!edit(scene, merge))
            return;
        check(scene.slides[0].shapes[cell_index(0, 0)].table_cell->row_span == 2 &&
                scene.slides[0].shapes[cell_index(0, 0)].table_cell->unmergeable &&
                cell_index(1, 0) == scene.slides[0].shapes.size(),
            "two-cell vertical merge advertises safe unmerge");
        unmerge.shape_index = cell_index(0, 0);
        if (!edit(scene, unmerge))
            return;
        check(cell_index(1, 0) < scene.slides[0].shapes.size() &&
                !reopen(scene).slides[0].shapes[cell_index(0, 0)].table_cell->has_merges,
            "vertical unmerge restores row cells after reload");
        row.shape_index = cell_index(0, 0);
        check(edit(scene, row) && scene.slides[0].shapes[cell_index(0, 0)].table_cell->row_count == 4,
            "row editing resumes after all merges are removed");

        auto unsafe = make_presentation(PresentationSlideLayout::Blank);
        insert.table_rows = 1;
        insert.table_columns = 2;
        if (!edit(unsafe, insert))
            return;
        merge.shape_index = 0;
        merge.table_direction = "right";
        if (!edit(unsafe, merge))
            return;
        auto unsafe_parts = serialize_presentation(unsafe).parts;
        for (auto& part : unsafe_parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document document;
            document.load_buffer(part.bytes.data(), part.bytes.size());
            auto continuation = document.document_element()
                                    .child("p:cSld")
                                    .child("p:spTree")
                                    .child("p:graphicFrame")
                                    .child("a:graphic")
                                    .child("a:graphicData")
                                    .child("a:tbl")
                                    .child("a:tr")
                                    .child("a:tc")
                                    .next_sibling("a:tc");
            continuation.child("a:txBody").child("a:p").prepend_child("a:r").append_child("a:t").text() =
                "Hidden imported text";
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto unsafe_read = parse_presentation(std::move(unsafe_parts));
        const bool unsafe_cell = unsafe_read.error == PresentationError::None &&
            !unsafe_read.scene.slides.empty() && !unsafe_read.scene.slides[0].shapes.empty() &&
            unsafe_read.scene.slides[0].shapes[0].table_cell.has_value();
        check(unsafe_cell && !unsafe_read.scene.slides[0].shapes[0].table_cell->unmergeable,
            "imported merge with hidden continuation text is not advertised as safe to split");
        if (unsafe_cell)
        {
            unsafe_read.scene.native_editable = true;
            const auto unchanged = parts(unsafe_read.scene);
            unmerge.shape_index = 0;
            check(apply_presentation_edit(unsafe_read.scene, unmerge).error ==
                        PresentationEditError::ReadOnly &&
                    parts(unsafe_read.scene) == unchanged,
                "unsafe imported continuation cannot be exposed by unmerge");
        }

        auto single = make_presentation(PresentationSlideLayout::Blank);
        insert.table_rows = 1;
        insert.table_columns = 1;
        if (!edit(single, insert))
            return;
        const auto single_parts = parts(single);
        delete_row.shape_index = 0;
        delete_column.shape_index = 0;
        check(apply_presentation_edit(single, delete_row).error == PresentationEditError::ReadOnly &&
                apply_presentation_edit(single, delete_column).error == PresentationEditError::ReadOnly &&
                parts(single) == single_parts,
            "last table row and column cannot be removed");

        insert.table_rows = 2;
        insert.table_columns = 2;
        auto malformed = make_presentation(PresentationSlideLayout::Blank);
        if (!edit(malformed, insert))
            return;
        auto malformed_parts = serialize_presentation(malformed).parts;
        for (auto& part : malformed_parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document document;
            document.load_buffer(part.bytes.data(), part.bytes.size());
            auto table = document.document_element()
                             .child("p:cSld")
                             .child("p:spTree")
                             .child("p:graphicFrame")
                             .child("a:graphic")
                             .child("a:graphicData")
                             .child("a:tbl");
            auto second_row = table.child("a:tr").next_sibling("a:tr");
            second_row.remove_child(second_row.last_child());
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto malformed_read = parse_presentation(std::move(malformed_parts));
        check(
            malformed_read.error == PresentationError::None && !malformed_read.scene.slides[0].shapes.empty(),
            "uneven table fixture remains readable");
        if (malformed_read.error == PresentationError::None && !malformed_read.scene.slides[0].shapes.empty())
        {
            malformed_read.scene.native_editable = true;
            const auto unchanged = parts(malformed_read.scene);
            delete_column.shape_index = 0;
            check(apply_presentation_edit(malformed_read.scene, delete_column).error !=
                        PresentationEditError::None &&
                    parts(malformed_read.scene) == unchanged,
                "uneven table rejects column deletion without mutating the package");
        }

        auto vertical = make_presentation(PresentationSlideLayout::Blank);
        insert.table_rows = 2;
        insert.table_columns = 2;
        if (apply_presentation_edit(vertical, insert).error != PresentationEditError::None)
        {
            check(false, "vertical merge fixture inserts table");
            return;
        }
        const auto initial_height = vertical.slides[0].shapes[0].height;
        merge.shape_index = 0;
        merge.table_direction = "down";
        check(apply_presentation_edit(vertical, merge).error == PresentationEditError::None &&
                vertical.slides[0].shapes[0].height > initial_height + 1,
            "vertical table merge preserves expanded cell geometry");
    }

    void test_rectangular_table_unmerge()
    {
        auto source = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand insert;
        insert.action = PresentationEditAction::InsertTable;
        insert.table_rows = 3;
        insert.table_columns = 4;
        if (!edit(source, insert))
            return;
        auto source_parts = serialize_presentation(source).parts;
        for (auto& part : source_parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document document;
            document.load_buffer(part.bytes.data(), part.bytes.size());
            auto table = document.document_element()
                             .child("p:cSld")
                             .child("p:spTree")
                             .child("p:graphicFrame")
                             .child("a:graphic")
                             .child("a:graphicData")
                             .child("a:tbl");
            auto row = table.child("a:tr");
            for (int dy = 0; dy < 2; ++dy, row = row.next_sibling("a:tr"))
            {
                auto cell = row.child("a:tc");
                for (int dx = 0; dx < 3; ++dx, cell = cell.next_sibling("a:tc"))
                {
                    if (dx == 0)
                        cell.append_attribute("gridSpan") = 3;
                    if (dy == 0)
                        cell.append_attribute("rowSpan") = 2;
                    if (dx > 0)
                        cell.append_attribute("hMerge") = "1";
                    if (dy > 0)
                        cell.append_attribute("vMerge") = "1";
                    if (dy == 1 && dx == 2)
                    {
                        auto properties = cell.child("a:tcPr");
                        properties.remove_child(properties.child("a:solidFill"));
                        properties.append_child("a:solidFill")
                            .append_child("a:srgbClr")
                            .append_attribute("val") = "45B26A";
                    }
                }
            }
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto parsed = parse_presentation(source_parts);
        const bool readable = parsed.error == PresentationError::None && !parsed.scene.slides.empty() &&
            !parsed.scene.slides[0].shapes.empty() && parsed.scene.slides[0].shapes[0].table_cell.has_value();
        check(readable && parsed.scene.slides[0].shapes[0].table_cell->row_span == 2 &&
                parsed.scene.slides[0].shapes[0].table_cell->column_span == 3 &&
                parsed.scene.slides[0].shapes[0].table_cell->unmergeable,
            "Office-compatible rectangular merge exposes a safe split capability");
        if (!readable)
            return;
        parsed.scene.native_editable = true;
        const auto original = parts(parsed.scene);
        PresentationEditCommand unmerge;
        unmerge.action = PresentationEditAction::UnmergeTableCell;
        unmerge.shape_index = 0;
        if (!edit(parsed.scene, unmerge))
            return;
        const auto restored = reopen(parsed.scene);
        const auto find_cell = [&restored](std::size_t row, std::size_t column)
        {
            return std::find_if(restored.slides[0].shapes.begin(), restored.slides[0].shapes.end(),
                [row, column](const auto& shape)
            {
                return shape.table_cell && shape.table_cell->row == row && shape.table_cell->column == column;
            });
        };
        const auto last = find_cell(1, 2);
        check(last != restored.slides[0].shapes.end() && last->fill.color == "#45B26A" &&
                !restored.slides[0].shapes[0].table_cell->has_merges &&
                find_cell(0, 1) != restored.slides[0].shapes.end(),
            "rectangular split restores all cells and their original fill after save and reopen");
        const auto changed = parts(parsed.scene);
        for (const auto& [path, bytes] : original)
            if (path != "ppt/slides/slide1.xml")
                check(
                    changed.at(path) == bytes, "rectangular split keeps unrelated package parts byte exact");

        auto malformed_parts = source_parts;
        for (auto& part : malformed_parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document document;
            document.load_buffer(part.bytes.data(), part.bytes.size());
            auto table = document.document_element()
                             .child("p:cSld")
                             .child("p:spTree")
                             .child("p:graphicFrame")
                             .child("a:graphic")
                             .child("a:graphicData")
                             .child("a:tbl");
            auto interior = table.child("a:tr").next_sibling("a:tr").child("a:tc").next_sibling("a:tc");
            interior.remove_attribute("vMerge");
            std::ostringstream output;
            document.save(output);
            part.bytes = output.str();
        }
        auto malformed = parse_presentation(std::move(malformed_parts));
        const bool malformed_cell = malformed.error == PresentationError::None &&
            !malformed.scene.slides.empty() && !malformed.scene.slides[0].shapes.empty() &&
            malformed.scene.slides[0].shapes[0].table_cell.has_value();
        check(malformed_cell && !malformed.scene.slides[0].shapes[0].table_cell->unmergeable,
            "broken rectangular continuation does not expose split");
        if (malformed_cell)
        {
            malformed.scene.native_editable = true;
            const auto unchanged = parts(malformed.scene);
            check(
                apply_presentation_edit(malformed.scene, unmerge).error == PresentationEditError::ReadOnly &&
                    parts(malformed.scene) == unchanged,
                "broken merge is rejected without mutating the package");
        }
    }

    void test_group_roundtrip()
    {
        auto native = make_presentation(PresentationSlideLayout::Title);
        PresentationEditCommand group;
        group.action = PresentationEditAction::GroupAdjacent;
        group.shape_index = 0;
        group.target_index = 1;
        check(apply_presentation_edit(native, group).error == PresentationEditError::None &&
                native.source_package && !native.slides[0].shapes[0].source_groups.empty(),
            "new native presentation can group adjacent objects");
        auto scene = fixture();
        if (scene.slides.empty() || scene.slides[0].shapes.size() < 2)
            return;
        const auto before = scene.slides[0].shapes[0].transform;
        if (!edit(scene, group))
            return;
        check(!scene.slides[0].shapes[0].source_groups.empty(), "adjacent shapes become one editable group");
        const auto group_id = scene.slides[0].shapes[0].source_groups.back();
        PresentationEditCommand move;
        move.action = PresentationEditAction::MoveGroup;
        move.group_id = group_id;
        move.x = 12;
        if (!edit(scene, move))
            return;
        check(std::abs(scene.slides[0].shapes[0].transform[4] - before[4] - 12) < 0.001,
            "new group moves both objects");
        PresentationEditCommand ungroup;
        ungroup.action = PresentationEditAction::Ungroup;
        ungroup.group_id = group_id;
        if (!edit(scene, ungroup))
            return;
        check(scene.slides[0].shapes[0].source_groups.empty() &&
                std::abs(scene.slides[0].shapes[0].transform[4] - before[4] - 12) < 0.001,
            "ungroup preserves moved object position");
        const auto reopened = reopen(scene);
        check(reopened.slides[0].shapes[0].source_groups.empty() &&
                std::abs(reopened.slides[0].shapes[0].transform[4] - before[4] - 12) < 0.001,
            "group and ungroup survive save and reopen");

        auto scaled = native;
        auto scaled_package = serialize_presentation(scaled);
        for (auto& part : scaled_package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto group_node = document.child("p:sld").child("p:cSld").child("p:spTree").child("p:grpSp");
                auto width = group_node.child("p:grpSpPr").child("a:xfrm").child("a:ext").attribute("cx");
                width = width.as_llong() * 2;
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto scaled_parse = parse_presentation(scaled_package.parts);
        check(scaled_parse.error == PresentationError::None, "scaled group fixture parses");
        if (scaled_parse.error == PresentationError::None)
        {
            scaled_parse.scene.native_editable = true;
            const auto& frame = scaled_parse.scene.slides[0].groups[0];
            check(!frame.ungroupable, "scaled text remains locked to preserve its font size");
            ungroup.group_id = frame.source_id;
            check(apply_presentation_edit(scaled_parse.scene, ungroup).error != PresentationEditError::None,
                "scaled text group rejects visually unsafe ungroup");
        }

        auto pictures = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand image;
        image.action = PresentationEditAction::AddImage;
        image.image_mime_type = "image/png";
        image.image_bytes = "picture-one";
        image.image_path = "picture-one.png";
        image.x = 10;
        image.y = 20;
        image.width = 50;
        image.height = 40;
        check(edit(pictures, image), "first picture fixture inserts");
        image.image_bytes = "picture-two";
        image.image_path = "picture-two.png";
        image.x = 90;
        check(edit(pictures, image), "second picture fixture inserts");
        group.shape_index = 0;
        group.target_index = 1;
        check(edit(pictures, group), "picture fixture groups");
        auto picture_package = serialize_presentation(pictures);
        for (auto& part : picture_package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                const auto transform = document.child("p:sld")
                                           .child("p:cSld")
                                           .child("p:spTree")
                                           .child("p:grpSp")
                                           .child("p:grpSpPr")
                                           .child("a:xfrm");
                auto width = transform.child("a:ext").attribute("cx");
                auto height = transform.child("a:ext").attribute("cy");
                width = width.as_llong() * 2;
                height = height.as_llong() * 3;
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto picture_parse = parse_presentation(picture_package.parts);
        check(picture_parse.error == PresentationError::None, "scaled picture group parses");
        if (picture_parse.error != PresentationError::None)
            return;
        picture_parse.scene.native_editable = true;
        check(picture_parse.scene.slides[0].groups[0].ungroupable,
            "scaled picture group exposes ungroup capability");
        const auto picture_shapes = picture_parse.scene.slides[0].shapes;
        ungroup.group_id = picture_parse.scene.slides[0].groups[0].source_id;
        if (!edit(picture_parse.scene, ungroup))
            return;
        const auto saved = reopen(picture_parse.scene);
        const auto& result = saved.slides[0].shapes;
        check(result.size() == picture_shapes.size() && result[0].source_groups.empty(),
            "scaled pictures become independent after save and reload");
        for (std::size_t index = 0; index < result.size() && index < picture_shapes.size(); ++index)
        {
            const auto& before_picture = picture_shapes[index];
            const auto& after_picture = result[index];
            check(std::abs(after_picture.transform[4] - before_picture.transform[4]) < 0.001 &&
                    std::abs(after_picture.transform[5] - before_picture.transform[5]) < 0.001 &&
                    std::abs(after_picture.width * after_picture.transform[0] -
                        before_picture.width * before_picture.transform[0]) < 0.001 &&
                    std::abs(after_picture.height * after_picture.transform[3] -
                        before_picture.height * before_picture.transform[3]) < 0.001 &&
                    after_picture.image_path == before_picture.image_path,
                "scaled picture geometry and image relationship survive ungroup");
        }
        auto rotated_package = picture_package;
        for (auto& part : rotated_package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto transform = document.child("p:sld")
                                     .child("p:cSld")
                                     .child("p:spTree")
                                     .child("p:grpSp")
                                     .child("p:grpSpPr")
                                     .child("a:xfrm");
                transform.append_attribute("rot") = 5400000;
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto rotated = parse_presentation(rotated_package.parts);
        check(rotated.error == PresentationError::None && rotated.scene.slides[0].groups[0].ungroupable,
            "rotated scaled picture group exposes safe ungroup capability");
        if (rotated.error != PresentationError::None || rotated.scene.slides[0].groups.empty())
            return;
        rotated.scene.native_editable = true;
        const auto rotated_before = rotated.scene.slides[0].shapes;
        ungroup.group_id = rotated.scene.slides[0].groups[0].source_id;
        if (!edit(rotated.scene, ungroup))
            return;
        const auto rotated_after = reopen(rotated.scene).slides[0].shapes;
        const auto corners = [](const PresentationShape& shape)
        {
            std::array<double, 8> result{};
            std::size_t next = 0;
            for (const double x : {0.0, shape.width})
                for (const double y : {0.0, shape.height})
                {
                    result[next++] = shape.transform[0] * x + shape.transform[2] * y + shape.transform[4];
                    result[next++] = shape.transform[1] * x + shape.transform[3] * y + shape.transform[5];
                }
            return result;
        };
        check(rotated_after.size() == rotated_before.size(),
            "rotated picture group retains both independent children");
        for (std::size_t index = 0; index < rotated_after.size() && index < rotated_before.size(); ++index)
        {
            const auto before_corners = corners(rotated_before[index]);
            const auto after_corners = corners(rotated_after[index]);
            bool same_geometry = rotated_before[index].image_path == rotated_after[index].image_path &&
                rotated_after[index].source_groups.empty();
            for (std::size_t value = 0; value < before_corners.size(); ++value)
                same_geometry =
                    same_geometry && std::abs(before_corners[value] - after_corners[value]) < 0.001;
            check(same_geometry,
                "rotated picture visual corners and image relationship survive save and reopen");
        }
        auto flipped_package = rotated_package;
        for (auto& part : flipped_package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto transform = document.child("p:sld")
                                     .child("p:cSld")
                                     .child("p:spTree")
                                     .child("p:grpSp")
                                     .child("p:grpSpPr")
                                     .child("a:xfrm");
                transform.append_attribute("flipH") = 1;
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto flipped = parse_presentation(flipped_package.parts);
        check(flipped.error == PresentationError::None && !flipped.scene.slides[0].groups[0].ungroupable,
            "flipped group remains locked until its transform can be safely represented");
        if (flipped.error == PresentationError::None)
        {
            flipped.scene.native_editable = true;
            ungroup.group_id = flipped.scene.slides[0].groups[0].source_id;
            const auto unchanged = parts(flipped.scene);
            check(apply_presentation_edit(flipped.scene, ungroup).error != PresentationEditError::None &&
                    parts(flipped.scene) == unchanged,
                "rejected flipped group edit keeps original package bytes");
        }
        auto skewed_package = rotated_package;
        for (auto& part : skewed_package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto transform = document.child("p:sld")
                                     .child("p:cSld")
                                     .child("p:spTree")
                                     .child("p:grpSp")
                                     .child("p:pic")
                                     .child("p:spPr")
                                     .child("a:xfrm");
                transform.append_attribute("rot") = 1800000;
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto skewed = parse_presentation(skewed_package.parts);
        check(skewed.error == PresentationError::None && !skewed.scene.slides[0].groups[0].ungroupable,
            "nonuniformly scaled rotated child remains locked because flattening would shear it");
        auto malformed_package = picture_package;
        for (auto& part : malformed_package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto offset = document.child("p:sld")
                                  .child("p:cSld")
                                  .child("p:spTree")
                                  .child("p:grpSp")
                                  .child("p:pic")
                                  .child("p:spPr")
                                  .child("a:xfrm")
                                  .child("a:off");
                offset.attribute("x") = "invalid";
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto malformed = parse_presentation(malformed_package.parts);
        check(malformed.error != PresentationError::None,
            "malformed child coordinates are rejected before ungroup becomes available");
        auto rotated_text_package = scaled_package;
        for (auto& part : rotated_text_package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto transform = document.child("p:sld")
                                     .child("p:cSld")
                                     .child("p:spTree")
                                     .child("p:grpSp")
                                     .child("p:grpSpPr")
                                     .child("a:xfrm");
                transform.append_attribute("rot") = 5400000;
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto rotated_text = parse_presentation(rotated_text_package.parts);
        check(rotated_text.error == PresentationError::None &&
                !rotated_text.scene.slides[0].groups[0].ungroupable,
            "rotated text group stays locked because font and paragraph geometry cannot be flattened");
    }

    void test_group_extension()
    {
        const auto make_three = []()
        {
            auto scene = make_presentation(PresentationSlideLayout::Title);
            PresentationEditCommand add;
            add.action = PresentationEditAction::AddShape;
            add.geometry = "rect";
            add.x = 510;
            add.y = 220;
            add.width = 90;
            add.height = 65;
            check(edit(scene, add), "group extension fixture adds third shape");
            return scene;
        };
        for (const bool prepend : {false, true})
        {
            auto scene = make_three();
            const auto before = scene.slides[0].shapes;
            PresentationEditCommand group;
            group.action = PresentationEditAction::GroupAdjacent;
            group.shape_index = prepend ? 1 : 0;
            group.target_index = prepend ? 2 : 1;
            if (!edit(scene, group))
                continue;
            const auto group_id = scene.slides[0].shapes[group.shape_index].source_groups.back();
            const auto frame = std::find_if(scene.slides[0].groups.begin(), scene.slides[0].groups.end(),
                [&](const auto& item)
            {
                return item.source_id == group_id;
            });
            check(frame != scene.slides[0].groups.end() && frame->extensible,
                "plain authored group accepts an adjacent object");
            PresentationEditCommand append;
            append.action = PresentationEditAction::AddToGroup;
            append.group_id = group_id;
            append.shape_index = group.shape_index;
            append.target_index = prepend ? 0 : 2;
            const auto before_parts = parts(scene);
            if (!edit(scene, append))
                continue;
            const auto after_parts = parts(scene);
            const bool unrelated_untouched =
                std::all_of(before_parts.begin(), before_parts.end(), [&](const auto& part)
            {
                return part.first == "ppt/slides/slide1.xml" ||
                    (after_parts.count(part.first) && after_parts.at(part.first) == part.second);
            });
            check(scene.slides[0].shapes.size() == 3 &&
                    std::all_of(scene.slides[0].shapes.begin(), scene.slides[0].shapes.end(),
                        [&](const auto& shape)
            {
                return !shape.source_groups.empty() && shape.source_groups.back() == group_id;
            }) && unrelated_untouched,
                "third object joins the selected group without changing unrelated package parts");
            const auto reopened = reopen(scene);
            bool stable = reopened.slides[0].shapes.size() == 3;
            if (stable)
                for (std::size_t index = 0; index < before.size(); ++index)
                    for (std::size_t element = 0; element < before[index].transform.size(); ++element)
                        stable = stable &&
                            std::abs(reopened.slides[0].shapes[index].transform[element] -
                                before[index].transform[element]) < 0.001;
            check(stable, "three-member group preserves all transforms after save and reopen");
            PresentationEditCommand ungroup;
            ungroup.action = PresentationEditAction::Ungroup;
            ungroup.group_id = group_id;
            check(edit(scene, ungroup) &&
                    std::all_of(scene.slides[0].shapes.begin(), scene.slides[0].shapes.end(),
                        [](const auto& shape)
            {
                return shape.source_groups.empty();
            }),
                "extended group can be ungrouped without losing members");
        }
        auto scene = make_three();
        PresentationEditCommand fourth;
        fourth.action = PresentationEditAction::AddShape;
        fourth.geometry = "ellipse";
        check(edit(scene, fourth), "nonadjacent group fixture adds fourth shape");
        PresentationEditCommand group;
        group.action = PresentationEditAction::GroupAdjacent;
        group.shape_index = 0;
        group.target_index = 1;
        if (!edit(scene, group))
            return;
        PresentationEditCommand append;
        append.action = PresentationEditAction::AddToGroup;
        append.group_id = scene.slides[0].shapes[0].source_groups.back();
        append.shape_index = 0;
        append.target_index = 3;
        const auto stable = parts(scene);
        check(apply_presentation_edit(scene, append).error != PresentationEditError::None &&
                parts(scene) == stable,
            "nonadjacent object cannot enter group or reorder intervening layers");
        append.target_index = 999999;
        check(apply_presentation_edit(scene, append).error == PresentationEditError::InvalidIndex &&
                parts(scene) == stable,
            "out-of-range group target is rejected without changing package bytes");
        auto scaled_parts = serialize_presentation(scene).parts;
        for (auto& part : scaled_parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_buffer(part.bytes.data(), part.bytes.size());
                auto extent = document.child("p:sld")
                                  .child("p:cSld")
                                  .child("p:spTree")
                                  .child("p:grpSp")
                                  .child("p:grpSpPr")
                                  .child("a:xfrm")
                                  .child("a:ext")
                                  .attribute("cx");
                extent = extent.as_llong() * 2;
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        auto scaled = parse_presentation(scaled_parts);
        check(scaled.error == PresentationError::None && !scaled.scene.slides[0].groups[0].extensible,
            "scaled imported group does not offer extension");
        if (scaled.error == PresentationError::None)
        {
            scaled.scene.native_editable = true;
            append.target_index = 2;
            const auto original = parts(scaled.scene);
            check(apply_presentation_edit(scaled.scene, append).error == PresentationEditError::ReadOnly &&
                    parts(scaled.scene) == original,
                "scaled group rejects adjacent insertion without changing package bytes");
        }
    }

    void test_multilevel_list_edit()
    {
        auto source = make_presentation(PresentationSlideLayout::Title);
        auto& paragraphs = source.slides[0].shapes[0].text.paragraphs;
        paragraphs.front().runs.front().text = "Parent";
        paragraphs.front().numbered = true;
        paragraphs.front().margin_left = 24;
        paragraphs.push_back(paragraphs.front());
        paragraphs.back().runs.front().text = "Child";
        const auto package = serialize_presentation(source);
        check(package.error == PresentationError::None, "multi-level fixture serializes");
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None, "multi-level fixture parses");
        auto scene = std::move(parsed.scene);
        scene.native_editable = true;
        const auto undo_snapshot = scene;
        PresentationEditCommand command;
        command.action = PresentationEditAction::FormatParagraph;
        command.paragraph_index = 1;
        command.list_level = 2;
        check(edit(scene, command), "one paragraph accepts a list level");
        const auto& edited = scene.slides[0].shapes[0].text.paragraphs;
        check(edited.size() == 2 && edited[0].list_level == 0 && edited[1].list_level == 2 &&
                std::abs(edited[1].margin_left - 72) < 1e-8,
            "level edit changes only the selected paragraph and its visible indentation");
        const auto xml = parts(scene).at("ppt/slides/slide1.xml");
        check(xml.find("lvl=\"2\"") != std::string::npos &&
                parts(undo_snapshot).at("ppt/slides/slide1.xml").find("lvl=\"2\"") == std::string::npos,
            "preserved package writes the chosen level while undo snapshot remains intact");
        const auto reopened = reopen(scene);
        const auto& saved = reopened.slides[0].shapes[0].text.paragraphs;
        check(saved.size() == 2 && saved[0].list_level == 0 && saved[1].list_level == 2 &&
                std::abs(saved[1].margin_left - 72) < 1e-8,
            "multi-level numbering survives save and reload");
        command.list_level = 0;
        check(edit(scene, command), "paragraph level can return to zero");
        const auto reset_xml = parts(scene).at("ppt/slides/slide1.xml");
        check(reset_xml.find("lvl=\"0\"") != std::string::npos &&
                reset_xml.find("lvl=\"\"") == std::string::npos &&
                reopen(scene).slides[0].shapes[0].text.paragraphs[1].list_level == 0,
            "native package stores a valid zero level and reopens it");
        const auto stable = parts(scene);
        command.list_level = 9;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                parts(scene) == stable,
            "out-of-range list level leaves the package unchanged");
        command.list_level = 1;
        command.paragraph_index = 2;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                parts(scene) == stable,
            "out-of-range paragraph index leaves the package unchanged");
    }

    int run_preservation_tests()
    {
        test_model_only_edit_stages_package();
        test_speaker_notes();
        test_image_fills();
        test_theme_inheritance_edit();
        test_theme_effect_reference();
        test_placeholder_style_inheritance();
        test_text_style_source_layers();
        test_authored_text_inheritance_reset();
        test_authored_theme_links();
        test_theme_missing_slots();
        test_table_structure_edit();
        test_rectangular_table_unmerge();
        test_group_roundtrip();
        test_group_extension();
        test_multilevel_list_edit();
        auto art = make_presentation(PresentationSlideLayout::Title);
        auto& text = art.slides[0].shapes[0].text;
        text.warp = "textWave1";
        text.warp_adjustment = 0.3;
        text.rotation = 12;
        auto& run = text.paragraphs[0].runs[0];
        run.spacing = -1;
        run.baseline = 0.3;
        run.strike = true;
        run.fill.stops = {{0, "#FF0000", 1}, {1, "#0000FF", 1}};
        run.effects.outline_color = "#112233";
        run.effects.outline_width = 1.5;
        run.effects.shadow_color = "#334455";
        run.effects.shadow_opacity = 0.6;
        run.effects.shadow_x = 3;
        run.effects.shadow_y = 4;
        run.effects.shadow_blur = 2;
        run.effects.glow_color = "#00FF00";
        run.effects.glow_opacity = 0.5;
        run.effects.glow_radius = 3;
        run.effects.reflection_opacity = 0.4;
        run.effects.reflection_offset = 2;
        auto art_copy = reopen(art);
        const auto& copied_text = art_copy.slides[0].shapes[0].text;
        const auto& copied_run = copied_text.paragraphs[0].runs[0];
        check(copied_text.warp == "textWave1" && std::abs(copied_text.warp_adjustment - 0.3) < 1e-8 &&
                copied_text.rotation == 12,
            "WordArt preset adjustment and text rotation persist");
        check(copied_run.fill.stops.size() == 2 && copied_run.effects.outline_width == 1.5 &&
                copied_run.spacing == -1 && copied_run.baseline == 0.3 && copied_run.strike &&
                std::abs(copied_run.effects.shadow_x - 3) < 1e-6 && copied_run.effects.glow_radius == 3 &&
                copied_run.effects.reflection_opacity == 0.4,
            "text gradient outline shadow glow reflection and character effects persist");
        PresentationEditCommand replace;
        replace.action = PresentationEditAction::UpdateText;
        replace.text = "艺术字改字";
        edit(art_copy, replace);
        check(reopen(art_copy).slides[0].shapes[0].text.paragraphs[0].runs[0].effects.glow_radius == 3,
            "text replacement retains supported WordArt character effects");
        auto scene = fixture();
        const auto original = scene;
        const auto original_parts = parts(scene);
        PresentationEditCommand command;
        command.action = PresentationEditAction::FormatText;
        command.bold = false;
        command.font_size = 36;
        command.alignment = "center";
        command.bullet = true;
        edit(scene, command);
        auto changed = parts(scene);
        for (const auto& part : original_parts)
            if (part.first != "ppt/slides/slide1.xml")
                check(changed.at(part.first) == part.second, "unmodified part remains byte exact");
        const auto& xml = changed.at("ppt/slides/slide1.xml");
        for (const auto* marker : {"p:timing", "p:transition", "p:graphicFrame", "unknown-data", "a:glow",
                 "a:prstTxWarp", "a:videoFile"})
            check(xml.find(marker) != std::string::npos, std::string("format preserves ") + marker);
        check(parts(original) == original_parts, "immutable undo snapshot keeps exact source");
        auto checked = reopen(scene);
        check(checked.slides[0].shapes[0].text.paragraphs[0].runs[0].font_size == 36 &&
                !checked.slides[0].shapes[0].text.paragraphs[0].runs[0].bold && checked.media.size() == 1,
            "format and media survive reload");

        command = {};
        command.action = PresentationEditAction::FormatText;
        command.strike = true;
        command.character_spacing = 2.5;
        command.baseline = -0.25;
        edit(scene, command);
        command = {};
        command.action = PresentationEditAction::FormatParagraph;
        command.alignment = "right";
        command.bullet = false;
        command.numbered = true;
        command.number_start = 4;
        command.paragraph_margin_left = 30;
        command.first_line_indent = -15;
        command.line_spacing = 1.4;
        command.space_before = 5;
        command.space_after = 7;
        edit(scene, command);
        command = {};
        command.action = PresentationEditAction::FormatTextBox;
        command.inset_left = 11;
        command.inset_right = 12;
        command.inset_top = 6;
        command.inset_bottom = 8;
        command.vertical_alignment = "bottom";
        command.wrap = false;
        command.auto_fit = true;
        edit(scene, command);
        checked = reopen(scene);
        const auto& advanced_text = checked.slides[0].shapes[0].text;
        const auto& advanced_paragraph = advanced_text.paragraphs[0];
        const auto& advanced_run = advanced_paragraph.runs[0];
        check(advanced_run.strike && std::abs(advanced_run.spacing - 2.5) < 1e-8 &&
                std::abs(advanced_run.baseline + 0.25) < 1e-8 && advanced_paragraph.numbered &&
                advanced_paragraph.number_start == 4 && advanced_paragraph.alignment == "right" &&
                std::abs(advanced_paragraph.margin_left - 30) < 1e-8 &&
                std::abs(advanced_paragraph.first_line_indent + 15) < 1e-8 &&
                std::abs(advanced_paragraph.line_spacing - 1.4) < 1e-8 &&
                std::abs(advanced_paragraph.space_before - 5) < 1e-8 &&
                std::abs(advanced_paragraph.space_after - 7) < 1e-8,
            "character and paragraph formatting survive preserved-package reload");
        check(std::abs(advanced_text.inset_left - 11) < 1e-8 &&
                std::abs(advanced_text.inset_bottom - 8) < 1e-8 &&
                advanced_text.vertical_alignment == "bottom" && !advanced_text.wrap && advanced_text.auto_fit,
            "text box formatting survives preserved-package reload");
        for (const auto* marker : {"p:timing", "p:transition", "p:graphicFrame", "unknown-data", "a:glow",
                 "a:prstTxWarp", "a:videoFile"})
            check(parts(scene).at("ppt/slides/slide1.xml").find(marker) != std::string::npos,
                std::string("advanced text format preserves ") + marker);

        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.x = 99;
        command.y = 55;
        command.width = 500;
        edit(scene, command);
        checked = reopen(scene);
        check(checked.slides[0].shapes[0].transform[4] == 99 && checked.slides[0].shapes[0].width == 500,
            "transform stored without dropping effects");
        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.rotation = 90;
        edit(scene, command);
        checked = reopen(scene);
        check(std::abs(checked.slides[0].shapes[0].transform[0]) < 1e-8 &&
                std::abs(checked.slides[0].shapes[0].transform[1] - 1) < 1e-8 &&
                parts(scene).at("ppt/slides/slide1.xml").find("a:outerShdw") != std::string::npos,
            "rotation persists while unsupported effects remain intact");
        command = {};
        command.action = PresentationEditAction::UpdateText;
        command.text = "新的标题\n第二行";
        edit(scene, command);
        checked = reopen(scene);
        check(checked.slides[0].shapes[0].text.paragraphs.size() == 2 &&
                checked.slides[0].shapes[0].text.paragraphs[0].runs[0].text == "新的标题",
            "text replacement persisted");
        check(parts(scene).at("ppt/slides/slide1.xml").find("a:prstTxWarp") != std::string::npos,
            "text replacement preserves WordArt body definition");

        command = {};
        command.action = PresentationEditAction::FormatShape;
        command.gradient_start_color = "#123456";
        command.gradient_end_color = "#ABCDEF";
        command.gradient_angle = 35;
        command.fill_opacity = 0.7;
        command.outline_color = "#654321";
        command.outline_opacity = 0.55;
        command.outline_width = 2;
        command.line_dash = "dashDot";
        command.line_head = "oval";
        command.line_tail = "triangle";
        command.shadow_enabled = true;
        command.shadow_color = "#223344";
        command.shadow_x = 4;
        command.shadow_y = 5;
        command.glow_enabled = true;
        command.glow_color = "#4488FF";
        command.glow_radius = 7;
        edit(scene, command);
        checked = reopen(scene);
        const auto& advanced_shape = checked.slides[0].shapes[0];
        check(advanced_shape.fill.stops.size() == 2 && advanced_shape.fill.stops.front().color == "#123456" &&
                advanced_shape.fill.stops.back().color == "#ABCDEF" &&
                std::abs(advanced_shape.fill.angle_degrees - 35) < 1e-8 &&
                std::abs(advanced_shape.fill.opacity - 0.7) < 1e-8 &&
                std::abs(advanced_shape.outline_opacity - 0.55) < 1e-8 &&
                advanced_shape.line_style.dashes == std::vector<double>({4, 3, 1, 3}) &&
                advanced_shape.line_style.head.type == "oval" &&
                advanced_shape.line_style.tail.type == "triangle",
            "gradient opacity dash and arrow formatting persist");
        const auto styled_parts = parts(scene);
        const auto& styled_xml = styled_parts.at("ppt/slides/slide1.xml");
        check(styled_xml.find("unknown-data") != std::string::npos,
            "shape effect formatting preserves unrelated slide data");
        check(styled_xml.find("a:outerShdw") != std::string::npos,
            "shape effect formatting writes the edited shadow");
        check(
            styled_xml.find("a:glow") != std::string::npos, "shape effect formatting writes the edited glow");
        check(styled_xml.find("a:softEdge") != std::string::npos,
            "shape effect formatting preserves unrelated effect children");
        command = {};
        command.action = PresentationEditAction::DuplicateShape;
        edit(scene, command);
        checked = reopen(scene);
        check(checked.slides[0].shapes.size() == 3 &&
                checked.slides[0].shapes[1].media_path == "ppt/media/clip.mp4" &&
                checked.slides[0].shapes[0].source_id != checked.slides[0].shapes[1].source_id,
            "duplicate keeps media and allocates unique OOXML shape ID");
        const auto moved_source_id = scene.slides[0].shapes[0].source_id;
        command = {};
        command.action = PresentationEditAction::MoveShape;
        command.target_index = 2;
        edit(scene, command);
        checked = reopen(scene);
        check(checked.slides[0].shapes[2].source_id == moved_source_id,
            "direct object layer movement persisted");
        command = {};
        command.action = PresentationEditAction::DeleteShape;
        edit(scene, command);
        check(reopen(scene).slides[0].shapes.size() == 2, "deletion persisted");

        command = {};
        command.action = PresentationEditAction::AddShape;
        command.geometry = "ellipse";
        edit(scene, command);
        checked = reopen(scene);
        check(
            checked.slides[0].shapes.back().geometry == "ellipse", "new object inserted into original tree");
        command = {};
        command.action = PresentationEditAction::AddImage;
        command.image_path = "insertion";
        command.image_mime_type = "image/png";
        command.image_bytes = "image-fixture";
        edit(scene, command);
        checked = reopen(scene);
        check(checked.images.size() == 1 && checked.images.front().mime_type == "image/png" &&
                checked.images.front().bytes && *checked.images.front().bytes == "image-fixture",
            "inserted image uses valid relationship and content type");
        command = {};
        command.action = PresentationEditAction::FormatImage;
        command.shape_index = scene.slides[0].shapes.size() - 1;
        command.image_crop_left = 0.12;
        command.image_crop_top = 0.08;
        command.image_crop_right = 0.18;
        command.image_crop_bottom = 0.04;
        command.image_opacity = 0.65;
        edit(scene, command);
        checked = reopen(scene);
        const auto& picture = checked.slides[0].shapes.back();
        check(std::abs(picture.image_crop[0] - 0.12) < 1e-8 &&
                std::abs(picture.image_crop[2] - 0.18) < 1e-8 &&
                std::abs(picture.image_opacity - 0.65) < 1e-8,
            "image crop and opacity survive preserved-package reload");
        const auto image_xml = parts(scene).at("ppt/slides/slide1.xml");
        check(image_xml.find("a:srcRect") != std::string::npos &&
                image_xml.find("a:alphaModFix") != std::string::npos &&
                image_xml.find("unknown-data") != std::string::npos,
            "image formatting patches only the picture while preserving unknown slide data");
        const auto picture_width = scene.slides[0].shapes.back().width;
        const auto picture_height = scene.slides[0].shapes.back().height;
        command = {};
        command.action = PresentationEditAction::ReplaceImage;
        command.shape_index = scene.slides[0].shapes.size() - 1;
        command.image_path = "replacement";
        command.image_mime_type = "image/jpeg";
        command.image_bytes = "replacement-fixture";
        edit(scene, command);
        checked = reopen(scene);
        const auto& replaced_picture = checked.slides[0].shapes.back();
        check(replaced_picture.image_path.find("mirrorfly") != std::string::npos &&
                std::abs(replaced_picture.width - picture_width) < 1e-8 &&
                std::abs(replaced_picture.height - picture_height) < 1e-8 &&
                std::abs(replaced_picture.image_crop[0] - 0.12) < 1e-8 &&
                std::abs(replaced_picture.image_opacity - 0.65) < 1e-8 &&
                std::any_of(checked.images.begin(), checked.images.end(),
                    [](const auto& image)
        {
            return image.mime_type == "image/jpeg" && image.bytes && *image.bytes == "replacement-fixture";
        }),
            "image replacement preserves geometry crop and opacity in an imported package");
        check(parts(scene).at("ppt/slides/slide1.xml").find("unknown-data") != std::string::npos,
            "image replacement preserves unknown slide data");

        command = {};
        command.action = PresentationEditAction::DuplicateSlide;
        edit(scene, command);
        checked = reopen(scene);
        check(checked.slides.size() == 2 &&
                checked.slides[1].shapes.size() == scene.slides[1].shapes.size() &&
                std::any_of(checked.slides[1].shapes.begin(), checked.slides[1].shapes.end(),
                    [](const auto& shape)
        {
            return shape.media_path == "ppt/media/clip.mp4";
        }),
            "slide duplication retains unsupported parts and media");
        const auto duplicated = parts(scene);
        check(duplicated.count("ppt/notesSlides/mirrorfly1.xml") == 1 &&
                duplicated.at("ppt/notesSlides/_rels/mirrorfly1.xml.rels")
                        .find("/ppt/slides/mirrorfly1.xml") != std::string::npos,
            "duplicated notes own an updated backlink");
        command = {};
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 1;
        command.layout = PresentationSlideLayout::TitleContent;
        edit(scene, command);
        checked = reopen(scene);
        check(checked.slides.size() == 3 && checked.slides[1].shapes.size() == 2,
            "native slide added with isolated theme and layout relationships");
        command = {};
        command.action = PresentationEditAction::SetBackground;
        command.slide_index = 1;
        command.background_color = "#ABCDEF";
        edit(scene, command);
        command = {};
        command.action = PresentationEditAction::SetSlideHidden;
        command.slide_index = 1;
        command.hidden = true;
        edit(scene, command);
        command = {};
        command.action = PresentationEditAction::MoveSlide;
        command.slide_index = 1;
        command.offset = -1;
        edit(scene, command);
        checked = reopen(scene);
        check(checked.slides[0].hidden && checked.slides[0].background.color == "#ABCDEF",
            "slide flags background and reorder persist");
        command = {};
        command.action = PresentationEditAction::DeleteSlide;
        edit(scene, command);
        check(reopen(scene).slides.size() == 2, "slide deletion removes displayed slide only");

        const auto stable = parts(scene);
        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.width = -1;
        check(apply_presentation_edit(scene, command).error != PresentationEditError::None &&
                parts(scene) == stable,
            "failed edit keeps both scene and source package unchanged");
        scene.slides[0].shapes[0].source_part = "ppt/slideMasters/slideMaster1.xml";
        command.width = 100;
        check(apply_presentation_edit(scene, command).error != PresentationEditError::None &&
                parts(scene) == stable,
            "inherited objects cannot mutate their shared master");
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_preservation_tests();
}
