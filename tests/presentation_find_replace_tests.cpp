#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

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

    std::map<std::string, std::string> parts(const mirrorfly::PresentationScene& scene)
    {
        std::map<std::string, std::string> result;
        const auto package = mirrorfly::serialize_presentation(scene);
        check(package.error == mirrorfly::PresentationError::None, "serialize find fixture");
        for (const auto& part : package.parts)
            result[part.path] = part.bytes;
        return result;
    }

    mirrorfly::PresentationScene fixture()
    {
        using namespace mirrorfly;
        auto native = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddText;
        command.text = "Hello";
        command.width = 140;
        command.height = 50;
        check(apply_presentation_edit(native, command).error == PresentationEditError::None,
            "create split-run source");
        auto& paragraph = native.slides[0].shapes[0].text.paragraphs[0];
        paragraph.runs[0].text = "Hel";
        auto second = paragraph.runs[0];
        second.text = "lo";
        second.bold = true;
        paragraph.runs.push_back(std::move(second));
        command.text = "hello hello";
        command.x = 170;
        check(apply_presentation_edit(native, command).error == PresentationEditError::None,
            "create repeated target");
        command = {};
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 1;
        command.layout = PresentationSlideLayout::Blank;
        check(apply_presentation_edit(native, command).error == PresentationEditError::None,
            "create search destination slide");
        command = {};
        command.action = PresentationEditAction::AddText;
        command.slide_index = 1;
        command.text = "HELLO";
        command.width = 140;
        command.height = 50;
        check(apply_presentation_edit(native, command).error == PresentationEditError::None,
            "create cross-slide result");
        auto parsed = parse_presentation(serialize_presentation(native).parts);
        check(parsed.error == PresentationError::None, "import find fixture");
        parsed.scene.native_editable = true;
        return std::move(parsed.scene);
    }

    void cross_run_and_all()
    {
        using namespace mirrorfly;
        auto scene = fixture();
        const auto matches = find_presentation_text(scene, "hello");
        check(matches.size() == 4 && matches[0].slide_index == 0 && matches[0].shape_index == 0 &&
                matches[0].replaceable && matches[0].start_byte == 0 && matches[3].slide_index == 1,
            "search finds case-insensitive matches across runs and slides");
        if (matches.empty())
            return;
        const auto before = parts(scene);
        PresentationEditCommand command;
        command.action = PresentationEditAction::ReplaceTextMatches;
        command.find_query = "hello";
        command.find_replacement = "Hi";
        command.find_shape_id = matches[0].shape_id;
        command.slide_index = matches[0].slide_index;
        command.shape_index = matches[0].shape_index;
        command.find_paragraph_index = matches[0].paragraph_index;
        command.find_start_byte = matches[0].start_byte;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "replace one cross-run match");
        const auto& runs = scene.slides[0].shapes[0].text.paragraphs[0].runs;
        check(runs.size() == 2 && runs[0].text == "Hi" && runs[1].text.empty() && runs[1].bold,
            "replacement keeps run structure and surviving formatting");
        const auto once = parts(scene);
        for (const auto& [path, bytes] : before)
            if (path != "ppt/slides/slide1.xml")
                check(once.at(path) == bytes, "single replacement edits only one slide XML part");
        auto reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None &&
                reopened.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text == "Hi" &&
                reopened.scene.slides[0].shapes[0].text.paragraphs[0].runs[1].bold,
            "cross-run edit survives package readback");
        command = {};
        command.action = PresentationEditAction::ReplaceTextMatches;
        command.find_query = "hello";
        command.find_replacement = "Done";
        command.find_all = true;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                find_presentation_text(scene, "hello").empty() &&
                scene.slides[0].shapes[1].text.paragraphs[0].runs[0].text == "Done Done" &&
                scene.slides[1].shapes[0].text.paragraphs[0].runs[0].text == "Done",
            "replace all covers every slide in one command");
        reopened = parse_presentation(serialize_presentation(scene).parts);
        check(reopened.error == PresentationError::None &&
                reopened.scene.slides[1].shapes[0].text.paragraphs[0].runs[0].text == "Done",
            "all replacements survive save and reopen");
        const auto stable = parts(scene);
        command.find_query.clear();
        check(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                parts(scene) == stable,
            "invalid query leaves every package part unchanged");
    }

    void protected_field_and_unicode()
    {
        using namespace mirrorfly;
        auto scene = fixture();
        auto package = serialize_presentation(scene);
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/slides/slide2.xml")
                continue;
            pugi::xml_document xml;
            check(xml.load_buffer(part.bytes.data(), part.bytes.size()), "load protected field fixture");
            auto run = xml.document_element()
                           .child("p:cSld")
                           .child("p:spTree")
                           .child("p:sp")
                           .child("p:txBody")
                           .child("a:p")
                           .child("a:r");
            check(static_cast<bool>(run), "field fixture has a run");
            run.set_name("a:fld");
            run.append_attribute("id") = "{A1B2C3D4-0000-0000-0000-000000000000}";
            run.append_attribute("type") = "slidenum";
            std::ostringstream output;
            xml.save(output);
            part.bytes = output.str();
        }
        auto parsed = parse_presentation(std::move(package.parts));
        check(parsed.error == PresentationError::None, "read protected field fixture");
        if (parsed.error != PresentationError::None)
            return;
        scene = std::move(parsed.scene);
        scene.native_editable = true;
        const auto matches = find_presentation_text(scene, "hello");
        check(matches.size() == 4 && !matches.back().replaceable,
            "field text is searchable but is marked read-only");
        PresentationEditCommand command;
        command.action = PresentationEditAction::ReplaceTextMatches;
        command.find_query = "hello";
        command.find_replacement = "new";
        command.find_all = true;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes[0].text.paragraphs[0].runs[0].text == "HELLO",
            "replace all preserves protected fields");
        command.find_query = "HELLO";
        command.find_all = false;
        command.slide_index = 1;
        command.find_shape_id = scene.slides[1].shapes[0].id;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue,
            "individual field replacement is rejected");
        auto native = make_presentation(PresentationSlideLayout::Blank);
        command = {};
        command.action = PresentationEditAction::AddText;
        command.text = "汉字汉字";
        command.width = 120;
        command.height = 50;
        check(apply_presentation_edit(native, command).error == PresentationEditError::None,
            "create Unicode search fixture");
        const auto chinese = find_presentation_text(native, "汉字");
        check(chinese.size() == 2 && chinese[1].start_byte == 6,
            "Unicode search reports UTF-8 byte offsets without splitting characters");
    }
}

int run_presentation_find_replace_tests()
{
    cross_run_and_all();
    protected_field_and_unicode();
    return failures ? 1 : 0;
}

int main()
{
    return run_presentation_find_replace_tests();
}
