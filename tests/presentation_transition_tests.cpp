#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <iostream>
#include <map>

namespace
{
    using namespace mirrorfly;
    int failures = 0;

    void check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << name << '\n';
            ++failures;
        }
    }

    std::map<std::string, std::string> parts(const PresentationScene& scene)
    {
        std::map<std::string, std::string> result;
        const auto package = serialize_presentation(scene);
        check(package.error == PresentationError::None, "serialize transition package");
        for (const auto& part : package.parts)
            result.emplace(part.path, part.bytes);
        return result;
    }

    PresentationScene imported(const std::string& transition)
    {
        auto package = serialize_presentation(make_presentation(PresentationSlideLayout::Blank));
        for (auto& part : package.parts)
            if (part.path == "ppt/slides/slide1.xml")
                part.bytes.insert(part.bytes.find("</p:sld>"), transition);
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None, "parse imported transition");
        parsed.scene.native_editable = true;
        return parsed.scene;
    }

    PresentationEditCommand command(const std::string& type = "push")
    {
        PresentationEditCommand edit;
        edit.action = PresentationEditAction::SetSlideTransition;
        PresentationTransition transition;
        transition.type = type;
        transition.direction = type == "push" ? "r" : "";
        transition.duration = 1.0;
        transition.advance_on_click = false;
        transition.advance_after = 2.5;
        edit.slide_transition = transition;
        return edit;
    }

    void test_authored_round_trip()
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        check(apply_presentation_edit(scene, command()).error == PresentationEditError::None,
            "authored transition edit succeeds");
        const auto package = serialize_presentation(scene);
        const auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].transition.type == "push" &&
                parsed.scene.slides[0].transition.direction == "r" &&
                parsed.scene.slides[0].transition.duration == 1.0 &&
                parsed.scene.slides[0].transition.advance_after == 2.5 &&
                !parsed.scene.slides[0].transition.advance_on_click,
            "authored transition survives serialize and parse");
    }

    void test_imported_local_edit()
    {
        auto scene = imported("<p:transition spd='fast' advClick='1'><p:fade/></p:transition>"
                              "<p:timing><p:tnLst/></p:timing>");
        const auto before = parts(scene);
        check(scene.slides[0].transition.editable, "plain imported transition editable");
        check(apply_presentation_edit(scene, command()).error == PresentationEditError::None,
            "imported transition edit succeeds");
        const auto after = parts(scene);
        for (const auto& [path, bytes] : before)
            if (path != "ppt/slides/slide1.xml")
                check(after.at(path) == bytes, "unrelated package part remains byte exact");
        pugi::xml_document slide;
        check(slide.load_string(after.at("ppt/slides/slide1.xml").c_str()), "edited slide XML parses");
        const auto transition = slide.document_element().child("p:transition");
        check(std::string(transition.attribute("spd").value()) == "slow" &&
                std::string(transition.attribute("advTm").value()) == "2500" &&
                std::string(transition.child("p:push").attribute("dir").value()) == "r" &&
                slide.document_element().child("p:timing"),
            "transition changes while timing remains");
        const auto reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None &&
                reopened.scene.slides[0].transition.type == "push" &&
                reopened.scene.slides[0].transition.editable,
            "imported transition reopens editable");
    }

    void test_protected_and_invalid()
    {
        for (const auto& xml :
            {"<p:transition><p:sndAc/></p:transition>",
                "<p:transition foo='unknown'><p:fade/></p:transition>",
                "<p:transition><p:extLst/></p:transition>",
                "<mc:AlternateContent xmlns:mc='http://schemas.openxmlformats.org/markup-compatibility/2006'>"
                "<mc:Choice Requires='p14'><p:transition><p:fade/></p:transition></mc:Choice>"
                "</mc:AlternateContent>"})
        {
            auto scene = imported(xml);
            const auto before = parts(scene);
            check(!scene.slides[0].transition.editable, "complex transition locked");
            check(apply_presentation_edit(scene, command()).error != PresentationEditError::None,
                "complex transition rejected");
            check(parts(scene) == before, "rejected transition leaves source unchanged");
        }
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        for (const auto& invalid : {"unknown", "wipe"})
        {
            auto edit = command(invalid);
            check(apply_presentation_edit(scene, edit).error == PresentationEditError::InvalidValue,
                "unsupported transition type rejected");
        }
        auto edit = command();
        edit.slide_transition->advance_after = 86401;
        check(apply_presentation_edit(scene, edit).error == PresentationEditError::InvalidValue,
            "oversized transition timer rejected");
    }
}

int run_transition_tests()
{
    test_authored_round_trip();
    test_imported_local_edit();
    test_protected_and_invalid();
    return failures ? 1 : 0;
}

int main()
{
    return run_transition_tests();
}
