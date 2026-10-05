#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;

    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }

    OfficePart& part(std::vector<OfficePart>& parts, const std::string& path)
    {
        return *std::find_if(parts.begin(), parts.end(), [&](const auto& item)
        {
            return item.path == path;
        });
    }

    std::vector<OfficePart> fixture(const std::string& object)
    {
        auto parts = serialize_word(WordDocument{}).parts;
        part(parts, "word/document.xml").bytes = R"xml(
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"
 xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
 xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
 xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture" xmlns:x="urn:unknown">
<w:body><w:p><w:r><w:t>Before</w:t></w:r></w:p><w:p><w:r>)xml" +
            object + "</w:r></w:p><w:p><w:r><w:t>After</w:t></w:r></w:p></w:body></w:document>";
        auto& relationships = part(parts, "word/_rels/document.xml.rels").bytes;
        relationships.insert(relationships.find("</Relationships>"), R"xml(
<Relationship Id="preview" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"
 Target="media/preview.emf"/>
<Relationship Id="external" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"
 Target="https://invalid.example/image.emf" TargetMode="External"/>
<Relationship Id="ole" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject"
 Target="embeddings/equation.bin"/>)xml");
        parts.push_back({"word/media/preview.emf", "immutable cached preview"});
        parts.push_back({"word/embeddings/equation.bin", "immutable OLE data"});
        return parts;
    }

    std::string vml(const std::string& style, const std::string& attributes = "")
    {
        return "<w:object><v:shape style='" + style + "' alt='Equation'><v:imagedata r:id='preview' " +
            attributes + "/></v:shape><o:OLEObject r:id='ole' ProgID='Equation.3'/></w:object>";
    }

    bool close(double left, double right)
    {
        return std::abs(left - right) < 0.000001;
    }

    void dimensions()
    {
        const std::pair<const char*, double> cases[] = {{"143.7pt", 143.7}, {"96px", 72}, {"96", 72},
            {"1in", 72}, {"2.54cm", 72}, {"25.4mm", 72}, {"6pc", 72}, {" +2.5e1 PT ", 25}, {"9000pt", 3168},
            {"0.1pt", 1}, {"-4pt", 100}, {"0pt", 100}, {"nanpt", 100}, {"infpt", 100}, {"1e999pt", 100},
            {"50%", 100}, {"1em", 100}, {"oops", 100}, {"", 100}};
        for (const auto& item : cases)
        {
            auto loaded = parse_word(fixture(vml(std::string("width:") + item.first + ";height:27.35pt;")));
            check(loaded.success && loaded.document.images.size() == 1, "VML preview loads safely");
            if (!loaded.success || loaded.document.images.empty())
                continue;
            const auto& image = loaded.document.images.front();
            check(close(image.width, item.second) && close(image.height, 27.35),
                "VML length converts to points without a square fallback for supported units");
            check(image.description == "Equation" && image.mime_type == "image/x-emf" &&
                    validate_word(loaded.document).empty(),
                "VML model is valid and retains resource metadata");
        }
        auto loaded = parse_word(fixture(vml("WIDTH:5pt; height:19.9pt; Width:92.75pt;")));
        check(loaded.success && loaded.document.images.size() == 1 &&
                close(loaded.document.images.front().width, 92.75),
            "style properties ignore case and use the last declaration");
        loaded = parse_word(fixture(vml(std::string(9000, ' ') + "width:20pt;height:30pt")));
        check(loaded.success && loaded.document.images.size() == 1 &&
                loaded.document.images.front().width == 100 && !loaded.document.warnings.empty(),
            "oversized style uses a bounded, advertised fallback");
        loaded = parse_word(fixture("<w:pict><v:group style='width:200pt;height:100pt' coordsize='100,100'>"
                                    "<v:shape style='width:50;height:20'><v:imagedata r:id='preview'/>"
                                    "</v:shape></v:group></w:pict>"));
        check(loaded.success && loaded.document.images.size() == 1 &&
                loaded.document.images.front().width == 100 && !loaded.document.warnings.empty(),
            "unsupported group coordinates are not misinterpreted as physical pixels");
    }

    void transforms()
    {
        auto loaded = parse_word(
            fixture(vml("width:143.7pt;height:27.35pt;rotation:55deg;flip:x;position:absolute;z-index:-1;",
                "cropleft='16384f' croptop='10%' cropright='.125' cropbottom='-.2'")));
        check(loaded.success && loaded.document.images.size() == 1, "VML transformed preview loads");
        if (!loaded.success || loaded.document.images.empty())
            return;
        const auto& image = loaded.document.images.front();
        check(close(image.crop[0], 0.25) && close(image.crop[1], 0.1) && close(image.crop[2], 0.125) &&
                close(image.crop[3], -0.2),
            "VML decimal, percentage and 16.16 crop fractions convert correctly");
        check(image.rotation == -55 && image.flip_horizontal && !image.flip_vertical && image.anchored &&
                image.behind_text,
            "VML post-rotation flip maps to shared pre-rotation flip semantics");
        loaded = parse_word(fixture(vml("width:1in;height:2in", "cropleft='1' cropright='1' croptop='nan'")));
        check(loaded.success && loaded.document.images.front().crop == std::array<double, 4>{} &&
                !loaded.document.warnings.empty(),
            "invalid crop cannot erase the preview or poison model validation");
        loaded = parse_word(fixture(R"xml(<w:drawing><wp:anchor behindDoc="1">
<x:extent cx="1" cy="1"/><wp:extent cx="1824990" cy="347345"/><wp:docPr descr="Modern"/>
<a:graphic><a:graphicData><pic:pic><pic:blipFill><a:blip r:embed="preview"/>
<a:srcRect l="25000" t="-10000" r="12500" b="0"/></pic:blipFill>
<pic:spPr><a:xfrm rot="5400000" flipH="1" flipV="true"/></pic:spPr></pic:pic>
</a:graphicData></a:graphic></wp:anchor></w:drawing>)xml"));
        check(loaded.success && loaded.document.images.size() == 1, "DrawingML preview remains supported");
        if (!loaded.success || loaded.document.images.empty())
            return;
        const auto& modern = loaded.document.images.front();
        check(close(modern.width, 143.7) && close(modern.height, 27.35) && modern.rotation == 90 &&
                modern.flip_horizontal && modern.flip_vertical && modern.anchored && modern.behind_text &&
                modern.description == "Modern" && close(modern.crop[0], 0.25) && close(modern.crop[1], -0.1),
            "namespace-aware DrawingML geometry retains extents, crop, flip, rotation and anchoring");
    }

    void boundaries()
    {
        const char* objects[] = {"<w:pict><v:shape style='width:20pt;height:30pt'><v:imagedata "
                                 "x:id='preview'/></v:shape></w:pict>",
            "<w:pict><v:shape style='width:20pt;height:30pt'><x:imagedata "
            "r:id='preview'/></v:shape></w:pict>",
            "<w:pict><v:shape style='width:20pt;height:30pt'><v:imagedata "
            "r:id='external'/></v:shape></w:pict>",
            "<w:drawing><a:blip x:embed='preview'/></w:drawing>",
            "<w:drawing><a:blip r:link='external'/></w:drawing>"};
        for (const auto* object : objects)
        {
            const auto loaded = parse_word(fixture(object));
            check(loaded.success && loaded.document.images.empty(),
                "unrelated namespaces and external pictures never become embedded previews");
        }
        const auto loaded = parse_word(
            fixture("<w:pict xmlns:vector='urn:schemas-microsoft-com:vml' "
                    "xmlns:rel='http://schemas.openxmlformats.org/officeDocument/2006/relationships'>"
                    "<vector:shape style='width:25pt;height:15pt'><vector:imagedata rel:id='preview'/>"
                    "</vector:shape></w:pict>"));
        check(loaded.success && loaded.document.images.size() == 1 &&
                loaded.document.images.front().width == 25,
            "valid alternate namespace prefixes are recognized");
    }

    std::string object_xml(const std::string& bytes)
    {
        pugi::xml_document xml;
        xml.load_string(bytes.c_str());
        auto paragraph = xml.child("w:document").child("w:body").child("w:p").next_sibling("w:p");
        std::ostringstream stream;
        paragraph.child("w:r").child("w:object").print(stream, "", pugi::format_raw);
        return stream.str();
    }

    void preservation()
    {
        auto parts = fixture(vml("height:284.2pt;width:413.25pt;"));
        auto loaded = parse_word(parts);
        auto saved = serialize_word(loaded.document);
        check(loaded.success && saved.success && saved.parts.size() == parts.size(),
            "VML import and unchanged save succeed");
        if (!loaded.success || !saved.success)
            return;
        for (const auto& source : parts)
            check(part(saved.parts, source.path).bytes == source.bytes,
                "unchanged save retains every package part byte-for-byte");
        loaded.document.paragraphs.front().runs.front().text = "Edited nearby text";
        saved = serialize_word(loaded.document);
        check(saved.success, "body editing beside a VML/OLE preview remains saveable");
        if (!saved.success)
            return;
        for (const auto& source : parts)
            if (source.path != "word/document.xml")
                check(part(saved.parts, source.path).bytes == source.bytes,
                    "editing body text preserves all related media and embedding bytes");
        check(object_xml(part(saved.parts, "word/document.xml").bytes) ==
                object_xml(part(parts, "word/document.xml").bytes),
            "body edit leaves VML and OLE object XML untouched");
        const auto reopened = parse_word(saved.parts);
        check(reopened.success && reopened.document.images.size() == 1 &&
                close(reopened.document.images.front().width, 413.25) &&
                close(reopened.document.images.front().height, 284.2),
            "source geometry survives edit, save and reopen");
        auto deleted = reopened.document;
        auto& runs = deleted.paragraphs[1].runs;
        runs.erase(std::remove_if(runs.begin(), runs.end(),
                       [](const auto& run)
        {
            return run.image_id != 0;
        }),
            runs.end());
        check(!validate_word_image_placement(reopened.document, deleted).empty() &&
                !serialize_word(deleted).success,
            "public image placement validation and package writer both reject deleting protected previews");
        deleted = reopened.document;
        deleted.paragraphs.erase(deleted.paragraphs.begin() + 1);
        check(!validate_word_image_placement(reopened.document, deleted).empty(),
            "image placement validation also rejects removing its entire paragraph");
    }

    int run_image_geometry_tests()
    {
        dimensions();
        transforms();
        boundaries();
        preservation();
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_image_geometry_tests();
}
