#include <mirrorfly/presentation.hpp>
#include <mirrorfly/presentation_geometry.hpp>

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    int failures = 0;
    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
}

int run_presentation_geometry_tests()
{
    using namespace mirrorfly;
    const auto names = presentation_geometry_presets();
    check(names.size() == 187, "all pinned preset definitions available without Qt");
    auto authored = make_presentation(PresentationSlideLayout::Blank);
    for (const auto& name : names)
    {
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddShape;
        command.geometry = name;
        const auto result = apply_presentation_edit(authored, command);
        check(result.error == PresentationEditError::None, "advertised preset can be created: " + name);
        if (result.error == PresentationEditError::None)
            check(authored.slides.front().shapes.back().path_geometry != nullptr,
                "created preset has a renderable path before saving: " + name);
    }
    const auto authored_package = serialize_presentation(authored);
    const auto readback = parse_presentation(authored_package.parts);
    check(authored_package.error == PresentationError::None && readback.error == PresentationError::None &&
            readback.scene.slides.size() == 1 && readback.scene.slides.front().shapes.size() == names.size(),
        "all advertised presets survive one native PPTX round trip");
    if (readback.scene.slides.size() == 1 && readback.scene.slides.front().shapes.size() == names.size())
        for (std::size_t index = 0; index < names.size(); ++index)
            check(readback.scene.slides.front().shapes[index].geometry == names[index] &&
                    readback.scene.slides.front().shapes[index].path_geometry != nullptr,
                "preset identity and rendered geometry survive reopening: " + names[index]);
    PresentationEditCommand invalid;
    invalid.action = PresentationEditAction::AddShape;
    invalid.geometry = "madeUpGeometry";
    const auto previous_id = authored.next_shape_id;
    check(apply_presentation_edit(authored, invalid).error == PresentationEditError::InvalidValue &&
            authored.next_shape_id == previous_id && authored.slides.front().shapes.size() == names.size(),
        "unknown preset is rejected without inserting an object or consuming an ID");
    for (const auto& name : names)
        for (const auto size : {std::array<double, 2>{200, 100}, {100, 200}, {120, 120}, {1, 400}})
        {
            const auto result = presentation_geometry(name, size[0], size[1]);
            check(result.error.empty() && !result.paths.empty(), name + ": " + result.error);
            for (const auto& path : result.paths)
                for (const auto& command : path.commands)
                    for (double value : command.values)
                        check(std::isfinite(value), "finite preset coordinates: " + name);
        }
    const auto rect = presentation_geometry("rect", 200, 100);
    check(rect.paths.size() == 1 && rect.paths.front().commands.size() == 5,
        "rectangle retains four corners and close");
    check(rect.text_rect == std::array<double, 4>{0, 0, 200, 100}, "rectangle text area in points");
    const auto rounded = presentation_geometry("roundRect", 200, 100);
    const auto adjusted = presentation_geometry("roundRect", 200, 100,
        "<a:prstGeom prst='roundRect'><a:avLst><a:gd name='adj' fmla='val 40000'/></a:avLst></a:prstGeom>");
    check(rounded.error.empty() && adjusted.error.empty() &&
            adjusted.paths.front().commands.front().values != rounded.paths.front().commands.front().values,
        "imported adjustment overrides preset default");
    const auto custom = presentation_geometry("custom", 200, 100,
        "<custGeom><avLst/><gdLst><gd name='mid' fmla='*/ w 1 2'/></gdLst>"
        "<rect l='0' t='0' r='w' b='h'/><pathLst><path><moveTo><pt x='0' y='0'/></moveTo>"
        "<quadBezTo><pt x='mid' y='h'/><pt x='w' y='0'/></quadBezTo>"
        "<lnTo><pt x='w' y='h'/></lnTo><close/></path></pathLst></custGeom>");
    check(custom.error.empty() && custom.paths.front().commands.size() == 4 &&
            custom.paths.front().commands[1].action == PresentationPathAction::Quadratic,
        "custom curves and guide coordinates retained");
    const auto circle = presentation_geometry("ellipse", 100, 100);
    check(circle.error.empty() && circle.paths.front().commands.size() >= 6,
        "full elliptical arc converts into bounded cubic segments");
    const auto bad = presentation_geometry("custom", 100, 100,
        "<custGeom><gdLst><gd name='a' fmla='val missing'/></gdLst><pathLst/></custGeom>");
    check(!bad.error.empty() && bad.paths.empty(), "unresolved geometry fails without partial output");
    check(!presentation_geometry("rect", std::numeric_limits<double>::quiet_NaN(), 10).error.empty(),
        "nonfinite dimensions rejected");
    check(!presentation_geometry("rect", 100, 100, "<!DOCTYPE x><prstGeom/>").error.empty(),
        "DTD geometry rejected");
    check(!presentation_geometry("unknown", 10, 10).error.empty(), "unknown preset is reported");
    auto scene = make_presentation(PresentationSlideLayout::Blank);
    PresentationEditCommand add;
    add.action = PresentationEditAction::AddShape;
    add.geometry = "rect";
    check(apply_presentation_edit(scene, add).error == PresentationEditError::None, "shape fixture");
    auto package = serialize_presentation(scene);
    const std::string original = "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>";
    bool replaced = false;
    for (auto& part : package.parts)
    {
        const auto found = part.bytes.find(original);
        if (found != std::string::npos)
        {
            part.bytes.replace(found, original.size(),
                "<a:prstGeom prst='gear6'><a:avLst>"
                "<a:gd name='adj1' fmla='val 20000'/></a:avLst></a:prstGeom>");
            replaced = true;
        }
    }
    check(replaced, "replace native preset with imported gear and adjustment");
    auto imported = parse_presentation(package.parts);
    check(imported.error == PresentationError::None && !imported.scene.slides.empty(),
        "parse geometry fixture");
    if (!imported.scene.slides.empty() && !imported.scene.slides[0].shapes.empty())
    {
        const auto& shape = imported.scene.slides[0].shapes[0];
        check(shape.geometry == "gear6" && shape.path_geometry && !shape.path_geometry->paths.empty(),
            "complex imported preset retains actual path instead of rectangle");
        imported.scene.native_editable = true;
        const auto saved = serialize_presentation(imported.scene);
        const auto reopened = parse_presentation(saved.parts);
        check(saved.error == PresentationError::None && reopened.error == PresentationError::None &&
                reopened.scene.slides[0].shapes[0].geometry == "gear6" &&
                reopened.scene.slides[0].shapes[0].geometry_definition.find("20000") != std::string::npos,
            "geometry and adjustment survive serialization and reopening");
    }
    package = serialize_presentation(scene);
    for (auto& part : package.parts)
    {
        if (part.path == "ppt/slides/slide1.xml")
        {
            const auto found = part.bytes.find("<p:nvPr/>", part.bytes.find("<p:sp>"));
            check(found != std::string::npos, "media fixture nonvisual properties");
            if (found != std::string::npos)
                part.bytes.replace(found, 9, "<p:nvPr><a:videoFile r:link='video1'/></p:nvPr>");
        }
        if (part.path == "ppt/slides/_rels/slide1.xml.rels")
        {
            const auto found = part.bytes.find("</Relationships>");
            if (found != std::string::npos)
                part.bytes.insert(found,
                    "<Relationship Id='video1' Target='../media/movie.mp4' "
                    "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/video'/>");
        }
    }
    package.parts.push_back({"ppt/media/movie.mp4", "bounded-video-fixture"});
    const auto movie = parse_presentation(package.parts);
    check(movie.error == PresentationError::None && movie.scene.media.size() == 1,
        "embedded media relation resolves to shared scene resource");
    if (movie.scene.media.size() == 1)
        check(movie.scene.media[0].mime_type == "video/mp4" &&
                *movie.scene.media[0].bytes == "bounded-video-fixture" &&
                movie.scene.slides[0].shapes[0].media_path == movie.scene.media[0].path &&
                !movie.scene.slides[0].shapes[0].source_id.empty(),
            "media bytes, shape identity and relation survive parsing");
    return failures == 0 ? 0 : 1;
}

int main()
{
    return run_presentation_geometry_tests();
}
