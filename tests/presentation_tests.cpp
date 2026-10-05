#include <mirrorfly/presentation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{

    int failures = 0;

    void expect(bool condition, const std::string& description)
    {
        if (!condition)
        {
            std::cerr << description << '\n';
            ++failures;
        }
    }

    bool near(double actual, double expected)
    {
        return std::abs(actual - expected) < 0.001;
    }

    struct Bounds
    {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
    };

    Bounds bounds(const mirrorfly::PresentationShape& shape)
    {
        const auto& matrix = shape.transform;
        const std::array<double, 4> x{matrix[4], matrix[0] * shape.width + matrix[4],
            matrix[2] * shape.height + matrix[4],
            matrix[0] * shape.width + matrix[2] * shape.height + matrix[4]};
        const std::array<double, 4> y{matrix[5], matrix[1] * shape.width + matrix[5],
            matrix[3] * shape.height + matrix[5],
            matrix[1] * shape.width + matrix[3] * shape.height + matrix[5]};
        return {*std::min_element(x.begin(), x.end()), *std::min_element(y.begin(), y.end()),
            *std::max_element(x.begin(), x.end()), *std::max_element(y.begin(), y.end())};
    }

    bool is_xml_part(const std::string& path)
    {
        const auto has_suffix = [&path](const std::string& suffix)
        {
            return path.size() >= suffix.size() &&
                path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        return has_suffix(".xml") || has_suffix(".rels");
    }

    std::string relationship(
        const std::string& id, const std::string& kind, const std::string& target, bool external = false)
    {
        return "<Relationship Id='" + id +
            "' Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/" + kind +
            "' Target='" + target + "'" + (external ? " TargetMode='External'" : "") + "/>";
    }

    std::string relations(const std::string& contents)
    {
        return "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>" +
            contents + "</Relationships>";
    }

    std::string slide(const std::string& contents, const std::string& extra = "")
    {
        return "<p:sld xmlns:p='urn:slides' xmlns:a='urn:drawing' "
               "xmlns:r='urn:relationships'><p:cSld><p:spTree>" +
            contents + "</p:spTree></p:cSld>" + extra + "</p:sld>";
    }

    std::string shape(
        const std::string& text, const std::string& x = "127000", const std::string& y = "254000")
    {
        return "<p:sp><p:nvSpPr><p:cNvPr id='1' name='Text'/><p:nvPr/></p:nvSpPr>"
               "<p:spPr><a:xfrm><a:off x='" +
            x + "' y='" + y +
            "'/><a:ext cx='2540000' cy='1270000'/></a:xfrm>"
            "<a:prstGeom prst='rect'/><a:solidFill><a:srgbClr val='B07040'/></a:solidFill></p:spPr>"
            "<p:txBody><a:bodyPr/><a:p><a:r><a:t>" +
            text + "</a:t></a:r></a:p></p:txBody></p:sp>";
    }

    std::vector<mirrorfly::PresentationPart> fixture(const std::string& content = "")
    {
        return {{"_rels/.rels", relations(relationship("entry", "officeDocument", "ppt/deck.xml"))},
            {"ppt/deck.xml",
                "<p:presentation xmlns:p='urn:slides' xmlns:r='urn:relationships'>"
                "<p:sldIdLst><p:sldId id='256' r:id='later'/><p:sldId id='257' r:id='first'/></p:sldIdLst>"
                "<p:sldSz cx='11430000' cy='7620000'/></p:presentation>"},
            {"ppt/_rels/deck.xml.rels",
                relations(relationship("first", "slide", "slides/one.xml") +
                    relationship("later", "slide", "slides/two.xml"))},
            {"ppt/slides/one.xml", slide(shape("First filename"))},
            {"ppt/slides/two.xml", slide(content.empty() ? shape("First visible") : content)}};
    }

    mirrorfly::PresentationPart& part(
        std::vector<mirrorfly::PresentationPart>& parts, const std::string& path)
    {
        for (auto& candidate : parts)
        {
            if (candidate.path == path)
            {
                return candidate;
            }
        }
        parts.push_back({path, {}});
        return parts.back();
    }

    void check_order_and_text()
    {
        const auto result = mirrorfly::parse_presentation(fixture());
        expect(result.error == mirrorfly::PresentationError::None, "basic package parses");
        if (result.scene.slides.size() != 2)
        {
            expect(false, "two slides are returned");
            return;
        }
        expect(near(result.scene.width, 900) && near(result.scene.height, 600), "slide dimensions use EMU");
        const auto& first = result.scene.slides[0].shapes.at(0);
        expect(first.text.paragraphs.at(0).runs.at(0).text == "First visible",
            "relationship order wins over names");
        expect(near(first.transform[4], 10) && near(first.transform[5], 20), "shape position in points");
        expect(near(first.width, 200) && near(first.height, 100), "shape dimensions in points");
        expect(first.fill.color == "#B07040", "solid fill is preserved");
        expect(mirrorfly::is_presentation_path("演示.PPTX"), "uppercase extension accepted");
        expect(!mirrorfly::is_presentation_path("legacy.ppt"), "legacy binary PPT is not accepted");
        expect(!mirrorfly::is_presentation_path(std::string("bad\0name.pptx", 13)), "NUL path rejected");
        expect(!mirrorfly::is_presentation_path("\xFF.pptx"),
            "invalid UTF-8 path rejected before platform decoding");
    }

    void check_saturation_modulation()
    {
        const std::string content =
            "<p:sp><p:nvSpPr><p:cNvPr id='8' name='Saturation'/><p:nvPr/></p:nvSpPr>"
            "<p:spPr><a:xfrm><a:off x='0' y='0'/><a:ext cx='1270000' cy='1270000'/></a:xfrm>"
            "<a:prstGeom prst='rect'/><a:solidFill><a:srgbClr val='FF0000'>"
            "<a:satMod val='50000'/></a:srgbClr></a:solidFill></p:spPr></p:sp>";
        const auto result = mirrorfly::parse_presentation(fixture(content));
        expect(result.error == mirrorfly::PresentationError::None && !result.scene.slides.empty() &&
                !result.scene.slides[0].shapes.empty(),
            "saturation modulation package parses");
        if (result.error == mirrorfly::PresentationError::None && !result.scene.slides.empty() &&
            !result.scene.slides[0].shapes.empty())
        {
            expect(result.scene.slides[0].shapes[0].fill.color == "#BF4040",
                "saturation modulation preserves hue and lightness");
            expect(std::none_of(result.scene.warnings.begin(), result.scene.warnings.end(),
                       [](const auto& warning)
            {
                return warning.find("color") != std::string::npos;
            }),
                "supported saturation modulation does not emit an approximation warning");
        }
    }

    void check_math_without_fallback()
    {
        const std::string content = "<mc:AlternateContent xmlns:mc='urn:compat' xmlns:a14='urn:drawing14' "
                                    "xmlns:m='urn:math'><mc:Choice Requires='a14'>"
                                    "<p:sp><p:nvSpPr><p:cNvPr id='8' name='Math'/><p:nvPr/></p:nvSpPr>"
                                    "<p:spPr><a:xfrm><a:off x='0' y='0'/><a:ext cx='2540000' cy='1270000'/>"
                                    "</a:xfrm><a:prstGeom prst='rect'/></p:spPr><p:txBody><a:bodyPr/><a:p>"
                                    "<a:r><a:t>f(x)=</a:t></a:r><a14:m><m:oMath><m:f><m:num><m:sSup>"
                                    "<m:e><m:r><m:t>x</m:t></m:r></m:e><m:sup><m:r><m:t>2</m:t></m:r></m:sup>"
                                    "</m:sSup></m:num><m:den><m:rad><m:deg/><m:e><m:r><m:t>y</m:t></m:r>"
                                    "</m:e></m:rad></m:den></m:f></m:oMath></a14:m>"
                                    "</a:p></p:txBody></p:sp></mc:Choice></mc:AlternateContent>";
        const auto result = mirrorfly::parse_presentation(fixture(content));
        expect(result.error == mirrorfly::PresentationError::None && !result.scene.slides.empty() &&
                result.scene.slides[0].shapes.size() == 1,
            "formula-only compatibility choice parses without a fallback image");
        if (result.error != mirrorfly::PresentationError::None || result.scene.slides.empty() ||
            result.scene.slides[0].shapes.empty())
        {
            return;
        }
        const auto& slide_result = result.scene.slides[0];
        const auto& runs = slide_result.shapes[0].text.paragraphs.at(0).runs;
        expect(runs.size() == 2 && runs[0].text == "f(x)=" && runs[1].text == "(x^(2))/(\xE2\x88\x9A(y))" &&
                runs[1].font_family == "Cambria Math",
            "Office Math receives a bounded readable text approximation");
        expect(std::find(slide_result.warnings.begin(), slide_result.warnings.end(),
                   "公式使用只读文字近似显示；原始公式结构保留。") != slide_result.warnings.end() &&
                std::find(slide_result.warnings.begin(), slide_result.warnings.end(),
                    "无兼容内容的扩展对象暂未显示。") == slide_result.warnings.end(),
            "formula approximation is reported without the generic missing-content warning");

        auto with_fallback = fixture(content.substr(0, content.rfind("</mc:AlternateContent>")) +
            "<mc:Fallback>" + shape("Fallback wins") + "</mc:Fallback></mc:AlternateContent>");
        const auto fallback = mirrorfly::parse_presentation(std::move(with_fallback));
        expect(fallback.error == mirrorfly::PresentationError::None && !fallback.scene.slides.empty() &&
                fallback.scene.slides[0].shapes.size() == 1 &&
                fallback.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text == "Fallback wins",
            "formula fallback remains preferred when the package provides one");

        auto preserved = result.scene;
        preserved.native_editable = true;
        const auto saved = mirrorfly::serialize_presentation(preserved);
        const auto source_xml = slide(content);
        const auto source_part = std::find_if(saved.parts.begin(), saved.parts.end(), [](const auto& part)
        {
            return part.path == "ppt/slides/two.xml";
        });
        expect(saved.error == mirrorfly::PresentationError::None && source_part != saved.parts.end() &&
                source_part->bytes == source_xml,
            "formula source XML remains byte exact in an unmodified editable copy");

        auto oversized = content;
        const auto value = oversized.find("<m:t>x</m:t>");
        oversized.replace(
            value, std::string("<m:t>x</m:t>").size(), "<m:t>" + std::string(70 * 1024, 'x') + "</m:t>");
        expect(
            mirrorfly::parse_presentation(fixture(oversized)).error == mirrorfly::PresentationError::TooLarge,
            "single-formula text budget rejects oversized approximations");
    }

    void check_inheritance()
    {
        const std::string title =
            "<p:sp><p:nvSpPr><p:cNvPr id='2' name='Title'/><p:nvPr><p:ph idx='7'/></p:nvPr>"
            "</p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/><a:p><a:pPr algn='ctr'/>"
            "<a:r><a:rPr i='1'/><a:t>继承标题</a:t></a:r></a:p></p:txBody></p:sp>";
        auto parts = fixture(title);
        parts.push_back({"ppt/slides/_rels/two.xml.rels",
            relations(relationship("layout", "slideLayout", "../layouts/l.xml"))});
        parts.push_back({"ppt/layouts/_rels/l.xml.rels",
            relations(relationship("master", "slideMaster", "../masters/m.xml"))});
        parts.push_back(
            {"ppt/masters/_rels/m.xml.rels", relations(relationship("theme", "theme", "../theme/t.xml"))});
        parts.push_back({"ppt/layouts/l.xml",
            "<p:sldLayout xmlns:p='urn:slides' xmlns:a='urn:drawing'><p:cSld><p:spTree>"
            "<p:sp><p:nvSpPr><p:nvPr><p:ph type='title' idx='7'/></p:nvPr></p:nvSpPr>"
            "<p:spPr><a:xfrm><a:off x='635000' y='381000'/><a:ext cx='8890000' cy='1524000'/>"
            "</a:xfrm><a:solidFill><a:schemeClr val='accent1'/></a:solidFill></p:spPr>"
            "<p:txBody><a:bodyPr lIns='127000'/><a:lstStyle><a:lvl1pPr><a:defRPr sz='2800'/>"
            "</a:lvl1pPr></a:lstStyle><a:p><a:r><a:t>Do not copy layout text</a:t></a:r></a:p>"
            "</p:txBody></p:sp></p:spTree></p:cSld></p:sldLayout>"});
        parts.push_back({"ppt/masters/m.xml",
            "<p:sldMaster xmlns:p='urn:slides' xmlns:a='urn:drawing'><p:cSld>"
            "<p:bg><p:bgPr><a:solidFill><a:schemeClr val='bg1'/></a:solidFill></p:bgPr></p:bg>"
            "<p:spTree>" +
                shape("Master decoration") +
                "<p:sp><p:nvSpPr><p:nvPr><p:ph type='title'/></p:nvPr></p:nvSpPr>"
                "<p:txBody><a:bodyPr/><a:p><a:r><a:t>Do not copy master text</a:t></a:r></a:p>"
                "</p:txBody></p:sp></p:spTree></p:cSld><p:clrMap bg1='lt1' tx1='dk1'/>"
                "<p:txStyles><p:titleStyle><a:lvl1pPr><a:defRPr sz='3200' b='1'>"
                "<a:solidFill><a:schemeClr val='tx1'/></a:solidFill><a:latin typeface='+mj-lt'/>"
                "<a:ea "
                "typeface='+mj-ea'/></a:defRPr></a:lvl1pPr></p:titleStyle></p:txStyles></p:sldMaster>"});
        parts.push_back({"ppt/theme/t.xml",
            "<a:theme xmlns:a='urn:drawing'><a:themeElements><a:clrScheme>"
            "<a:dk1><a:srgbClr val='242322'/></a:dk1><a:lt1><a:srgbClr val='FAF7F2'/></a:lt1>"
            "<a:accent1><a:srgbClr val='AD7548'/></a:accent1></a:clrScheme><a:fontScheme>"
            "<a:majorFont><a:latin typeface='Aptos Display'/><a:ea typeface='Microsoft YaHei'/>"
            "</a:majorFont><a:minorFont><a:latin typeface='Aptos'/></a:minorFont>"
            "</a:fontScheme></a:themeElements></a:theme>"});
        const auto result = mirrorfly::parse_presentation(parts);
        expect(result.error == mirrorfly::PresentationError::None, "theme package parses");
        if (result.scene.slides.empty() || result.scene.slides[0].shapes.size() != 2)
        {
            expect(false, "master decoration and slide content only; no instructional placeholders");
            return;
        }
        const auto& slide_result = result.scene.slides[0];
        const auto& inherited = slide_result.shapes.back();
        const auto& paragraph = inherited.text.paragraphs.at(0);
        const auto& run = paragraph.runs.at(0);
        expect(near(inherited.transform[4], 50) && near(inherited.width, 700), "layout geometry inherited");
        expect(inherited.fill.color == "#AD7548", "theme scheme fill inherited");
        expect(slide_result.background.color == "#FAF7F2", "master background and clrMap inherited");
        expect(run.text == "继承标题" && run.font_family == "Aptos Display", "actual text with theme font");
        expect(run.east_asian_font_family == "Microsoft YaHei", "theme East Asian family carried");
        expect(near(run.font_size, 28) && run.bold && run.italic,
            "layout size overrides master; run italic merges");
        expect(run.color == "#242322" && paragraph.alignment == "center", "text colors and local alignment");
        expect(near(inherited.text.inset_left, 10), "layout body properties inherited");
        expect(slide_result.title.find("继承标题") != std::string::npos, "title type inherited from layout");
        auto& slide_xml = part(parts, "ppt/slides/two.xml").bytes;
        const auto body_position = slide_xml.find("<p:txBody>");
        slide_xml.insert(
            body_position, "<p:style><a:fontRef idx='minor'><a:srgbClr val='FFFFFF'/></a:fontRef></p:style>");
        const auto styled = mirrorfly::parse_presentation(parts);
        expect(styled.error == mirrorfly::PresentationError::None, "font reference inheritance parses");
        if (!styled.scene.slides.empty() && !styled.scene.slides[0].shapes.empty())
        {
            const auto& styled_run = styled.scene.slides[0].shapes.back().text.paragraphs[0].runs[0];
            expect(styled_run.color == "#FFFFFF" && styled_run.font_family == "Aptos",
                "local font reference overrides master text defaults");
        }
        const auto properties_position = slide_xml.find("<a:rPr i='1'/>");
        slide_xml.replace(properties_position, std::string("<a:rPr i='1'/>").size(),
            "<a:rPr i='1'><a:solidFill><a:srgbClr val='FF0000'/></a:solidFill>"
            "<a:latin typeface='Arial'/></a:rPr>");
        const auto direct = mirrorfly::parse_presentation(parts);
        if (!direct.scene.slides.empty() && !direct.scene.slides[0].shapes.empty())
        {
            const auto& direct_run = direct.scene.slides[0].shapes.back().text.paragraphs[0].runs[0];
            expect(direct_run.color == "#FF0000" && direct_run.font_family == "Arial",
                "explicit run formatting overrides the font reference");
        }
    }

    void check_line_styles()
    {
        using namespace mirrorfly;
        auto autofit_scene = make_presentation(PresentationSlideLayout::Title);
        auto& text = autofit_scene.slides[0].shapes[0].text;
        text.auto_fit = true;
        text.font_scale = 0.925;
        text.line_spacing_reduction = 0.1;
        const auto autofit = parse_presentation(serialize_presentation(autofit_scene).parts);
        expect(autofit.error == PresentationError::None && autofit.scene.slides[0].shapes[0].text.auto_fit &&
                near(autofit.scene.slides[0].shapes[0].text.font_scale, 0.925) &&
                near(autofit.scene.slides[0].shapes[0].text.line_spacing_reduction, 0.1),
            "normal autofit and line spacing reduction survive serialization");
        const auto content =
            "<p:cxnSp><p:nvCxnSpPr><p:cNvPr id='9' name='Arrow'/><p:nvPr/></p:nvCxnSpPr>"
            "<p:spPr><a:xfrm><a:off x='127000' y='127000'/><a:ext cx='2540000' cy='1270000'/></a:xfrm>"
            "<a:prstGeom prst='line'/><a:ln w='38100' cap='rnd'><a:solidFill><a:srgbClr "
            "val='123456'/></a:solidFill>"
            "<a:prstDash val='lgDashDot'/><a:bevel/><a:headEnd type='oval' w='sm' len='lg'/>"
            "<a:tailEnd type='triangle' w='lg' len='med'/></a:ln>"
            "<a:effectLst><a:outerShdw blurRad='25400' dist='38100' dir='2700000'>"
            "<a:srgbClr val='223344'><a:alpha val='60000'/></a:srgbClr></a:outerShdw>"
            "<a:glow rad='12700'><a:srgbClr val='4488FF'/></a:glow>"
            "<a:reflection stA='35000' dist='12700'/></a:effectLst><a:scene3d/>"
            "</p:spPr></p:cxnSp>";
        auto source = fixture(content);
        for (auto& item : source)
            for (const auto& binding : std::vector<std::pair<std::string, std::string>>{
                     {"urn:slides", "http://schemas.openxmlformats.org/presentationml/2006/main"},
                     {"urn:drawing", "http://schemas.openxmlformats.org/drawingml/2006/main"},
                     {"urn:relationships",
                         "http://schemas.openxmlformats.org/officeDocument/2006/relationships"}})
            {
                std::size_t offset = 0;
                while ((offset = item.bytes.find(binding.first, offset)) != std::string::npos)
                {
                    item.bytes.replace(offset, binding.first.size(), binding.second);
                    offset += binding.second.size();
                }
            }
        auto parsed = parse_presentation(std::move(source));
        expect(parsed.error == PresentationError::None && !parsed.scene.slides.empty(),
            "connector style parses");
        if (parsed.scene.slides.empty() || parsed.scene.slides[0].shapes.empty())
            return;
        const auto& shape = parsed.scene.slides[0].shapes[0];
        expect(shape.line_style.head.type == "oval" && shape.line_style.head.length == "lg" &&
                shape.line_style.tail.type == "triangle" && shape.line_style.tail.width == "lg" &&
                shape.line_style.cap == "rnd" && shape.line_style.join == "bevel" &&
                shape.line_style.dashes == std::vector<double>({8, 3, 1, 3}) &&
                near(shape.effects.shadow_opacity, 0.6) && shape.effects.glow_color == "#4488FF" &&
                near(shape.effects.reflection_opacity, 0.35) && shape.approximate_3d,
            "line decoration, effects and bounded three-dimensional metadata are retained");
        auto native = make_presentation(PresentationSlideLayout::Blank);
        native.slides[0].shapes.push_back(shape);
        const auto roundtrip = parse_presentation(serialize_presentation(native).parts);
        expect(roundtrip.error == PresentationError::None &&
                roundtrip.scene.slides[0].shapes[0].line_style.dashes == shape.line_style.dashes &&
                roundtrip.scene.slides[0].shapes[0].line_style.tail.type == "triangle" &&
                near(roundtrip.scene.slides[0].shapes[0].effects.shadow_opacity, 0.6),
            "native serialization preserves line decoration, custom dashes and effects");
        parsed.scene.native_editable = true;
        PresentationEditCommand command;
        command.action = PresentationEditAction::FormatShape;
        command.shape_index = 0;
        command.outline_color = "#AA0000";
        expect(apply_presentation_edit(parsed.scene, command).error == PresentationEditError::None,
            "outline color can change without replacing original line definition");
        const auto edited = parse_presentation(serialize_presentation(parsed.scene).parts);
        expect(edited.error == PresentationError::None &&
                edited.scene.slides[0].shapes[0].line_style.head.type == "oval" &&
                edited.scene.slides[0].shapes[0].line_style.dashes == std::vector<double>({8, 3, 1, 3}),
            "imported outline edit preserves arrows and dash definition");
    }

    void check_content_placeholders_and_list_styles()
    {
        using namespace mirrorfly;
        const auto placeholder_shape = [](const std::string& type, int index, const std::string& body)
        {
            return "<p:sp><p:nvSpPr><p:cNvPr id='" + std::to_string(index + 20) + "'/><p:nvPr><p:ph type='" +
                type + "' idx='" + std::to_string(index) +
                "'/></p:nvPr></p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/>" + body + "</p:txBody></p:sp>";
        };
        auto parts = fixture(placeholder_shape("body", 1,
                                 "<a:p><a:r><a:t>Visible body</a:t></a:r></a:p><a:p><a:pPr lvl='1'/>"
                                 "<a:r><a:t>Second level</a:t></a:r></a:p>") +
            placeholder_shape("ftr", 11, "<a:p><a:r><a:t>Footer</a:t></a:r></a:p>"));
        parts.push_back({"ppt/slides/_rels/two.xml.rels",
            relations(relationship("layout", "slideLayout", "../layouts/layout.xml"))});
        parts.push_back({"ppt/layouts/_rels/layout.xml.rels",
            relations(relationship("master", "slideMaster", "../masters/master.xml"))});
        parts.push_back({"ppt/layouts/layout.xml",
            slide(placeholder_shape("obj", 1,
                      "<a:p><a:pPr lvl='0' marL='127000'/></a:p>"
                      "<a:p><a:pPr lvl='1' marL='381000'/></a:p>") +
                placeholder_shape("ftr", 11, "<a:p/>"))});
        std::string master_body = placeholder_shape("body", 7, "<a:p/>");
        const std::string geometry = "<p:spPr><a:xfrm><a:off x='254000' y='762000'/>"
                                     "<a:ext cx='5080000' cy='3810000'/></a:xfrm></p:spPr>";
        master_body.replace(master_body.find("<p:spPr/>"), 9, geometry);
        auto footer = placeholder_shape("ftr", 3, "<a:p/>");
        footer.replace(footer.find("<p:spPr/>"), 9, geometry);
        parts.push_back({"ppt/masters/master.xml",
            slide(master_body + footer,
                "<p:txStyles><p:bodyStyle><a:lvl1pPr><a:buChar char='•'/><a:defRPr sz='2800'/>"
                "</a:lvl1pPr><a:lvl2pPr><a:spcBef><a:spcPct val='20000'/></a:spcBef>"
                "<a:spcAft><a:spcPts val='500'/></a:spcAft><a:buClr><a:srgbClr val='FF0000'/>"
                "</a:buClr><a:buSzPct val='150000'/><a:buFont typeface='Arial'/><a:buChar char='•'/>"
                "<a:defRPr sz='2400'/></a:lvl2pPr></p:bodyStyle><p:otherStyle><a:lvl1pPr>"
                "<a:defRPr sz='1200'/></a:lvl1pPr></p:otherStyle></p:txStyles>")});
        auto parsed = parse_presentation(parts);
        expect(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 2,
            "content layout without geometry inherits the master body by type rather than index");
        if (parsed.error != PresentationError::None || parsed.scene.slides[0].shapes.size() != 2)
            return;
        const auto& body = parsed.scene.slides[0].shapes[0];
        const auto& second = body.text.paragraphs[1];
        expect(near(body.width, 400) && near(body.height, 300) && near(body.transform[5], 60),
            "missing slide and layout transforms use actual master geometry");
        expect(near(body.text.paragraphs[0].margin_left, 10) && near(second.margin_left, 30),
            "each list level inherits its matching placeholder paragraph");
        expect(near(second.space_before_percent, 0.2) && near(second.space_after, 5) &&
                second.bullet_color == "#FF0000" && second.bullet_font == "Arial" &&
                near(second.bullet_size_percent, 1.5),
            "relative paragraph spacing and independent bullet style parsed");
        const auto& footer_paragraph = parsed.scene.slides[0].shapes[1].text.paragraphs[0];
        expect(footer_paragraph.bullet.empty() && near(footer_paragraph.runs[0].font_size, 12),
            "footer uses other text style instead of body bullets");
        parsed.scene.source_package.reset();
        parsed.scene.native_editable = true;
        const auto reopened = parse_presentation(serialize_presentation(parsed.scene).parts);
        expect(reopened.error == PresentationError::None, "independent bullet style native roundtrip");
        if (reopened.error == PresentationError::None)
        {
            const auto& saved = reopened.scene.slides[0].shapes[0].text.paragraphs[1];
            expect(near(saved.space_before_percent, 0.2) && near(saved.space_after, 5) &&
                    saved.bullet_color == "#FF0000" && saved.bullet_font == "Arial" &&
                    near(saved.bullet_size_percent, 1.5),
                "native save preserves relative spacing and bullet styling");
        }
    }

    void check_numbering_formats_and_tabs()
    {
        const std::string content =
            "<p:sp><p:nvSpPr><p:cNvPr id='12' name='Numbering'/><p:nvPr/></p:nvSpPr><p:spPr>"
            "<a:xfrm><a:off x='0' y='0'/><a:ext cx='2540000' cy='1270000'/></a:xfrm></p:spPr>"
            "<p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:pPr marL='514350' indent='-514350'>"
            "<a:buAutoNum type='arabicParenR' startAt='2'/><a:tabLst><a:tab pos='911225' algn='l'/>"
            "</a:tabLst></a:pPr><a:r><a:t>Second item</a:t></a:r></a:p></p:txBody></p:sp>";
        auto result = mirrorfly::parse_presentation(fixture(content));
        expect(result.error == mirrorfly::PresentationError::None && !result.scene.slides.empty() &&
                !result.scene.slides[0].shapes.empty(),
            "numbering format package parses");
        if (result.error != mirrorfly::PresentationError::None || result.scene.slides.empty() ||
            result.scene.slides[0].shapes.empty())
        {
            return;
        }
        const auto& paragraph = result.scene.slides[0].shapes[0].text.paragraphs[0];
        expect(paragraph.numbered && paragraph.number_start == 2 && paragraph.number_format == "arabicParenR",
            "right-parenthesis numbering survives parsing");
        expect(std::none_of(result.scene.slides[0].warnings.begin(), result.scene.slides[0].warnings.end(),
                   [](const auto& warning)
        {
            return warning.find("制表位") != std::string::npos;
        }),
            "unused custom tab stops do not emit a display warning");
        result.scene.source_package.reset();
        result.scene.native_editable = true;
        const auto reopened =
            mirrorfly::parse_presentation(mirrorfly::serialize_presentation(result.scene).parts);
        expect(reopened.error == mirrorfly::PresentationError::None &&
                reopened.scene.slides[0].shapes[0].text.paragraphs[0].number_format == "arabicParenR",
            "right-parenthesis numbering survives editable-copy roundtrip");

        auto tabbed = fixture(content);
        auto& xml = part(tabbed, "ppt/slides/two.xml").bytes;
        const auto text = xml.find("<a:r><a:t>Second item</a:t></a:r>");
        xml.replace(text, std::string("<a:r><a:t>Second item</a:t></a:r>").size(),
            "<a:r><a:t>Second</a:t></a:r><a:tab/><a:r><a:t>item</a:t></a:r>");
        const auto with_tab = mirrorfly::parse_presentation(std::move(tabbed));
        expect(with_tab.error == mirrorfly::PresentationError::None &&
                with_tab.scene.slides[0].shapes[0].text.paragraphs[0].runs[1].text == "\t",
            "DrawingML tab characters remain visible to the renderer");
        expect(std::any_of(with_tab.scene.slides[0].warnings.begin(), with_tab.scene.slides[0].warnings.end(),
                   [](const auto& warning)
        {
            return warning.find("制表位") != std::string::npos;
        }),
            "used custom tab stops retain an honest approximation warning");
    }

    void check_group_images_and_paragraphs()
    {
        std::string content = "<p:grpSp><p:grpSpPr><a:xfrm><a:off x='127000' y='254000'/>"
                              "<a:ext cx='2540000' cy='3810000'/><a:chOff x='0' y='0'/>"
                              "<a:chExt cx='1270000' cy='1270000'/></a:xfrm></p:grpSpPr>" +
            shape("Grouped") +
            "</p:grpSp>"
            "<p:pic><p:nvPicPr/><p:spPr><a:xfrm><a:off x='0' y='0'/><a:ext cx='1270000' cy='1270000'/>"
            "</a:xfrm></p:spPr><p:blipFill><a:blip r:embed='image'><a:alphaModFix amt='50000'/>"
            "<a:extLst><a:ext><a14:useLocalDpi val='0'/></a:ext></a:extLst></a:blip>"
            "<a:srcRect l='10000' b='20000'/>"
            "<a:stretch/></p:blipFill></p:pic>"
            "<p:sp><p:spPr><a:xfrm><a:off x='0' y='0'/><a:ext cx='2540000' cy='2540000'/></a:xfrm>"
            "<a:gradFill><a:gsLst><a:gs pos='0'><a:srgbClr val='FF0000'/></a:gs>"
            "<a:gs pos='100000'><a:srgbClr val='0000FF'><a:alpha val='50000'/></a:srgbClr></a:gs>"
            "</a:gsLst><a:lin ang='5400000'/></a:gradFill></p:spPr><p:txBody><a:bodyPr anchor='ctr'/>"
            "<a:p><a:pPr marL='254000' indent='-127000' algn='r'><a:buChar char='•'/>"
            "<a:lnSpc><a:spcPct val='125000'/></a:lnSpc><a:spcAft><a:spcPts val='600'/></a:spcAft>"
            "<a:defRPr sz='2400'/></a:pPr><a:r><a:t>A&#160;B</a:t></a:r><a:br/>"
            "<a:r><a:rPr b='1'/><a:t>bold</a:t></a:r></a:p></p:txBody></p:sp>";
        auto parts = fixture(content);
        parts.push_back({"ppt/slides/_rels/two.xml.rels",
            relations(relationship("image", "image", "../media/picture.png"))});
        parts.push_back({"ppt/media/picture.png", "image-bytes"});
        const auto result = mirrorfly::parse_presentation(std::move(parts));
        expect(result.error == mirrorfly::PresentationError::None, "group and image package parses");
        if (result.scene.slides.empty() || result.scene.slides[0].shapes.size() != 3)
        {
            expect(false, "group is flattened in draw order");
            return;
        }
        const auto& shapes = result.scene.slides[0].shapes;
        expect(near(shapes[0].transform[0], 1) && near(shapes[0].transform[3], 1) &&
                near(shapes[0].width, 400) && near(shapes[0].height, 300),
            "group child coordinates normalized to physical points");
        expect(near(shapes[0].transform[4], 30) && near(shapes[0].transform[5], 80), "group child offset");
        expect(shapes[1].image_path == "ppt/media/picture.png", "image relation resolves within package");
        expect(near(shapes[1].image_crop[0], 0.1) && near(shapes[1].image_crop[3], 0.2),
            "picture crop fractions");
        expect(near(shapes[1].image_opacity, 0.5), "picture alpha modulation is preserved");
        expect(std::none_of(result.scene.slides[0].warnings.begin(), result.scene.slides[0].warnings.end(),
                   [](const auto& warning)
        {
            return warning.find("图片平铺") != std::string::npos;
        }),
            "supported picture opacity and local-DPI metadata do not emit approximation warnings");
        expect(result.scene.images.size() == 1 && result.scene.images[0].bytes &&
                *result.scene.images[0].bytes == "image-bytes",
            "shared binary image");
        expect(shapes[2].fill.stops.size() == 2 && near(shapes[2].fill.angle_degrees, 90),
            "linear gradient parsed");
        expect(near(shapes[2].fill.stops[1].opacity, 0.5), "gradient stop opacity");
        const auto& paragraph = shapes[2].text.paragraphs[0];
        expect(paragraph.bullet == "•" && paragraph.alignment == "right", "bullet and alignment");
        expect(
            near(paragraph.margin_left, 20) && near(paragraph.first_line_indent, -10), "paragraph indents");
        expect(near(paragraph.line_spacing, 1.25) && near(paragraph.space_after, 6), "paragraph spacing");
        expect(paragraph.runs[0].text ==
                    "A\xC2\xA0"
                    "B" &&
                paragraph.runs[1].text == "\n",
            "text whitespace preserved");
        expect(paragraph.runs[2].bold && near(paragraph.runs[2].font_size, 24),
            "paragraph defaults merge with runs");
        auto editable = result.scene;
        editable.source_package.reset();
        editable.native_editable = true;
        const auto reopened =
            mirrorfly::parse_presentation(mirrorfly::serialize_presentation(editable).parts);
        expect(reopened.error == mirrorfly::PresentationError::None &&
                near(reopened.scene.slides[0].shapes[1].image_opacity, 0.5),
            "picture opacity survives editable-copy roundtrip");
    }

    void check_svg_package_image()
    {
        const std::string content = "<p:pic><p:nvPicPr/><p:spPr><a:xfrm><a:off x='0' y='0'/>"
                                    "<a:ext cx='1270000' cy='1270000'/></a:xfrm></p:spPr>"
                                    "<p:blipFill><a:blip r:embed='vector'/><a:stretch/></p:blipFill></p:pic>";
        auto parts = fixture(content);
        parts.push_back({"ppt/slides/_rels/two.xml.rels",
            relations(relationship("vector", "image", "../media/vector.svg"))});
        parts.push_back({"ppt/media/vector.svg",
            "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'>"
            "<rect width='8' height='8' fill='#B43C1E'/></svg>"});
        const auto result = mirrorfly::parse_presentation(std::move(parts));
        expect(result.error == mirrorfly::PresentationError::None && result.scene.images.size() == 1 &&
                result.scene.images[0].path == "ppt/media/vector.svg" &&
                result.scene.images[0].mime_type == "image/svg+xml" &&
                result.scene.slides[0].shapes.size() == 1 &&
                result.scene.slides[0].shapes[0].image_path == "ppt/media/vector.svg",
            "package SVG relation reaches the public scene as a typed shared image");
    }

    void check_picture_bullets()
    {
        const std::string content =
            "<p:sp><p:nvSpPr><p:cNvPr id='14' name='Picture bullet'/><p:nvPr/></p:nvSpPr><p:spPr>"
            "<a:xfrm><a:off x='0' y='0'/><a:ext cx='2540000' cy='1270000'/></a:xfrm></p:spPr>"
            "<p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:pPr marL='381000' indent='-254000'>"
            "<a:buBlip><a:blip r:embed='bullet'/></a:buBlip></a:pPr>"
            "<a:r><a:t>Picture bullet</a:t></a:r></a:p></p:txBody></p:sp>";
        auto parts = fixture(content);
        parts.push_back({"ppt/slides/_rels/two.xml.rels",
            relations(relationship("bullet", "image", "../media/bullet.png"))});
        parts.push_back({"ppt/media/bullet.png", "bullet-image"});
        auto result = mirrorfly::parse_presentation(std::move(parts));
        expect(result.error == mirrorfly::PresentationError::None && !result.scene.slides.empty() &&
                !result.scene.slides[0].shapes.empty(),
            "picture bullet package parses");
        if (result.error != mirrorfly::PresentationError::None || result.scene.slides.empty() ||
            result.scene.slides[0].shapes.empty())
        {
            return;
        }
        const auto& paragraph = result.scene.slides[0].shapes[0].text.paragraphs[0];
        expect(paragraph.bullet_image_path == "ppt/media/bullet.png" && paragraph.bullet.empty() &&
                result.scene.images.size() == 1,
            "picture bullet resolves through the slide relationship");
        result.scene.source_package.reset();
        result.scene.native_editable = true;
        const auto reopened =
            mirrorfly::parse_presentation(mirrorfly::serialize_presentation(result.scene).parts);
        expect(reopened.error == mirrorfly::PresentationError::None && !reopened.scene.slides.empty() &&
                !reopened.scene.slides[0].shapes.empty() &&
                reopened.scene.slides[0].shapes[0].text.paragraphs[0].bullet_image_path ==
                    "ppt/media/image1.png",
            "picture bullet survives editable-copy roundtrip");
    }

    void check_metafile_package_images()
    {
        const std::string content = "<p:pic><p:nvPicPr/><p:spPr><a:xfrm><a:off x='0' y='0'/>"
                                    "<a:ext cx='1270000' cy='1270000'/></a:xfrm></p:spPr>"
                                    "<p:blipFill><a:blip r:embed='emf'/><a:stretch/></p:blipFill></p:pic>"
                                    "<p:pic><p:nvPicPr/><p:spPr><a:xfrm><a:off x='1270000' y='0'/>"
                                    "<a:ext cx='1270000' cy='1270000'/></a:xfrm></p:spPr>"
                                    "<p:blipFill><a:blip r:embed='wmf'/><a:stretch/></p:blipFill></p:pic>";
        auto parts = fixture(content);
        parts.push_back({"ppt/slides/_rels/two.xml.rels",
            relations(relationship("emf", "image", "../media/vector.emf") +
                relationship("wmf", "image", "../media/vector.wmf"))});
        parts.push_back({"ppt/media/vector.emf", "emf-binary"});
        parts.push_back({"ppt/media/vector.wmf", "wmf-binary"});
        const auto result = mirrorfly::parse_presentation(std::move(parts));
        expect(result.error == mirrorfly::PresentationError::None && result.scene.images.size() == 2 &&
                result.scene.images[0].mime_type == "image/x-emf" &&
                result.scene.images[1].mime_type == "image/x-wmf" &&
                result.scene.slides[0].shapes.size() == 2,
            "package EMF and WMF relationships reach the public scene with typed resources");
        auto editable_copy = result.scene;
        editable_copy.native_editable = true;
        const auto saved = mirrorfly::serialize_presentation(editable_copy);
        bool emf_preserved = false;
        bool wmf_preserved = false;
        for (const auto& part : saved.parts)
        {
            emf_preserved =
                emf_preserved || (part.path == "ppt/media/vector.emf" && part.bytes == "emf-binary");
            wmf_preserved =
                wmf_preserved || (part.path == "ppt/media/vector.wmf" && part.bytes == "wmf-binary");
        }
        expect(saved.error == mirrorfly::PresentationError::None && emf_preserved && wmf_preserved,
            "unmodified imported EMF and WMF parts remain byte exact when copied");
    }

    void check_slide_transitions()
    {
        auto parts = fixture();
        part(parts, "ppt/slides/two.xml").bytes = slide(shape("Transition"),
            "<p:transition spd='slow' dur='750' advClick='0' advTm='2500'><p:push dir='l'/>"
            "</p:transition>");
        part(parts, "ppt/slides/one.xml").bytes =
            slide(shape("Approximate"), "<p:transition spd='fast'><p:wheel spokes='4'/></p:transition>");
        const auto result = mirrorfly::parse_presentation(std::move(parts));
        expect(result.error == mirrorfly::PresentationError::None && result.scene.slides.size() == 2,
            "slide transition package parses");
        if (result.scene.slides.size() != 2)
        {
            return;
        }
        const auto& push = result.scene.slides[0].transition;
        expect(push.type == "push" && push.direction == "l" && near(push.duration, 0.75) &&
                !push.advance_on_click && near(push.advance_after, 2.5) && !push.approximate,
            "push transition retains timing, direction and advance rules");
        const auto& wheel = result.scene.slides[1].transition;
        expect(wheel.type == "wheel" && near(wheel.duration, 0.3) && wheel.approximate &&
                !result.scene.slides[1].warnings.empty(),
            "unsupported transition metadata is retained and explicitly marked approximate");

        parts = fixture();
        part(parts, "ppt/slides/two.xml").bytes = slide(shape("Compatibility transition"),
            "<mc:AlternateContent xmlns:mc='urn:compat' xmlns:p14='urn:presentation14'>"
            "<mc:Choice Requires='p14'><p:transition p14:dur='0' advTm='7780'/></mc:Choice>"
            "<mc:Fallback><p:transition advTm='7780'/></mc:Fallback></mc:AlternateContent>");
        const auto compatibility = mirrorfly::parse_presentation(std::move(parts));
        expect(compatibility.error == mirrorfly::PresentationError::None &&
                compatibility.scene.slides.size() == 2 &&
                compatibility.scene.slides[0].transition.type == "cut" &&
                near(compatibility.scene.slides[0].transition.duration, 0) &&
                near(compatibility.scene.slides[0].transition.advance_after, 7.78),
            "p14 compatibility transitions and automatic advance timing are parsed");
    }

    void check_embedded_font_metadata()
    {
        auto parts = fixture();
        auto& presentation = part(parts, "ppt/deck.xml").bytes;
        const auto end = presentation.rfind("</p:presentation>");
        presentation.insert(end,
            "<p:embeddedFontLst><p:embeddedFont><p:font typeface='Mirrorfly Embedded'/>"
            "<p:regular r:id='font1'/></p:embeddedFont></p:embeddedFontLst>");
        auto& presentation_relations = part(parts, "ppt/_rels/deck.xml.rels").bytes;
        const auto relationship_end = presentation_relations.rfind("</Relationships>");
        presentation_relations.insert(relationship_end, relationship("font1", "font", "fonts/font1.fntdata"));
        part(parts, "ppt/fonts/font1.fntdata").bytes = "embedded-font-data";
        const auto result = mirrorfly::parse_presentation(std::move(parts));
        expect(result.error == mirrorfly::PresentationError::None &&
                result.scene.embedded_fonts.size() == 1 &&
                result.scene.embedded_fonts[0].family == "Mirrorfly Embedded" &&
                result.scene.embedded_fonts[0].style == "regular" && result.scene.embedded_fonts[0].bytes &&
                *result.scene.embedded_fonts[0].bytes == "embedded-font-data",
            "embedded font family, style, relationship and bytes reach the public scene");
        auto copy = result.scene;
        copy.native_editable = true;
        const auto saved = mirrorfly::serialize_presentation(copy);
        const auto font = std::find_if(saved.parts.begin(), saved.parts.end(), [](const auto& candidate)
        {
            return candidate.path == "ppt/fonts/font1.fntdata";
        });
        expect(saved.error == mirrorfly::PresentationError::None && font != saved.parts.end() &&
                font->bytes == "embedded-font-data",
            "editable copy preserves embedded font bytes exactly");
    }

    void check_internal_hyperlinks()
    {
        using namespace mirrorfly;
        auto content = shape("Bookmark label");
        const auto properties = content.find("<a:r>");
        content.insert(properties + 5, "<a:rPr><a:hlinkClick r:id='anchor'/></a:rPr>");
        for (const auto* target : {"#_ftn1", "#", "#chapter%20one", "#part/section?x=1", "one.xml#_ftnref1",
                 "../slides/one.xml#bookmark"})
        {
            auto parts = fixture(content);
            parts.push_back(
                {"ppt/slides/_rels/two.xml.rels", relations(relationship("anchor", "hyperlink", target))});
            for (auto& item : parts)
                for (const auto& binding : std::vector<std::pair<std::string, std::string>>{
                         {"urn:slides", "http://schemas.openxmlformats.org/presentationml/2006/main"},
                         {"urn:drawing", "http://schemas.openxmlformats.org/drawingml/2006/main"},
                         {"urn:relationships",
                             "http://schemas.openxmlformats.org/officeDocument/2006/relationships"}})
                {
                    std::size_t offset = 0;
                    while ((offset = item.bytes.find(binding.first, offset)) != std::string::npos)
                    {
                        item.bytes.replace(offset, binding.first.size(), binding.second);
                        offset += binding.second.size();
                    }
                }
            auto loaded = parse_presentation(parts);
            expect(loaded.error == PresentationError::None, "internal hyperlink fragment permits loading");
            if (loaded.error != PresentationError::None)
                continue;
            expect(loaded.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text == "Bookmark label" &&
                    loaded.scene.slides[0].shapes[0].click_action.kind.empty() &&
                    std::any_of(loaded.scene.slides[0].warnings.begin(),
                        loaded.scene.slides[0].warnings.end(),
                        [](const auto& warning)
            {
                return warning.find("文字锚点") != std::string::npos;
            }),
                "bookmark label remains visible; unsupported anchor jump is reported, not invented");
            loaded.scene.native_editable = true;
            auto unchanged = serialize_presentation(loaded.scene);
            expect(unchanged.error == PresentationError::None &&
                    part(unchanged.parts, "ppt/slides/_rels/two.xml.rels").bytes == parts.back().bytes,
                "untouched bookmark relationship bytes are preserved");
            PresentationEditCommand edit;
            edit.action = PresentationEditAction::FormatText;
            edit.slide_index = 0;
            edit.shape_index = 0;
            edit.font_size = 24;
            expect(apply_presentation_edit(loaded.scene, edit).error == PresentationEditError::None,
                "bookmark text can be formatted through the public edit interface");
            auto saved = serialize_presentation(loaded.scene);
            expect(saved.error == PresentationError::None &&
                    part(saved.parts, "ppt/slides/_rels/two.xml.rels").bytes == parts.back().bytes &&
                    parse_presentation(saved.parts).error == PresentationError::None &&
                    part(saved.parts, "ppt/slides/two.xml").bytes.find("hlinkClick") != std::string::npos,
                "formatting and reopening preserve the original bookmark link");
        }
        for (const auto* target : {"../../../escape.xml#anchor", "%2e%2e/%2e%2e/%2e%2e/escape#x",
                 "//server/share#x", "file:/secret#x", "one.xml%23anchor", "one%2f.xml#x", "%+A.xml#x",
                 "#bad%GG", "#bad%1", "#bad\\anchor", "#bad#anchor"})
        {
            auto parts = fixture(content);
            parts.push_back(
                {"ppt/slides/_rels/two.xml.rels", relations(relationship("anchor", "hyperlink", target))});
            expect(parse_presentation(std::move(parts)).error == PresentationError::InvalidPackage,
                "invalid hyperlink paths and malformed fragments remain rejected");
        }
        auto parts = fixture(content);
        parts.push_back(
            {"ppt/slides/_rels/two.xml.rels", relations(relationship("anchor", "image", "one.xml#anchor"))});
        expect(parse_presentation(std::move(parts)).error == PresentationError::InvalidPackage,
            "hyperlink fragment support does not loosen image resource paths");
    }

    void check_rejections_and_warnings()
    {
        auto parts = fixture();
        parts.push_back(parts.front());
        expect(mirrorfly::parse_presentation(std::move(parts)).error ==
                mirrorfly::PresentationError::InvalidPackage,
            "duplicate member rejected");
        parts = fixture();
        parts.push_back({"../escape.xml", "x"});
        expect(mirrorfly::parse_presentation(std::move(parts)).error ==
                mirrorfly::PresentationError::InvalidPackage,
            "unsafe member path rejected");
        parts = fixture();
        part(parts, "ppt/deck.xml").bytes = "<!DOCTYPE p [<!ENTITY x SYSTEM 'file:///secret'>]><p/>";
        expect(
            mirrorfly::parse_presentation(std::move(parts)).error == mirrorfly::PresentationError::InvalidXml,
            "DTD and external entities rejected");
        parts = fixture();
        part(parts, "ppt/slides/two.xml").bytes = "<bad";
        expect(
            mirrorfly::parse_presentation(std::move(parts)).error == mirrorfly::PresentationError::InvalidXml,
            "malformed required slide rejected");
        parts = fixture();
        part(parts, "ppt/_rels/deck.xml.rels").bytes =
            relations(relationship("later", "slide", "../../escape.xml"));
        expect(mirrorfly::parse_presentation(std::move(parts)).error ==
                mirrorfly::PresentationError::InvalidPackage,
            "relationship traversal outside package rejected");
        parts = fixture(shape("bad", "nan"));
        expect(
            mirrorfly::parse_presentation(std::move(parts)).error == mirrorfly::PresentationError::InvalidXml,
            "nonfinite coordinates rejected");
        parts = fixture();
        auto& oversized = part(parts, "ppt/deck.xml").bytes;
        oversized.append(mirrorfly::maximum_presentation_xml_bytes, ' ');
        expect(
            mirrorfly::parse_presentation(std::move(parts)).error == mirrorfly::PresentationError::TooLarge,
            "XML size limit");
        parts = fixture();
        std::string nested;
        for (int index = 0; index < 70; ++index)
        {
            nested += "<a>";
        }
        for (int index = 0; index < 70; ++index)
        {
            nested += "</a>";
        }
        part(parts, "ppt/slides/two.xml").bytes = nested;
        expect(
            mirrorfly::parse_presentation(std::move(parts)).error == mirrorfly::PresentationError::TooLarge,
            "XML depth limit");
        parts = fixture("<p:graphicFrame/>");
        part(parts, "ppt/slides/two.xml").bytes = slide("<p:graphicFrame/>", "<p:timing/>");
        const auto unsupported = mirrorfly::parse_presentation(std::move(parts));
        expect(unsupported.error == mirrorfly::PresentationError::None &&
                unsupported.scene.slides[0].warnings.size() >= 2 &&
                unsupported.scene.slides[0].animations.empty(),
            "unsupported objects reported; empty timing does not invent animations");
    }

    void check_editing_and_serialization()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Title);
        expect(scene.native_editable && near(scene.width, 960) && near(scene.height, 540) &&
                scene.slides.size() == 1 && scene.slides.front().shapes.size() == 2,
            "new presentation uses a populated 16:9 title layout");

        PresentationEditCommand command;
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 1;
        command.layout = PresentationSlideLayout::TitleContent;
        auto edited = apply_presentation_edit(scene, command);
        expect(edited.error == PresentationEditError::None && scene.slides.size() == 2 &&
                scene.slides[1].shapes.size() == 2,
            "title and content slide is inserted");

        command = {};
        command.action = PresentationEditAction::AddShape;
        command.slide_index = 1;
        command.geometry = "rightArrow";
        edited = apply_presentation_edit(scene, command);
        expect(edited.error == PresentationEditError::None && edited.shape_index.has_value(),
            "basic shape is added and selected");
        command = {};
        command.action = PresentationEditAction::FormatShape;
        command.slide_index = 1;
        command.shape_index = *edited.shape_index;
        command.fill_color = "";
        command.outline_width = 0;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes.back().fill.color.empty() &&
                near(scene.slides[1].shapes.back().outline_width, 0),
            "shape fill and outline can be removed");

        command = {};
        command.action = PresentationEditAction::FormatText;
        command.slide_index = 1;
        command.shape_index = 1;
        command.alignment = "center";
        command.bullet = true;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "whole-object paragraph formatting is applied");
        command = {};
        command.action = PresentationEditAction::UpdateText;
        command.slide_index = 1;
        command.shape_index = 1;
        command.text = u8"第一项\nSecond item";
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes[1].text.paragraphs.size() == 2 &&
                scene.slides[1].shapes[1].text.paragraphs.back().alignment == "center" &&
                scene.slides[1].shapes[1].text.paragraphs.back().bullet == u8"•",
            "editing text preserves object paragraph formatting for new lines");

        command = {};
        command.action = PresentationEditAction::FormatText;
        command.slide_index = 1;
        command.shape_index = 1;
        command.strike = true;
        command.character_spacing = 2.5;
        command.baseline = 0.3;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes[1].text.paragraphs[0].runs[0].strike &&
                near(scene.slides[1].shapes[1].text.paragraphs[0].runs[0].spacing, 2.5) &&
                near(scene.slides[1].shapes[1].text.paragraphs[0].runs[0].baseline, 0.3),
            "character spacing strike and baseline use the public text command");
        command = {};
        command.action = PresentationEditAction::FormatParagraph;
        command.slide_index = 1;
        command.shape_index = 1;
        command.alignment = "right";
        command.bullet = false;
        command.numbered = true;
        command.number_start = 3;
        command.paragraph_margin_left = 24;
        command.first_line_indent = -12;
        command.line_spacing = 1.5;
        command.space_before = 6;
        command.space_after = 9;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes[1].text.paragraphs[1].numbered &&
                scene.slides[1].shapes[1].text.paragraphs[1].number_start == 3 &&
                scene.slides[1].shapes[1].text.paragraphs[1].bullet.empty() &&
                near(scene.slides[1].shapes[1].text.paragraphs[1].margin_left, 24) &&
                near(scene.slides[1].shapes[1].text.paragraphs[1].first_line_indent, -12) &&
                near(scene.slides[1].shapes[1].text.paragraphs[1].line_spacing, 1.5) &&
                near(scene.slides[1].shapes[1].text.paragraphs[1].space_after, 9),
            "paragraph lists indents and spacing use a dedicated public command");
        command = {};
        command.action = PresentationEditAction::FormatTextBox;
        command.slide_index = 1;
        command.shape_index = 1;
        command.inset_left = 12;
        command.inset_right = 14;
        command.inset_top = 8;
        command.inset_bottom = 10;
        command.vertical_alignment = "bottom";
        command.wrap = false;
        command.auto_fit = true;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                near(scene.slides[1].shapes[1].text.inset_left, 12) &&
                near(scene.slides[1].shapes[1].text.inset_bottom, 10) &&
                scene.slides[1].shapes[1].text.vertical_alignment == "bottom" &&
                !scene.slides[1].shapes[1].text.wrap && scene.slides[1].shapes[1].text.auto_fit,
            "text box insets alignment wrapping and autofit use a dedicated public command");

        const std::size_t arrow_index = scene.slides[1].shapes.size() - 1;
        const auto arrow_id = scene.slides[1].shapes[arrow_index].id;
        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.slide_index = 1;
        command.shape_index = arrow_index;
        command.rotation = 90;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                near(scene.slides[1].shapes[arrow_index].transform[0], 0) &&
                near(scene.slides[1].shapes[arrow_index].transform[1], 1) &&
                near(scene.slides[1].shapes[arrow_index].transform[2], -1) &&
                near(scene.slides[1].shapes[arrow_index].transform[3], 0),
            "object rotation is applied through the public transform command");
        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.slide_index = 1;
        command.shape_index = arrow_index;
        command.flip_horizontal = true;
        command.flip_vertical = true;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                near(scene.slides[1].shapes[arrow_index].transform[0], 0) &&
                near(scene.slides[1].shapes[arrow_index].transform[1], -1) &&
                near(scene.slides[1].shapes[arrow_index].transform[2], 1) &&
                near(scene.slides[1].shapes[arrow_index].transform[3], 0),
            "horizontal and vertical flip toggles share the public transform command");
        command = {};
        command.action = PresentationEditAction::FormatShape;
        command.slide_index = 1;
        command.shape_index = arrow_index;
        command.gradient_start_color = "#FF0000";
        command.gradient_end_color = "#0000FF";
        command.gradient_angle = 45;
        command.fill_opacity = 0.7;
        command.outline_color = "#112233";
        command.outline_opacity = 0.6;
        command.outline_width = 3;
        command.line_dash = "dashDot";
        command.line_head = "oval";
        command.line_tail = "triangle";
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes[arrow_index].fill.stops.size() == 2 &&
                near(scene.slides[1].shapes[arrow_index].fill.opacity, 0.7) &&
                near(scene.slides[1].shapes[arrow_index].fill.angle_degrees, 45) &&
                scene.slides[1].shapes[arrow_index].line_style.dashes == std::vector<double>({4, 3, 1, 3}) &&
                scene.slides[1].shapes[arrow_index].line_style.head.type == "oval" &&
                scene.slides[1].shapes[arrow_index].line_style.tail.type == "triangle",
            "gradient opacity dash and arrow ends use the public shape command");
        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.slide_index = 1;
        command.shape_index = arrow_index;
        command.width = 480;
        command.preserve_aspect = true;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                near(scene.slides[1].shapes[arrow_index].width, 480) &&
                near(scene.slides[1].shapes[arrow_index].height, 270),
            "locked aspect resizing is enforced by the public transform command");
        command = {};
        command.action = PresentationEditAction::MoveShape;
        command.slide_index = 1;
        command.shape_index = arrow_index;
        command.target_index = 0;
        edited = apply_presentation_edit(scene, command);
        expect(edited.error == PresentationEditError::None && edited.shape_index == 0 &&
                scene.slides[1].shapes.front().id == arrow_id,
            "object can move directly to an explicit layer index");

        command = {};
        command.action = PresentationEditAction::AddImage;
        command.slide_index = 1;
        command.image_path = "editing-image";
        command.image_mime_type = "image/png";
        command.image_bytes = "editing-image-bytes";
        edited = apply_presentation_edit(scene, command);
        expect(edited.error == PresentationEditError::None && edited.shape_index.has_value(),
            "editable image is added before image formatting");
        const std::size_t image_index = *edited.shape_index;
        command = {};
        command.action = PresentationEditAction::FormatImage;
        command.slide_index = 1;
        command.shape_index = image_index;
        command.image_crop_left = 0.1;
        command.image_crop_top = 0.2;
        command.image_crop_right = 0.15;
        command.image_crop_bottom = 0.05;
        command.image_opacity = 0.6;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                near(scene.slides[1].shapes[image_index].image_crop[0], 0.1) &&
                near(scene.slides[1].shapes[image_index].image_crop[3], 0.05) &&
                near(scene.slides[1].shapes[image_index].image_opacity, 0.6),
            "image crop and opacity are applied through a dedicated command");
        const auto original_image_path = scene.slides[1].shapes[image_index].image_path;
        command = {};
        command.action = PresentationEditAction::ReplaceImage;
        command.slide_index = 1;
        command.shape_index = image_index;
        command.image_path = "replacement-image";
        command.image_mime_type = "image/jpeg";
        command.image_bytes = "replacement-image-bytes";
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides[1].shapes[image_index].image_path != original_image_path &&
                near(scene.slides[1].shapes[image_index].image_crop[0], 0.1) &&
                near(scene.slides[1].shapes[image_index].image_opacity, 0.6) && scene.images.size() == 1 &&
                scene.images.front().mime_type == "image/jpeg" && scene.images.front().bytes &&
                *scene.images.front().bytes == "replacement-image-bytes",
            "image replacement preserves object formatting and prunes an unreferenced old resource");

        auto package = serialize_presentation(scene);
        expect(package.error == PresentationError::None && package.parts.size() >= 18,
            "editable scene serializes to a complete bounded package");
        auto reparsed = parse_presentation(package.parts);
        bool image_edit_reopened = false;
        bool advanced_edit_reopened = false;
        if (reparsed.error == PresentationError::None && reparsed.scene.slides.size() > 1)
        {
            const auto& reopened_shapes = reparsed.scene.slides[1].shapes;
            const auto reopened_image =
                std::find_if(reopened_shapes.begin(), reopened_shapes.end(), [](const auto& shape)
            {
                return !shape.image_path.empty();
            });
            image_edit_reopened = reopened_image != reopened_shapes.end() &&
                near(reopened_image->image_crop[0], 0.1) && near(reopened_image->image_crop[3], 0.05) &&
                near(reopened_image->image_opacity, 0.6) && reparsed.scene.images.size() == 1 &&
                reparsed.scene.images.front().mime_type == "image/jpeg" &&
                reparsed.scene.images.front().bytes &&
                *reparsed.scene.images.front().bytes == "replacement-image-bytes";
            const auto reopened_arrow =
                std::find_if(reopened_shapes.begin(), reopened_shapes.end(), [](const auto& shape)
            {
                return shape.geometry == "rightArrow";
            });
            const auto reopened_text =
                std::find_if(reopened_shapes.begin(), reopened_shapes.end(), [](const auto& shape)
            {
                return shape.text.paragraphs.size() == 2;
            });
            advanced_edit_reopened = reopened_arrow != reopened_shapes.end() &&
                reopened_arrow->fill.stops.size() == 2 && near(reopened_arrow->fill.opacity, 0.7) &&
                reopened_arrow->line_style.dashes == std::vector<double>({4, 3, 1, 3}) &&
                reopened_arrow->line_style.tail.type == "triangle" &&
                reopened_text != reopened_shapes.end() && reopened_text->text.paragraphs[0].runs[0].strike &&
                near(reopened_text->text.paragraphs[0].runs[0].spacing, 2.5) &&
                reopened_text->text.paragraphs[0].numbered && near(reopened_text->text.inset_left, 12) &&
                !reopened_text->text.wrap && reopened_text->text.auto_fit;
        }
        expect(reparsed.error == PresentationError::None && reparsed.scene.slides.size() == 2 &&
                !reparsed.scene.native_editable && image_edit_reopened && advanced_edit_reopened,
            "serialized advanced edits parse again and reopened files remain read-only");

        scene.slides[0].shapes[0].transform[2] = 0.2;
        expect(serialize_presentation(scene).error == PresentationError::InvalidPackage,
            "sheared transforms are rejected instead of silently changing geometry");
    }

    void check_failure_atomicity_and_duplicate_budgets()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand seed;
        seed.action = PresentationEditAction::AddImage;
        seed.image_path = "seed-image";
        seed.image_mime_type = "image/png";
        seed.image_bytes = "seed-bytes";
        expect(apply_presentation_edit(scene, seed).error == PresentationEditError::None,
            "failure atomicity fixture adds an image");
        const auto original_id = scene.next_shape_id;
        const auto original_shape_count = scene.slides.front().shapes.size();
        const auto original_image_count = scene.images.size();
        const auto original_shape_id = scene.slides.front().shapes.front().id;
        const auto original_image_path = scene.slides.front().shapes.front().image_path;
        const auto original_resource = scene.images.front();
        const auto unchanged = [&scene, original_id, original_shape_count, original_image_count,
                                   original_shape_id, &original_image_path, &original_resource]()
        {
            return scene.next_shape_id == original_id &&
                scene.slides.front().shapes.size() == original_shape_count &&
                scene.images.size() == original_image_count &&
                scene.slides.front().shapes.front().id == original_shape_id &&
                scene.slides.front().shapes.front().image_path == original_image_path &&
                scene.images.front().path == original_resource.path &&
                scene.images.front().mime_type == original_resource.mime_type &&
                scene.images.front().bytes == original_resource.bytes;
        };

        PresentationEditCommand command;
        command.action = PresentationEditAction::AddText;
        command.text.assign(maximum_presentation_text_bytes + 1, 'x');
        expect(
            apply_presentation_edit(scene, command).error == PresentationEditError::TooLarge && unchanged(),
            "rejected text insertion leaves objects, IDs, and resources unchanged");

        command = {};
        command.action = PresentationEditAction::AddShape;
        command.geometry = "rect";
        command.width = 0;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                unchanged(),
            "rejected shape insertion leaves objects, IDs, and resources unchanged");

        command = {};
        command.action = PresentationEditAction::AddImage;
        command.image_path = "rejected-image";
        command.image_mime_type = "image/png";
        command.image_bytes = "rejected-bytes";
        command.height = 0;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                unchanged(),
            "rejected image insertion leaves objects, IDs, and resources unchanged");

        command = {};
        command.action = PresentationEditAction::FormatImage;
        command.image_crop_left = 0.7;
        command.image_crop_right = 0.4;
        const auto previous_crop = scene.slides.front().shapes.front().image_crop;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                scene.slides.front().shapes.front().image_crop == previous_crop,
            "rejected image crop leaves the image unchanged");

        command = {};
        command.action = PresentationEditAction::ReplaceImage;
        command.image_path = "invalid-replacement";
        command.image_mime_type = "application/octet-stream";
        command.image_bytes = "invalid";
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                unchanged(),
            "rejected image replacement leaves the shape and resource unchanged");

        command = {};
        command.action = PresentationEditAction::TransformShape;
        command.width = 200;
        command.height = 200;
        command.preserve_aspect = true;
        const auto previous_width = scene.slides.front().shapes.front().width;
        const auto previous_height = scene.slides.front().shapes.front().height;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::InvalidValue &&
                near(scene.slides.front().shapes.front().width, previous_width) &&
                near(scene.slides.front().shapes.front().height, previous_height),
            "ambiguous aspect-locked resize leaves the shape unchanged");

        auto retained_fill = scene;
        retained_fill.slides.front().background.image_path = original_image_path;
        command = {};
        command.action = PresentationEditAction::DeleteShape;
        expect(apply_presentation_edit(retained_fill, command).error == PresentationEditError::None &&
                retained_fill.slides.front().shapes.empty() && retained_fill.images.size() == 1,
            "resource pruning retains an image still used by a slide background");

        auto limited = make_presentation(PresentationSlideLayout::Blank);
        command = {};
        command.action = PresentationEditAction::AddText;
        command.text.assign(maximum_presentation_text_bytes / 2 + 1, 'x');
        expect(apply_presentation_edit(limited, command).error == PresentationEditError::None,
            "duplicate budget fixture adds bounded text");
        const auto limited_id = limited.next_shape_id;
        command = {};
        command.action = PresentationEditAction::DuplicateShape;
        expect(apply_presentation_edit(limited, command).error == PresentationEditError::TooLarge &&
                limited.slides.front().shapes.size() == 1 && limited.next_shape_id == limited_id,
            "duplicating a shape enforces total text budget without consuming an ID");
        command = {};
        command.action = PresentationEditAction::DuplicateSlide;
        expect(apply_presentation_edit(limited, command).error == PresentationEditError::TooLarge &&
                limited.slides.size() == 1 && limited.next_shape_id == limited_id,
            "duplicating a slide enforces total text budget without consuming IDs");
    }

    void check_xml_export_budget()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddText;
        command.text.assign(maximum_presentation_xml_bytes / 6, '&');
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "bounded escaped-text fixture is editable");
        const auto package = serialize_presentation(scene);
        bool xml_parts_fit = package.error == PresentationError::None;
        for (const auto& part : package.parts)
        {
            if (is_xml_part(part.path) && part.bytes.size() > maximum_presentation_xml_bytes)
            {
                xml_parts_fit = false;
            }
        }
        const auto reopened = parse_presentation(package.parts);
        expect(xml_parts_fit && reopened.error == PresentationError::None &&
                reopened.scene.slides.front().shapes.front().text.paragraphs.front().runs.front().text ==
                    command.text,
            "successful export keeps every XML part within the reader budget and reparses it");

        scene = make_presentation(PresentationSlideLayout::Blank);
        command.text.assign(maximum_presentation_xml_bytes / 5, '&');
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "XML expansion fixture remains below the raw text budget");
        const auto rejected = serialize_presentation(scene);
        expect(rejected.error == PresentationError::TooLarge && rejected.parts.empty(),
            "export rejects XML escaping expansion beyond the reader's per-part budget");
    }

    void check_alignment_hidden_and_work_layouts()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddShape;
        command.geometry = "rect";
        command.width = 100;
        command.height = 40;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "alignment fixture adds a shape");
        auto& shape = scene.slides.front().shapes.front();
        shape.transform = {0, 1, -1, 0, 300, 100};
        const auto original_linear_transform = std::array<double, 4>{
            shape.transform[0], shape.transform[1], shape.transform[2], shape.transform[3]};
        const auto original_width = shape.width;
        const auto original_height = shape.height;
        const std::pair<const char*, double> horizontal[]{
            {"left", 0}, {"center", scene.width / 2}, {"right", scene.width}};
        for (const auto& target : horizontal)
        {
            command = {};
            command.action = PresentationEditAction::AlignShape;
            command.alignment = target.first;
            expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
                "rotated shape accepts horizontal page alignment");
            const auto aligned = bounds(shape);
            const double actual = target.first == std::string("left") ? aligned.left
                : target.first == std::string("center")               ? (aligned.left + aligned.right) / 2
                                                                      : aligned.right;
            expect(near(actual, target.second), "horizontal alignment uses transformed bounds");
        }
        const std::pair<const char*, double> vertical[]{
            {"top", 0}, {"middle", scene.height / 2}, {"bottom", scene.height}};
        for (const auto& target : vertical)
        {
            command = {};
            command.action = PresentationEditAction::AlignShape;
            command.alignment = target.first;
            expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
                "rotated shape accepts vertical page alignment");
            const auto aligned = bounds(shape);
            const double actual = target.first == std::string("top") ? aligned.top
                : target.first == std::string("middle")              ? (aligned.top + aligned.bottom) / 2
                                                                     : aligned.bottom;
            expect(near(actual, target.second), "vertical alignment uses transformed bounds");
        }
        expect(std::array<double, 4>{shape.transform[0], shape.transform[1], shape.transform[2],
                   shape.transform[3]} == original_linear_transform &&
                near(shape.width, original_width) && near(shape.height, original_height),
            "page alignment preserves rotation, scale, and local size");

        command = {};
        command.action = PresentationEditAction::SetSlideHidden;
        command.hidden = true;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                scene.slides.front().hidden,
            "a page can be hidden without deleting it");
        command.hidden = false;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None &&
                !scene.slides.front().hidden,
            "a hidden page can be shown again");
        command.hidden = true;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "page can be hidden again for serialization");
        command = {};
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 1;
        command.layout = PresentationSlideLayout::Blank;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "second hidden-page fixture is inserted");
        command = {};
        command.action = PresentationEditAction::SetSlideHidden;
        command.slide_index = 1;
        command.hidden = true;
        expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
            "core permits all pages to be hidden");
        const auto reopened = parse_presentation(serialize_presentation(scene).parts);
        expect(reopened.error == PresentationError::None && reopened.scene.slides.size() == 2 &&
                reopened.scene.slides[0].hidden && reopened.scene.slides[1].hidden,
            "hidden page state survives export and reparse");

        const PresentationSlideLayout layouts[]{PresentationSlideLayout::ProjectStatus,
            PresentationSlideLayout::MeetingSummary, PresentationSlideLayout::Milestones};
        auto reports = make_presentation(PresentationSlideLayout::Blank);
        reports.width = 720;
        for (const auto layout : layouts)
        {
            command = {};
            command.action = PresentationEditAction::AddSlide;
            command.slide_index = reports.slides.size();
            command.layout = layout;
            expect(apply_presentation_edit(reports, command).error == PresentationEditError::None,
                "work-report layout inserts through the public edit command");
            const auto& report = reports.slides.back();
            expect(report.shapes.size() == 4 && report.title != "幻灯片",
                "work-report layout contains a title and three editable content objects");
            for (const auto& item : report.shapes)
            {
                expect(!item.text.paragraphs.empty() && item.transform[4] >= 0 && item.transform[5] >= 0 &&
                        item.transform[4] + item.width <= reports.width &&
                        item.transform[5] + item.height <= reports.height,
                    "work-report template objects are editable and scale to the slide");
            }
        }
        const auto report_package = serialize_presentation(reports);
        const auto reopened_reports = parse_presentation(report_package.parts);
        expect(report_package.error == PresentationError::None &&
                reopened_reports.error == PresentationError::None &&
                reopened_reports.scene.slides.size() == 4,
            "work-report layouts survive export and reparse");
    }

    void check_academic_layouts()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 720;
        const PresentationSlideLayout layouts[]{PresentationSlideLayout::ReportOutline,
            PresentationSlideLayout::ResearchPlan, PresentationSlideLayout::Comparison,
            PresentationSlideLayout::References, PresentationSlideLayout::Conclusion};
        for (const auto layout : layouts)
        {
            PresentationEditCommand command;
            command.action = PresentationEditAction::AddSlide;
            command.slide_index = scene.slides.size();
            command.layout = layout;
            expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
                "academic layout inserts through the public edit command");
            const auto& slide = scene.slides.back();
            expect(slide.shapes.size() >= 2 && slide.title != "幻灯片",
                "academic layouts contain editable headings and body objects");
            for (const auto& shape : slide.shapes)
            {
                expect(shape.transform[4] >= 0 && shape.transform[5] >= 0 &&
                        shape.transform[4] + shape.width <= scene.width &&
                        shape.transform[5] + shape.height <= scene.height,
                    "template objects fit a non-widescreen document");
            }
        }
        const auto package = serialize_presentation(scene);
        const auto reopened = parse_presentation(package.parts);
        expect(package.error == PresentationError::None && reopened.error == PresentationError::None &&
                reopened.scene.slides.size() == 6,
            "all academic layouts survive serialization and parsing");
        if (reopened.scene.slides.size() == scene.slides.size())
        {
            for (std::size_t index = 1; index < scene.slides.size(); ++index)
            {
                expect(reopened.scene.slides[index].shapes.size() == scene.slides[index].shapes.size() &&
                        reopened.scene.slides[index]
                                .shapes.front()
                                .text.paragraphs.front()
                                .runs.front()
                                .text == scene.slides[index].title,
                    "reopened academic pages retain their individual objects and heading text");
            }
        }
        auto limited = make_presentation(PresentationSlideLayout::Blank);
        limited.slides.front().shapes.resize(maximum_presentation_shapes - 3);
        PresentationEditCommand command;
        command.action = PresentationEditAction::AddSlide;
        command.slide_index = 1;
        command.layout = PresentationSlideLayout::ResearchPlan;
        const auto next_id = limited.next_shape_id;
        expect(apply_presentation_edit(limited, command).error == PresentationEditError::TooLarge &&
                limited.slides.size() == 1 && limited.next_shape_id == next_id,
            "templates validate their actual shape count before changing the document");
        limited = make_presentation();
        limited.slides.front().shapes.front().text.paragraphs.front().runs.front().text.assign(
            maximum_presentation_text_bytes, 'x');
        const auto next_text_id = limited.next_shape_id;
        expect(apply_presentation_edit(limited, command).error == PresentationEditError::TooLarge &&
                limited.slides.size() == 1 && limited.next_shape_id == next_text_id,
            "template text budgets are checked without consuming object IDs on failure");
    }

    void check_refined_templates()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 720;
        scene.height = 405;
        for (const auto layout :
            {PresentationSlideLayout::ResearchStudio, PresentationSlideLayout::EvidenceBoard,
                PresentationSlideLayout::ProjectDashboard, PresentationSlideLayout::DeliveryRoadmap})
        {
            PresentationEditCommand command;
            command.action = PresentationEditAction::AddSlide;
            command.layout = layout;
            command.slide_index = scene.slides.size();
            command.template_palette.accent = "#426D63";
            expect(apply_presentation_edit(scene, command).error == PresentationEditError::None,
                "refined templates are ordinary atomic slide commands");
            const auto& slide = scene.slides.back();
            expect(std::any_of(slide.shapes.begin(), slide.shapes.end(),
                       [](const auto& shape)
            {
                return !shape.fill.color.empty();
            }) &&
                    std::any_of(slide.shapes.begin(), slide.shapes.end(),
                        [](const auto& shape)
            {
                return !shape.text.paragraphs.empty();
            }),
                "refined template includes independent editable content and graphics");
            for (const auto& shape : slide.shapes)
            {
                expect(shape.transform[4] >= 0 && shape.transform[5] >= 0 &&
                        shape.transform[4] + shape.width <= scene.width &&
                        shape.transform[5] + shape.height <= scene.height,
                    "refined template geometry scales inside a non-default slide size");
            }
        }
        const auto package = serialize_presentation(scene);
        const auto reopened = parse_presentation(package.parts);
        expect(package.error == PresentationError::None && reopened.error == PresentationError::None &&
                reopened.scene.slides.size() == 5,
            "all four refined templates survive PPTX roundtrip");
        if (reopened.scene.slides.size() == scene.slides.size())
        {
            for (std::size_t index = 1; index < scene.slides.size(); ++index)
            {
                expect(reopened.scene.slides[index].shapes.size() == scene.slides[index].shapes.size(),
                    "template objects remain separately editable after roundtrip");
            }
        }
        PresentationEditCommand invalid;
        invalid.action = PresentationEditAction::AddSlide;
        invalid.layout = PresentationSlideLayout::ResearchStudio;
        invalid.template_palette.accent = "invalid";
        const auto id = scene.next_shape_id;
        expect(apply_presentation_edit(scene, invalid).error == PresentationEditError::InvalidValue &&
                scene.next_shape_id == id && scene.slides.size() == 5,
            "invalid template palette fails atomically");
    }

    int run_presentation_tests()
    {
        check_order_and_text();
        check_saturation_modulation();
        check_math_without_fallback();
        check_inheritance();
        check_line_styles();
        check_group_images_and_paragraphs();
        check_svg_package_image();
        check_picture_bullets();
        check_metafile_package_images();
        check_slide_transitions();
        check_embedded_font_metadata();
        check_content_placeholders_and_list_styles();
        check_numbering_formats_and_tabs();
        check_internal_hyperlinks();
        check_rejections_and_warnings();
        check_editing_and_serialization();
        check_failure_atomicity_and_duplicate_budgets();
        check_xml_export_budget();
        check_alignment_hidden_and_work_layouts();
        check_academic_layouts();
        check_refined_templates();
        return failures == 0 ? 0 : 1;
    }

}

int main()
{
    return run_presentation_tests();
}
