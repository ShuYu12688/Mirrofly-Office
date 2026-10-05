#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <iostream>
#include <map>
#include <sstream>

namespace
{
    int failures = 0;

    void check(bool value, const char* description)
    {
        if (!value)
        {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }

    std::map<std::string, std::string> part_bytes(const mirrorfly::PresentationScene& scene)
    {
        std::map<std::string, std::string> bytes;
        const auto package = mirrorfly::serialize_presentation(scene);
        check(package.error == mirrorfly::PresentationError::None, "serialize format brush scene");
        for (const auto& part : package.parts)
            bytes[part.path] = part.bytes;
        return bytes;
    }

    void format_brush_round_trip()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddText;
        command.text = "Source";
        command.x = 10;
        command.y = 10;
        command.width = 120;
        command.height = 50;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "create source text object");
        command.text = "Target";
        command.x = 210;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "create target text object");
        command = {};
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 1;
        command.layout = PresentationSlideLayout::Blank;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "create second slide");
        command = {};
        command.action = PresentationEditAction::FormatShape;
        command.fill_color = "#A12542";
        command.outline_color = "#2468A0";
        command.outline_width = 3;
        command.shadow_enabled = true;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "style source shape");
        command = {};
        command.action = PresentationEditAction::FormatText;
        command.font_family = "Georgia";
        command.font_size = 30;
        command.bold = true;
        command.text_color = "#205030";
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "style source text");
        command = {};
        command.action = PresentationEditAction::SetClickAction;
        command.shape_index = 1;
        command.click_kind = "slide";
        command.click_target_slide = 1;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "link target before format brush");
        auto linked_parts = serialize_presentation(scene).parts;
        for (auto& part : linked_parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document xml;
            check(xml.load_buffer(part.bytes.data(), part.bytes.size()), "load text-link fixture");
            auto target_node = xml.document_element().child("p:cSld").child("p:spTree").child("p:sp");
            target_node = target_node.next_sibling("p:sp");
            const auto id = target_node.child("p:nvSpPr")
                                .child("p:cNvPr")
                                .child("a:hlinkClick")
                                .attribute("r:id")
                                .value();
            auto run = target_node.child("p:txBody").child("a:p").child("a:r");
            auto properties = run.child("a:rPr");
            if (!properties)
                properties = run.prepend_child("a:rPr");
            auto link = properties.append_child("a:hlinkClick");
            link.append_attribute("r:id") = id;
            link.append_attribute("action") = "ppaction://hlinksldjump";
            std::ostringstream output;
            xml.save(output);
            part.bytes = output.str();
        }
        const auto linked = parse_presentation(std::move(linked_parts));
        check(linked.error == PresentationError::None, "import linked text fixture");
        if (linked.error != PresentationError::None)
            return;
        scene = std::move(linked.scene);
        scene.native_editable = true;
        const auto before = part_bytes(scene);
        const auto target_x = scene.slides[0].shapes[1].transform[4];
        command = {};
        command.action = PresentationEditAction::ApplyFormat;
        command.shape_index = 1;
        const auto result = apply_presentation_edit(scene, command);
        if (result.error != PresentationEditError::None)
            std::cerr << "Format brush: " << result.message << '\n';
        check(result.error == PresentationEditError::None, "apply format in one core transaction");
        if (result.error != PresentationEditError::None)
            return;
        const auto& source = scene.slides[0].shapes[0];
        const auto& target = scene.slides[0].shapes[1];
        check(target.fill.color == source.fill.color && target.outline_color == source.outline_color &&
                target.effects.shadow_opacity == source.effects.shadow_opacity,
            "shape appearance copied");
        check(target.text.paragraphs[0].runs[0].font_family == "Georgia" &&
                target.text.paragraphs[0].runs[0].bold && target.text.paragraphs[0].runs[0].text == "Target",
            "typography copied while target text stays intact");
        check(target.transform[4] == target_x && target.click_action.target_slide == 1,
            "target position and click link preserved");
        check(target.text.paragraphs[0].runs[0].click_action.target_slide == 1,
            "target text-run link survives format brush in memory");
        const auto after = part_bytes(scene);
        for (const auto& [path, bytes] : before)
            if (path != "ppt/slides/slide1.xml")
                check(after.at(path) == bytes, "only target slide XML changes");
        const auto reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None, "reopen format brush package");
        if (reopened.error == PresentationError::None)
        {
            const auto& restored = reopened.scene.slides[0].shapes[1];
            check(restored.fill.color == source.fill.color &&
                    restored.text.paragraphs[0].runs[0].font_family == "Georgia" &&
                    restored.text.paragraphs[0].runs[0].text == "Target" &&
                    restored.click_action.target_slide == 1 &&
                    restored.text.paragraphs[0].runs[0].click_action.target_slide == 1,
                "format and target content survive save and reopen");
        }
        command.format_source_shape = 99;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidIndex &&
                part_bytes(scene) == after,
            "invalid source rejects edit without touching package");

        const auto source_color = source.fill.color;

        command = {};
        command.action = PresentationEditAction::AddText;
        command.slide_index = 1;
        command.text = "Other page";
        command.width = 130;
        command.height = 50;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "add a target on another slide");
        command = {};
        command.action = PresentationEditAction::ApplyFormat;
        command.slide_index = 1;
        command.format_source_slide = 0;
        command.format_source_shape = 0;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes[0].fill.color == source_color,
            "format brush works across slides");
        const auto cross_page = parse_presentation(serialize_presentation(scene).parts);
        check(cross_page.error == PresentationError::None &&
                cross_page.scene.slides[1].shapes[0].text.paragraphs[0].runs[0].text == "Other page" &&
                cross_page.scene.slides[1].shapes[0].fill.color == source_color,
            "cross-slide formatting survives save and reopen");
    }

    void picture_format_keeps_resources()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddImage;
        command.image_mime_type = "image/png";
        command.image_bytes = "source-image-bytes";
        command.image_path = "source-picture";
        command.width = 80;
        command.height = 60;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "create source picture");
        command.image_bytes = "target-image-bytes";
        command.image_path = "target-picture";
        command.x = 100;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "create target picture");
        command = {};
        command.action = PresentationEditAction::FormatShape;
        command.outline_color = "#385A85";
        command.outline_width = 4;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "style source picture border");
        auto imported = parse_presentation(serialize_presentation(scene).parts);
        check(imported.error == PresentationError::None, "import picture format fixture");
        if (imported.error != PresentationError::None)
            return;
        scene = std::move(imported.scene);
        scene.native_editable = true;
        const auto resources_before = scene.images.size();
        const auto target_path = scene.slides[0].shapes[1].image_path;
        const auto target_bytes = scene.images[1].bytes;
        command = {};
        command.action = PresentationEditAction::ApplyFormat;
        command.shape_index = 1;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "copy picture appearance");
        check(scene.images.size() == resources_before &&
                scene.slides[0].shapes[1].image_path == target_path &&
                scene.slides[0].shapes[1].outline_color == "#385A85" && scene.images[1].bytes == target_bytes,
            "picture format brush keeps target bytes and avoids extra image resources");
        const auto reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None &&
                reopened.scene.slides[0].shapes[1].image_path !=
                    reopened.scene.slides[0].shapes[0].image_path &&
                reopened.scene.slides[0].shapes[1].outline_color == "#385A85",
            "picture formatting survives save and reopen without replacing content");
    }
}

int run_presentation_format_brush_tests()
{
    format_brush_round_trip();
    picture_format_keeps_resources();
    return failures ? 1 : 0;
}

int main()
{
    return run_presentation_format_brush_tests();
}
