#include "presentation_table_fixture.hpp"

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

    void check(bool value, const char* label)
    {
        if (!value)
        {
            std::cerr << label << '\n';
            ++failures;
        }
    }

    PresentationScene reopen(const PresentationScene& scene)
    {
        const auto serialized = serialize_presentation(scene);
        auto parsed = parse_presentation(serialized.parts);
        check(serialized.error == PresentationError::None && parsed.error == PresentationError::None,
            "edited structure reopens");
        parsed.scene.native_editable = true;
        return parsed.scene;
    }

    std::map<std::string, std::string> parts(const PresentationScene& scene)
    {
        std::map<std::string, std::string> result;
        for (const auto& part : serialize_presentation(scene).parts)
            result.emplace(part.path, part.bytes);
        return result;
    }

    void table_edits()
    {
        auto parsed = parse_presentation(test_fixture::table_package());
        auto& scene = parsed.scene;
        PresentationEditCommand command;
        command.action = PresentationEditAction::UpdateText;
        command.text = "Edited merged cell";
        check(apply_presentation_edit(scene, command).error == PresentationEditError::ReadOnly,
            "source table protected before copy");
        scene.native_editable = true;
        const auto original = parts(scene);
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "edit merged table cell text");
        command.action = PresentationEditAction::FormatTextBox;
        command.inset_left = 8;
        command.vertical_alignment = "bottom";
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "table margins and anchor edit");
        command = {};
        command.action = PresentationEditAction::FormatTextStyle;
        command.text_style.shadow = PresentationShadowStyle{"#224466", 0.4, 2, 1, 2};
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "table text effect edit");
        const auto loaded = reopen(scene);
        const auto& cell = loaded.slides[0].shapes[0];
        check(cell.table_cell && cell.table_cell->row == 0 && cell.table_cell->column == 0 &&
                cell.text.paragraphs[0].runs[0].text == "Edited merged cell" && cell.text.inset_left == 8 &&
                cell.text.vertical_alignment == "bottom" &&
                cell.text.paragraphs[0].runs[0].effects.shadow_opacity == 0.4,
            "text and format survive table reload");
        const auto current = parts(scene);
        for (const auto& [path, value] : original)
            if (path != "ppt/slides/slide1.xml")
                check(current.at(path) == value, "cell edit preserves unrelated ZIP part");
        pugi::xml_document xml;
        xml.load_string(current.at("ppt/slides/slide1.xml").c_str());
        const auto table = xml.child("p:sld")
                               .child("p:cSld")
                               .child("p:spTree")
                               .child("p:graphicFrame")
                               .child("a:graphic")
                               .child("a:graphicData")
                               .child("a:tbl");
        check(table.child("a:tr").child("a:tc").attribute("gridSpan").as_int() == 2 &&
                table.child("a:tr").child("a:tc").next_sibling("a:tc").attribute("hMerge").as_bool(),
            "merged structure and continuation preserved");
        for (auto action : {PresentationEditAction::TransformShape, PresentationEditAction::DeleteShape,
                 PresentationEditAction::DuplicateShape, PresentationEditAction::FormatShape})
        {
            command = {};
            command.action = action;
            check(apply_presentation_edit(scene, command).error == PresentationEditError::ReadOnly &&
                    parts(scene) == current,
                "unsupported cell structural edit rejected atomically");
        }
    }

    void group_edits()
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddText;
        command.text = "Grouped";
        command.width = 90;
        command.height = 35;
        apply_presentation_edit(scene, command);
        auto package = serialize_presentation(scene);
        for (auto& part : package.parts)
            if (part.path == "ppt/slides/slide1.xml")
            {
                pugi::xml_document document;
                document.load_string(part.bytes.c_str());
                auto tree = document.child("p:sld").child("p:cSld").child("p:spTree");
                auto shape = tree.child("p:sp");
                pugi::xml_document group;
                group.load_string("<p:grpSp><p:nvGrpSpPr><p:cNvPr id='99' name='group'/></p:nvGrpSpPr>"
                                  "<p:grpSpPr><a:xfrm rot='1800000' flipH='1'><a:off x='1270000' y='254000'/>"
                                  "<a:ext cx='2540000' cy='1270000'/><a:chOff x='127000' y='127000'/>"
                                  "<a:chExt cx='5080000' cy='3810000'/></a:xfrm></p:grpSpPr></p:grpSp>");
                auto outer = tree.append_copy(group.document_element());
                pugi::xml_document nested;
                nested.load_string("<p:grpSp><p:nvGrpSpPr><p:cNvPr id='100' name='nested'/></p:nvGrpSpPr>"
                                   "<p:grpSpPr><a:xfrm rot='900000'><a:off x='127000' y='127000'/>"
                                   "<a:ext cx='1270000' cy='1270000'/><a:chOff x='0' y='0'/>"
                                   "<a:chExt cx='1270000' cy='1270000'/></a:xfrm></p:grpSpPr></p:grpSp>");
                outer.append_copy(nested.document_element()).append_copy(shape);
                tree.remove_child(shape);
                std::ostringstream output;
                document.save(output);
                part.bytes = output.str();
            }
        scene = parse_presentation(package.parts).scene;
        scene.native_editable = true;
        check(scene.slides[0].shapes[0].editable, "native group child exposes edits");
        const auto start_x = scene.slides[0].shapes[0].transform[4];
        const auto start_y = scene.slides[0].shapes[0].transform[5];
        command = {};
        command.action = PresentationEditAction::MoveGroup;
        command.group_id = "99";
        command.x = 12;
        command.y = -8;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "top-level group moves as one edit");
        check(std::abs(scene.slides[0].shapes[0].transform[4] - start_x - 12) < 0.002 &&
                std::abs(scene.slides[0].shapes[0].transform[5] - start_y + 8) < 0.002,
            "group movement updates descendant display coordinates");
        const auto moved = reopen(scene);
        check(std::abs(moved.slides[0].shapes[0].transform[4] - scene.slides[0].shapes[0].transform[4]) <
                    0.002 &&
                std::abs(moved.slides[0].shapes[0].transform[5] - scene.slides[0].shapes[0].transform[5]) <
                    0.002,
            "group movement survives save and reopen");
        const auto moved_parts = parts(scene);
        command.group_id = "100";
        check(apply_presentation_edit(scene, command).error == PresentationEditError::ReadOnly &&
                parts(scene) == moved_parts,
            "nested group movement is rejected atomically");
        const auto original = parts(scene);
        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.x = scene.slides[0].shapes[0].transform[4] + 13;
        command.y = scene.slides[0].shapes[0].transform[5] - 7;
        command.width = scene.slides[0].shapes[0].width + 8;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "move and resize inside rotated flipped nonuniform group");
        const auto loaded = reopen(scene);
        const auto& before = scene.slides[0].shapes[0];
        const auto& after = loaded.slides[0].shapes[0];
        for (std::size_t i = 0; i < 6; ++i)
            check(std::abs(before.transform[i] - after.transform[i]) < 0.002,
                "inverse group coordinates round trip");
        check(std::abs(before.width - after.width) < 0.002 && after.source_groups.size() == 2,
            "group dimensions and hierarchy preserved");
        command = {};
        command.action = PresentationEditAction::DuplicateShape;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                reopen(scene).slides[0].shapes.size() == 2,
            "duplicate remains in same group with unique source ID");
        command.action = PresentationEditAction::DeleteShape;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                reopen(scene).slides[0].shapes.size() == 1,
            "delete removes only the group child");
        const auto stable = parts(scene);
        command.action = PresentationEditAction::MoveShape;
        check(apply_presentation_edit(scene, command).error == PresentationEditError::ReadOnly &&
                parts(scene) == stable,
            "cross group ordering blocked by public capabilities");
        for (const auto& [path, value] : original)
            if (path != "ppt/slides/slide1.xml")
                check(stable.at(path) == value, "group edit preserves unrelated ZIP part");
    }

    int run_structure_tests()
    {
        table_edits();
        group_edits();
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_structure_tests();
}
