#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <iostream>
#include <map>
#include <sstream>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    std::map<std::string, std::string> parts(const mirrorfly::PresentationScene& scene)
    {
        std::map<std::string, std::string> result;
        const auto package = mirrorfly::serialize_presentation(scene);
        check(package.error == mirrorfly::PresentationError::None, "serialize section document");
        for (const auto& part : package.parts)
            result[part.path] = part.bytes;
        return result;
    }

    mirrorfly::PresentationScene reopen(const mirrorfly::PresentationScene& scene)
    {
        const auto package = mirrorfly::serialize_presentation(scene);
        const auto parsed = mirrorfly::parse_presentation(package.parts);
        check(parsed.error == mirrorfly::PresentationError::None, "reopen section document");
        return parsed.scene;
    }

    void section_lifecycle()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddSlide;
        command.layout = PresentationSlideLayout::Blank;
        for (int index = 0; index < 3; ++index)
        {
            command.slide_index = scene.slides.size();
            check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
                "add slides before sections");
        }
        command = {};
        command.action = PresentationEditAction::CreateSection;
        command.slide_index = 2;
        command.section_name = "课程内容";
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "create a section from the selected slide");
        check(scene.sections.size() == 2 && scene.slides[0].section_id == scene.sections[0].id &&
                scene.slides[1].section_id == scene.sections[0].id &&
                scene.slides[2].section_id == scene.sections[1].id &&
                scene.slides[3].section_id == scene.sections[1].id,
            "section membership covers contiguous slides");
        const auto loaded = reopen(scene);
        check(loaded.sections.size() == 2 && loaded.sections[1].name == "课程内容" &&
                loaded.slides[2].section_id == loaded.sections[1].id,
            "section names and slide IDs survive OOXML readback");
        const auto before = parts(scene);
        command.action = PresentationEditAction::RenameSection;
        command.section_name = "重点讲解";
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                reopen(scene).sections[1].name == "重点讲解",
            "rename a section and reopen");
        const auto renamed = parts(scene);
        for (const auto& [path, bytes] : before)
            if (path != "ppt/presentation.xml")
                check(renamed.at(path) == bytes, "section rename changes only presentation.xml");
        command = {};
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 2;
        command.layout = PresentationSlideLayout::Blank;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[2].section_id == scene.sections[1].id &&
                reopen(scene).slides[2].section_id == scene.sections[1].id,
            "new slide at a section boundary joins the following section");
        command = {};
        command.action = PresentationEditAction::DuplicateSlide;
        command.slide_index = 2;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                reopen(scene).slides.size() == 6 && scene.slides[3].section_id == scene.sections[1].id,
            "duplicated slide retains its section");
        command.action = PresentationEditAction::MoveSlide;
        command.slide_index = 1;
        command.offset = 1;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                reopen(scene).slides[2].section_id == scene.sections[1].id,
            "moving a slide across a boundary joins the destination section");
        command = {};
        command.action = PresentationEditAction::DeleteSlide;
        command.slide_index = 0;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.sections.size() == 1 && reopen(scene).sections.size() == 1,
            "deleting a section's last slide removes the empty section");
        const auto stable = parts(scene);
        command.action = PresentationEditAction::RenameSection;
        command.section_name = "";
        check(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                parts(scene) == stable,
            "invalid section name is atomic");
        command.action = PresentationEditAction::RemoveSection;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.sections.empty() && reopen(scene).sections.empty(),
            "removing the last section restores an unsectioned presentation");
        check(parts(scene).at("ppt/presentation.xml").find("sectionLst") == std::string::npos,
            "last section extension is removed");
    }

    void malformed_sections_stay_locked()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 1;
        apply_presentation_edit(scene, command);
        auto package = serialize_presentation(scene);
        for (auto& part : package.parts)
            if (part.path == "ppt/presentation.xml")
            {
                pugi::xml_document xml;
                xml.load_string(part.bytes.c_str());
                auto root = xml.document_element();
                root.append_attribute("xmlns:p14") =
                    "http://schemas.microsoft.com/office/powerpoint/2010/main";
                auto ext = root.append_child("p:extLst").append_child("p:ext");
                ext.append_attribute("uri") = "{521415D9-36F7-43E2-AB2F-B90AF26B5E84}";
                auto section = ext.append_child("p14:sectionLst").append_child("p14:section");
                section.append_attribute("name") = "Broken";
                section.append_attribute("id") = "{INVALID}";
                section.append_child("p14:sldIdLst").append_child("p14:sldId").append_attribute("id") =
                    "9999";
                std::ostringstream output;
                xml.save(output);
                part.bytes = output.str();
            }
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None && parsed.scene.section_structure_locked,
            "malformed section membership is read-only");
        parsed.scene.native_editable = true;
        const auto original = parts(parsed.scene);
        command = {};
        command.action = PresentationEditAction::CreateSection;
        command.section_name = "新节";
        check(apply_presentation_edit(parsed.scene, command).error == PresentationEditError::ReadOnly &&
                parts(parsed.scene) == original,
            "malformed section edit preserves every package part");
    }
}

int run_section_tests()
{
    section_lifecycle();
    malformed_sections_stay_locked();
    return failures ? 1 : 0;
}

int main()
{
    return run_section_tests();
}
