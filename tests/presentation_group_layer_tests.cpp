#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <iostream>
#include <map>
#include <sstream>

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
        check(package.error == PresentationError::None, "group package serializes");
        for (const auto& part : package.parts)
            result.emplace(part.path, part.bytes);
        return result;
    }

    PresentationScene grouped_scene()
    {
        auto scene = make_presentation(PresentationSlideLayout::Title);
        PresentationEditCommand add;
        add.action = PresentationEditAction::AddShape;
        add.geometry = "rect";
        check(apply_presentation_edit(scene, add).error == PresentationEditError::None,
            "third shape added before group layer test");
        PresentationEditCommand group;
        group.action = PresentationEditAction::GroupAdjacent;
        group.shape_index = 0;
        group.target_index = 1;
        check(apply_presentation_edit(scene, group).error == PresentationEditError::None,
            "first two shapes grouped");
        return scene;
    }

    std::string group_id(const PresentationScene& scene)
    {
        return scene.slides[0].groups.empty() ? std::string{} : scene.slides[0].groups[0].source_id;
    }

    PresentationEditCommand reorder(const std::string& id, const std::string& position)
    {
        PresentationEditCommand command;
        command.action = PresentationEditAction::ReorderGroup;
        command.group_id = id;
        command.group_layer_position = position;
        return command;
    }

    void test_group_layers()
    {
        auto scene = grouped_scene();
        const auto id = group_id(scene);
        if (id.empty())
            return;
        check(!id.empty() && scene.slides[0].groups[0].layer_index == 0 &&
                scene.slides[0].groups[0].layer_count == 2,
            "group occupies one top-level layer");
        const auto options = presentation_group_layer_options(scene.slides[0].groups[0]);
        check(!options.back && !options.backward && options.forward && options.front,
            "group layer options match initial position");
        const auto before = parts(scene);
        check(apply_presentation_edit(scene, reorder(id, "forward")).error == PresentationEditError::None,
            "group moves forward as one layer");
        check(scene.slides[0].groups[0].layer_index == 1 &&
                scene.slides[0].shapes.back().source_groups.back() == id,
            "group members remain together after moving forward");
        const auto after = parts(scene);
        for (const auto& [path, bytes] : before)
            if (path != "ppt/slides/slide1.xml")
                check(after.at(path) == bytes, "group layer edit keeps unrelated package part byte exact");
        pugi::xml_document slide;
        check(slide.load_string(after.at("ppt/slides/slide1.xml").c_str()), "reordered slide XML parses");
        auto tree = slide.document_element().child("p:cSld").child("p:spTree");
        check(tree.child("p:grpSp").previous_sibling("p:sp") && !tree.child("p:grpSp").next_sibling("p:sp"),
            "whole group moves after ordinary shape in XML");
        const auto reopened = parse_presentation(serialize_presentation(scene).parts);
        check(
            reopened.error == PresentationError::None && reopened.scene.slides[0].groups[0].layer_index == 1,
            "group layer survives save and reopen");
        check(apply_presentation_edit(scene, reorder(id, "front")).error ==
                    PresentationEditError::InvalidValue &&
                parts(scene) == after,
            "unavailable group layer direction leaves source unchanged");
        check(apply_presentation_edit(scene, reorder(id, "back")).error == PresentationEditError::None &&
                scene.slides[0].groups[0].layer_index == 0,
            "group can move to bottom again");
    }

    void test_unknown_tree_locked()
    {
        const auto scene = grouped_scene();
        auto package = serialize_presentation(scene);
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            pugi::xml_document slide;
            slide.load_buffer(part.bytes.data(), part.bytes.size());
            auto tree = slide.document_element().child("p:cSld").child("p:spTree");
            tree.append_child("p:extLst");
            std::ostringstream output;
            slide.save(output);
            part.bytes = output.str();
        }
        auto imported = parse_presentation(package.parts);
        check(imported.error == PresentationError::None, "extended group tree parses");
        imported.scene.native_editable = true;
        const auto before = parts(imported.scene);
        const auto id = group_id(imported.scene);
        if (id.empty())
            return;
        check(!id.empty() && !presentation_group_layer_options(imported.scene.slides[0].groups[0]).front,
            "unknown shape-tree extension locks group layer controls");
        check(apply_presentation_edit(imported.scene, reorder(id, "front")).error !=
                    PresentationEditError::None &&
                parts(imported.scene) == before,
            "unknown shape-tree extension is preserved after rejected edit");
    }
}

int run_group_layer_tests()
{
    test_group_layers();
    test_unknown_tree_locked();
    return failures ? 1 : 0;
}

int main()
{
    return run_group_layer_tests();
}
