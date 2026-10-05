#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>

namespace
{
    int failures = 0;

    void check(bool condition, const char* description)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }

    std::vector<mirrorfly::PresentationPart> linked_package(const char* action,
        const char* relation_type = "slide", bool external = false, bool text_link = false)
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand edit;
        edit.action = PresentationEditAction::AddText;
        edit.text = "Jump";
        edit.width = 120;
        edit.height = 60;
        check(apply_presentation_edit(scene, edit).error == PresentationEditError::None,
            "create clickable shape");
        edit = {};
        edit.action = PresentationEditAction::AddSlide;
        edit.layout = PresentationSlideLayout::Blank;
        for (int index = 0; index < 2; ++index)
        {
            edit.slide_index = scene.slides.size();
            check(apply_presentation_edit(scene, edit).error == PresentationEditError::None,
                "create jump target slides");
        }
        auto package = serialize_presentation(scene);
        check(package.error == PresentationError::None, "serialize navigation fixture");
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/slides/slide1.xml" && part.path != "ppt/slides/_rels/slide1.xml.rels")
                continue;
            pugi::xml_document xml;
            check(xml.load_string(part.bytes.c_str()), "parse navigation fixture part");
            if (part.path == "ppt/slides/slide1.xml")
            {
                auto shape = xml.document_element().child("p:cSld").child("p:spTree").child("p:sp");
                auto properties = text_link ? shape.child("p:txBody").child("a:p").child("a:r").child("a:rPr")
                                            : shape.child("p:nvSpPr").child("p:cNvPr");
                if (text_link && !properties)
                    properties = shape.child("p:txBody").child("a:p").child("a:r").prepend_child("a:rPr");
                check(static_cast<bool>(properties), "fixture contains a clickable shape");
                auto link = properties.append_child("a:hlinkClick");
                link.append_attribute("action") = action;
                if (std::string(action) == "ppaction://hlinksldjump")
                    link.append_attribute("r:id") = "rIdNavigation";
            }
            else
            {
                auto relation = xml.document_element().append_child("Relationship");
                relation.append_attribute("Id") = "rIdNavigation";
                relation.append_attribute("Type") =
                    (std::string("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") +
                        relation_type)
                        .c_str();
                relation.append_attribute("Target") = external ? "https://example.invalid" : "slide3.xml";
                if (external)
                    relation.append_attribute("TargetMode") = "External";
            }
            std::ostringstream output;
            xml.save(output);
            part.bytes = output.str();
        }
        return package.parts;
    }

    void internal_slide_jump()
    {
        using namespace mirrorfly;
        const auto parts = linked_package("ppaction://hlinksldjump");
        auto parsed = parse_presentation(parts);
        check(parsed.error == PresentationError::None && parsed.scene.slides.size() == 3,
            "import slide link fixture");
        if (parsed.error != PresentationError::None || parsed.scene.slides.empty() ||
            parsed.scene.slides[0].shapes.empty())
            return;
        const auto& click = parsed.scene.slides[0].shapes[0].click_action;
        check(click.kind == "slide" && click.target_slide == 2,
            "slide relation resolves against presentation order");
        const auto navigation = resolve_presentation_click(parsed.scene, 0, 0);
        check(navigation.handled && !navigation.end_show && navigation.target_slide == 2,
            "click navigates to the linked slide");
        check(
            !resolve_presentation_click(parsed.scene, 0, 100).handled, "out-of-range shape cannot navigate");
        parsed.scene.native_editable = true;
        const auto saved = serialize_presentation(parsed.scene);
        check(saved.error == PresentationError::None, "linked import serializes");
        for (const auto& original : parts)
            for (const auto& current : saved.parts)
                if (original.path == current.path)
                    check(original.bytes == current.bytes, "unmodified link package stays byte-identical");
    }

    void show_jumps()
    {
        using namespace mirrorfly;
        auto parsed = parse_presentation(linked_package("ppaction://hlinkshowjump?jump=nextslide"));
        check(parsed.error == PresentationError::None, "import next-slide action");
        if (parsed.error != PresentationError::None || parsed.scene.slides.size() != 3)
            return;
        parsed.scene.slides[1].hidden = true;
        auto navigation = resolve_presentation_click(parsed.scene, 0, 0);
        check(navigation.handled && navigation.target_slide == 2, "next-slide action skips hidden slides");
        parsed = parse_presentation(linked_package("ppaction://hlinkshowjump?jump=lastslideviewed"));
        navigation = resolve_presentation_click(parsed.scene, 0, 0, 2);
        check(
            navigation.handled && navigation.target_slide == 2, "last-viewed action uses slide-show history");
        check(!resolve_presentation_click(parsed.scene, 0, 0).handled,
            "last-viewed action without history is inert");
        parsed = parse_presentation(linked_package("ppaction://hlinkshowjump?jump=endshow"));
        navigation = resolve_presentation_click(parsed.scene, 0, 0);
        check(navigation.handled && navigation.end_show, "end-show action exits playback");
        parsed = parse_presentation(linked_package("ppaction://hlinkshowjump?jump=3"));
        navigation = resolve_presentation_click(parsed.scene, 0, 0);
        check(navigation.handled && navigation.target_slide == 2,
            "numbered show jump uses one-based slide numbering");
        parsed = parse_presentation(linked_package("ppaction://hlinkshowjump?jump=lastslide"));
        parsed.scene.slides[2].hidden = true;
        navigation = resolve_presentation_click(parsed.scene, 0, 0);
        check(navigation.handled && navigation.target_slide == 1, "last-slide show jump skips hidden slides");
    }

    void fully_linked_text()
    {
        using namespace mirrorfly;
        auto parsed = parse_presentation(linked_package("ppaction://hlinksldjump", "slide", false, true));
        check(parsed.error == PresentationError::None && !parsed.scene.slides[0].shapes.empty(),
            "import text-run link fixture");
        if (parsed.error != PresentationError::None || parsed.scene.slides[0].shapes.empty())
            return;
        const auto& shape = parsed.scene.slides[0].shapes[0];
        check(!shape.text.paragraphs.empty() && !shape.text.paragraphs[0].runs.empty() &&
                shape.text.paragraphs[0].runs[0].click_action.target_slide == 2 &&
                resolve_presentation_click(parsed.scene, 0, 0).target_slide == 2,
            "uniformly linked text activates the slide jump");
        parsed.scene.native_editable = true;
        PresentationEditCommand edit;
        edit.action = PresentationEditAction::UpdateText;
        edit.text = "Replaced";
        check(apply_presentation_edit(parsed.scene, edit).error == PresentationEditError::None &&
                !resolve_presentation_click(parsed.scene, 0, 0).handled,
            "replacing linked text clears its old click action");
        const auto reopened = parse_presentation(serialize_presentation(parsed.scene).parts);
        check(reopened.error == PresentationError::None &&
                !resolve_presentation_click(reopened.scene, 0, 0).handled,
            "old text link stays removed after save and reopen");
    }

    void unsafe_targets_are_ignored()
    {
        using namespace mirrorfly;
        auto parsed = parse_presentation(linked_package("ppaction://hlinksldjump", "slide", true));
        check(parsed.error == PresentationError::None &&
                !resolve_presentation_click(parsed.scene, 0, 0).handled,
            "external relationship cannot become an internal jump");
        parsed = parse_presentation(linked_package("ppaction://hlinksldjump", "image"));
        check(parsed.error == PresentationError::None &&
                !resolve_presentation_click(parsed.scene, 0, 0).handled,
            "wrong relationship type cannot become a slide jump");
        parsed = parse_presentation(linked_package("ppaction://macro?name=Unsafe"));
        check(parsed.error == PresentationError::None &&
                !resolve_presentation_click(parsed.scene, 0, 0).handled,
            "macro click actions remain inert");
    }

    void author_and_remove_jump()
    {
        using namespace mirrorfly;
        auto native = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddText;
        command.text = "Go";
        command.width = 100;
        command.height = 40;
        check(apply_presentation_edit(native, command).error == PresentationEditError::None,
            "create link authoring object");
        command = {};
        command.action = PresentationEditAction::AddSlide;
        command.layout = PresentationSlideLayout::Blank;
        for (int index = 0; index < 2; ++index)
        {
            command.slide_index = native.slides.size();
            check(apply_presentation_edit(native, command).error == PresentationEditError::None,
                "create authoring destination");
        }
        auto parsed = parse_presentation(serialize_presentation(native).parts);
        check(parsed.error == PresentationError::None, "read authoring fixture");
        if (parsed.error != PresentationError::None)
            return;
        auto& scene = parsed.scene;
        scene.native_editable = true;
        std::map<std::string, std::string> before;
        for (const auto& part : serialize_presentation(scene).parts)
            before[part.path] = part.bytes;
        command = {};
        command.action = PresentationEditAction::SetClickAction;
        command.click_kind = "slide";
        command.click_target_slide = 2;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                resolve_presentation_click(scene, 0, 0).target_slide == 2,
            "author slide jump through public edit command");
        const auto first = serialize_presentation(scene);
        check(first.error == PresentationError::None, "serialize authored jump");
        std::map<std::string, std::string> linked;
        for (const auto& part : first.parts)
            linked[part.path] = part.bytes;
        for (const auto& [path, bytes] : before)
            if (path != "ppt/slides/slide1.xml" && path != "ppt/slides/_rels/slide1.xml.rels")
                check(linked.at(path) == bytes, "authoring changes only target slide and its relations");
        check(linked.at("ppt/slides/slide1.xml").find("ppaction://hlinksldjump") != std::string::npos &&
                linked.at("ppt/slides/_rels/slide1.xml.rels").find("slide3.xml") != std::string::npos,
            "authored link has a slide relationship");
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "setting an existing jump again succeeds");
        const auto repeated = serialize_presentation(scene);
        for (const auto& part : repeated.parts)
            if (part.path == "ppt/slides/_rels/slide1.xml.rels")
                check(part.bytes == linked.at(part.path), "same target reuses its relationship");
        const auto stable = serialize_presentation(scene).parts;
        command.click_target_slide = 500;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue,
            "invalid target is rejected");
        const auto unchanged = serialize_presentation(scene).parts;
        check(unchanged.size() == stable.size(), "invalid target leaves package part count unchanged");
        for (std::size_t index = 0; index < std::min(unchanged.size(), stable.size()); ++index)
            check(
                unchanged[index].path == stable[index].path && unchanged[index].bytes == stable[index].bytes,
                "invalid target leaves every package part unchanged");
        command.click_kind = "";
        command.click_target_slide.reset();
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                !resolve_presentation_click(scene, 0, 0).handled,
            "clear authored jump");
        const auto reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None &&
                !resolve_presentation_click(reopened.scene, 0, 0).handled,
            "cleared jump stays removed after reopen");
    }

    void links_follow_slide_edits()
    {
        using namespace mirrorfly;
        auto parsed = parse_presentation(linked_package("ppaction://hlinksldjump"));
        check(parsed.error == PresentationError::None, "import slide edit link fixture");
        if (parsed.error != PresentationError::None)
            return;
        auto& scene = parsed.scene;
        scene.native_editable = true;
        PresentationEditCommand edit;
        edit.action = PresentationEditAction::DuplicateShape;
        check(apply_presentation_edit(scene, edit).error == PresentationEditError::None &&
                resolve_presentation_click(scene, 0, 1).target_slide == 2,
            "duplicated shape keeps its click action");
        edit = {};
        edit.action = PresentationEditAction::DuplicateSlide;
        const auto duplicated = apply_presentation_edit(scene, edit);
        check(duplicated.error == PresentationEditError::None, "duplicate linked slide succeeds");
        if (duplicated.error != PresentationEditError::None)
            std::cerr << "Duplicate slide: " << duplicated.message << '\n';
        check(resolve_presentation_click(scene, 0, 0).target_slide == 3,
            "original link target shifts after slide duplication");
        check(resolve_presentation_click(scene, 1, 0).target_slide == 3,
            "duplicated slide retains link and shifts target index");
        auto reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None &&
                resolve_presentation_click(reopened.scene, 1, 0).target_slide == 3 &&
                resolve_presentation_click(reopened.scene, 0, 1).target_slide == 3,
            "duplicated shape and slide links survive save and reopen");
        edit = {};
        edit.action = PresentationEditAction::MoveSlide;
        edit.slide_index = 3;
        edit.offset = -1;
        check(apply_presentation_edit(scene, edit).error == PresentationEditError::None &&
                resolve_presentation_click(scene, 0, 0).target_slide == 2,
            "moving the target slide updates live navigation index");
        edit = {};
        edit.action = PresentationEditAction::DeleteSlide;
        edit.slide_index = 2;
        check(apply_presentation_edit(scene, edit).error == PresentationEditError::None &&
                !resolve_presentation_click(scene, 0, 0).handled,
            "deleting a target slide disables its old jump");
        reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None &&
                !resolve_presentation_click(reopened.scene, 0, 0).handled,
            "deleted target stays disabled after save and reopen");
    }
}

int run_presentation_navigation_tests()
{
    internal_slide_jump();
    show_jumps();
    fully_linked_text();
    unsafe_targets_are_ignored();
    author_and_remove_jump();
    links_follow_slide_edits();
    return failures ? 1 : 0;
}

int main()
{
    return run_presentation_navigation_tests();
}
