#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace
{
    using namespace mirrorfly;
    int failures = 0;
    std::filesystem::path output_directory;

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
        return *std::find_if(parts.begin(), parts.end(), [&](const auto& entry)
        {
            return entry.path == path;
        });
    }

    void export_case(
        const std::string& name, const std::vector<OfficePart>& before, const std::vector<OfficePart>& after)
    {
        if (output_directory.empty())
            return;
        for (const auto* parts : {&before, &after})
            for (const auto& entry : *parts)
            {
                const auto target = output_directory / name / (parts == &before ? "before" : "after") /
                    std::filesystem::u8path(entry.path);
                std::filesystem::create_directories(target.parent_path());
                std::ofstream stream(target, std::ios::binary);
                stream.write(entry.bytes.data(), static_cast<std::streamsize>(entry.bytes.size()));
                check(bool(stream), "independent verification fixture is written");
            }
    }

    std::string cell(const std::string& properties = {})
    {
        return "<w:tc><w:tcPr>" + properties + "</w:tcPr><w:p><w:r><w:t>保留文字</w:t></w:r></w:p></w:tc>";
    }

    std::vector<OfficePart> fixture(const std::string& rows, const std::string& extra = {})
    {
        auto parts = serialize_word(WordDocument{}).parts;
        part(parts, "word/document.xml").bytes =
            "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main' "
            "xmlns:x='urn:retained'><w:body><w:tbl><w:tblPr><w:tblBorders>"
            "<w:left w:val='single' w:sz='8' w:color='100000'/>"
            "<w:top w:val='double' w:sz='16' w:color='200000'/>"
            "<w:right w:val='dashed' w:sz='24' w:color='300000'/>"
            "<w:bottom w:val='dotted' w:sz='32' w:color='400000'/>"
            "<w:insideH w:val='single' w:sz='4' w:color='500000'/>"
            "<w:insideV w:val='dashed' w:sz='6' w:color='600000'/>"
            "</w:tblBorders>" +
            extra +
            "</w:tblPr><w:tblGrid><w:gridCol w:w='1440'/><w:gridCol w:w='1440'/>"
            "</w:tblGrid>" +
            rows + "</w:tbl></w:body></w:document>";
        parts.push_back({"customXml/untouched.xml", "<custom>Do not rewrite</custom>"});
        return parts;
    }

    void parse_edges()
    {
        auto parts = fixture("<w:tr>" + cell() + cell() + "</w:tr><w:tr>" + cell() + cell() + "</w:tr>");
        auto loaded = parse_word(parts);
        check(loaded.success, "six independent table borders parse");
        if (!loaded.success)
            return;
        const auto& table = loaded.document.tables[0];
        const auto& a = table.cells[0].border_rows[0];
        const auto& d = table.cells[3].border_rows[0];
        check(a[0].color == "#100000" && a[1].style == "double" && a[1].width == 2 &&
                a[2].color == "#600000" && a[3].color == "#500000" && !a[1].cell_specific,
            "outer and inner edges are resolved by actual grid position");
        check(d[0].width == 0.75 && d[1].width == 0.5 && d[2].width == 3 && d[3].width == 4,
            "bottom-right cell does not reuse table top border");
        auto saved = serialize_word(loaded.document);
        check(saved.success &&
                part(saved.parts, "word/document.xml").bytes == part(parts, "word/document.xml").bytes,
            "no-op save preserves border source bytes");
        parts = fixture("<w:tr>" +
            cell("<w:tcBorders><w:left w:val='nil'/>"
                 "<w:top w:val='single' w:sz='12' w:color='ABCDEF'/>"
                 "<w:right w:val='none'/></w:tcBorders>") +
            cell() + "</w:tr>");
        loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells[0].border_rows[0][0].style == "nil" &&
                loaded.document.tables[0].cells[0].border_rows[0][1].color == "#ABCDEF" &&
                loaded.document.tables[0].cells[0].border_rows[0][1].cell_specific &&
                loaded.document.tables[0].cells[0].border_rows[0][2].style == "dashed" &&
                !loaded.document.tables[0].cells[0].border_rows[0][2].cell_specific,
            "cell nil suppresses, none inherits, and line properties override table defaults");
    }

    void merged_patch()
    {
        const auto first = cell("<w:vMerge w:val='restart'/><w:tcBorders>"
                                "<w:top w:val='double' w:color='123456' w:sz='16' x:keep='edge'/>"
                                "<w:left w:val='single' w:color='112233' w:sz='8'/>"
                                "<w:tl2br w:val='dashed' w:color='998877'/></w:tcBorders>");
        const auto second = cell("<w:vMerge/><w:tcBorders>"
                                 "<w:left w:val='dashed' w:color='334455' w:sz='24'/>"
                                 "<w:bottom w:val='dotted' w:color='556677' w:sz='32' x:keep='bottom'/>"
                                 "</w:tcBorders>");
        const std::string gap = "<w:trPr><w:gridBefore w:val='1'/></w:trPr>";
        auto parts = fixture("<w:tr>" + gap + first + "</w:tr><w:tr>" + gap + second + "</w:tr>");
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells.size() == 1,
            "vertical merge with a leading grid gap parses");
        if (!loaded.success)
            return;
        const auto before = loaded.document.tables[0].cells[0].border_rows;
        check(before.size() == 2 && before[0][0].color == "#112233" && before[1][0].color == "#334455" &&
                before[1][3].color == "#556677",
            "vertical merge retains differing side segments and last-row bottom");
        auto changed =
            set_word_cell_border(loaded.document, 0, 0, WordBorderEdge::Bottom, {"double", "#AABBCC", 2.5});
        check(changed.success && changed.changed, "public transaction changes only merged bottom edge");
        loaded.document.tables[0].cells[0].background = "#FFEEDD";
        auto saved = serialize_word(loaded.document);
        check(saved.success, "merged bottom edit and fill save across gridBefore");
        if (!saved.success)
        {
            std::cerr << saved.error << '\n';
            return;
        }
        auto reopened = parse_word(saved.parts);
        check(reopened.success, "edited merged source reopens");
        export_case("merged", parts, saved.parts);
        const auto& after = reopened.document.tables[0].cells[0].border_rows;
        check(after[0] == before[0] && after[1][0] == before[1][0] && after[1][1] == before[1][1] &&
                after[1][2] == before[1][2] && after[1][3].color == "#AABBCC" &&
                after[1][3].style == "double" && after[1][3].width == 2.5,
            "only last physical row bottom changes");
        pugi::xml_document xml;
        xml.load_string(part(saved.parts, "word/document.xml").bytes.c_str());
        const auto table = xml.child("w:document").child("w:body").child("w:tbl");
        const auto owner = table.child("w:tr").child("w:tc").child("w:tcPr");
        const auto continuation = table.child("w:tr").next_sibling("w:tr").child("w:tc").child("w:tcPr");
        check(owner.child("w:tcBorders").child("w:tl2br") &&
                std::string(owner.child("w:tcBorders").child("w:top").attribute("x:keep").value()) ==
                    "edge" &&
                std::string(
                    continuation.child("w:tcBorders").child("w:bottom").attribute("x:keep").value()) ==
                    "bottom" &&
                std::string(continuation.child("w:shd").attribute("w:fill").value()) == "FFEEDD",
            "unknown edge attributes, diagonal and continuation fill are preserved");
        for (const auto& original : parts)
            if (original.path != "word/document.xml")
                check(
                    part(saved.parts, original.path).bytes == original.bytes, "unrelated ZIP part unchanged");
        changed =
            set_word_cell_border(reopened.document, 0, 0, WordBorderEdge::Left, {"dotted", "#010203", 1.25});
        saved = serialize_word(reopened.document);
        reopened = parse_word(saved.parts);
        check(changed.success && saved.success && reopened.success &&
                reopened.document.tables[0].cells[0].border_rows[0][0].width == 1.25 &&
                reopened.document.tables[0].cells[0].border_rows[1][0].color == "#010203",
            "merged side transaction intentionally changes every physical segment");
    }

    void inherited_and_logical()
    {
        auto parts = fixture("<w:tr>" + cell() + cell() + "</w:tr>", "<w:tblStyle w:val='Borders'/>");
        auto& rels = part(parts, "word/_rels/document.xml.rels").bytes;
        rels.insert(rels.find("</Relationships>"),
            "<Relationship Id='style' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
            "relationships/styles' Target='styles.xml'/>");
        parts.push_back({"word/styles.xml",
            "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
            "<w:style w:type='table' w:styleId='Borders'><w:tcPr><w:tcBorders>"
            "<w:start w:val='double' w:color='112233' w:sz='24'/></w:tcBorders></w:tcPr>"
            "</w:style></w:styles>"});
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells[0].border_rows[0][0].color == "#112233",
            "inherited logical start border is resolved");
        auto edit =
            set_word_cell_border(loaded.document, 0, 0, WordBorderEdge::Left, {"single", "#ABCDEF", 1});
        auto saved = serialize_word(loaded.document);
        auto reopened = parse_word(saved.parts);
        export_case("logical", parts, saved.parts);
        check(edit.success && saved.success && reopened.success &&
                reopened.document.tables[0].cells[0].border_rows[0][0].color == "#ABCDEF" &&
                reopened.document.tables[0].cells[1].border_rows[0][0].color == "#112233" &&
                part(saved.parts, "word/styles.xml").bytes == part(parts, "word/styles.xml").bytes,
            "physical edit overrides inherited logical edge without mutating shared style");
        auto& source = part(parts, "word/document.xml").bytes;
        source.insert(source.find("<w:tcPr>") + 8,
            "<w:tcBorders><w:left w:val='dashed' w:sz='8' w:color='CCBBAA'/></w:tcBorders>");
        loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells[0].border_rows[0][0].color == "#CCBBAA",
            "direct physical side overrides inherited logical side");
        parts = fixture("<w:tr>" +
                cell("<w:tcBorders><w:start w:val='single' w:color='102030'/>"
                     "<w:end w:val='double' w:color='405060'/></w:tcBorders>") +
                cell() + "</w:tr>",
            "<w:bidiVisual/>");
        loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].right_to_left &&
                loaded.document.tables[0].cells[0].border_rows[0][0].color == "#405060" &&
                loaded.document.tables[0].cells[0].border_rows[0][2].color == "#102030",
            "RTL logical edge mapping is explicit");
    }

    void themes_and_prefixes()
    {
        auto parts = fixture("<w:tr>" +
            cell("<w:tcBorders>"
                 "<w:top w:val='double' w:color='000000' w:themeColor='accent1' w:sz='12' x:keep='top'/>"
                 "<w:bottom w:val='single' w:color='000000' w:themeColor='accent1' w:themeTint='99'/>"
                 "</w:tcBorders>") +
            cell() + "</w:tr>");
        auto& rels = part(parts, "word/_rels/document.xml.rels").bytes;
        rels.insert(rels.find("</Relationships>"),
            "<Relationship Id='theme' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
            "relationships/theme' Target='theme/colors.xml'/>");
        parts.push_back({"word/theme/colors.xml",
            "<a:theme xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'><a:themeElements>"
            "<a:clrScheme><a:accent1><a:srgbClr val='336699'/></a:accent1></a:clrScheme>"
            "</a:themeElements></a:theme>"});
        auto& source = part(parts, "word/document.xml").bytes;
        for (std::size_t at = 0; (at = source.find("w:", at)) != std::string::npos; at += 2)
            source.replace(at, 2, "q:");
        source.replace(source.find("xmlns:w"), 7, "xmlns:q");
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells[0].border_rows[0][1].color == "#336699" &&
                loaded.document.tables[0].cells[0].border_rows[0][3].color != "#000000",
            "theme border colors resolve with a non-default Word namespace prefix");
        const auto old_bottom = loaded.document.tables[0].cells[0].border_rows[0][3];
        auto changed =
            set_word_cell_border(loaded.document, 0, 0, WordBorderEdge::Top, {"dashed", "#778899", 1.5});
        const auto repeated =
            set_word_cell_border(loaded.document, 0, 0, WordBorderEdge::Top, {"dashed", "#778899", 1.5});
        check(changed.success && changed.changed && repeated.success && !repeated.changed,
            "repeat transaction is a no-op");
        auto saved = serialize_word(loaded.document);
        auto reopened = parse_word(saved.parts);
        export_case("theme", parts, saved.parts);
        check(saved.success && reopened.success &&
                reopened.document.tables[0].cells[0].border_rows[0][1].color == "#778899" &&
                reopened.document.tables[0].cells[0].border_rows[0][3] == old_bottom,
            "explicit edited RGB does not inherit stale theme color");
        pugi::xml_document xml;
        xml.load_string(part(saved.parts, "word/document.xml").bytes.c_str());
        const auto borders = xml.child("q:document")
                                 .child("q:body")
                                 .child("q:tbl")
                                 .child("q:tr")
                                 .child("q:tc")
                                 .child("q:tcPr")
                                 .child("q:tcBorders");
        check(!borders.child("q:top").attribute("q:themeColor") &&
                std::string(borders.child("q:top").attribute("x:keep").value()) == "top" &&
                std::string(borders.child("q:bottom").attribute("q:themeColor").value()) == "accent1" &&
                std::string(borders.child("q:bottom").attribute("q:themeTint").value()) == "99",
            "patch clears only edited edge theme and retains opposite edge theme and extensions");
        parts = fixture("<w:tr><w:tblPrEx><w:tblBorders><w:top w:val='nil'/>"
                        "</w:tblBorders></w:tblPrEx>" +
            cell("<w:gridSpan w:val='2'/>") + "</w:tr>");
        loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells[0].column_span == 2 &&
                loaded.document.tables[0].cells[0].border_rows[0][1].style == "nil" &&
                loaded.document.tables[0].cells[0].border_rows[0][2].color == "#300000",
            "row border exception and horizontal span choose correct exterior edges");
    }

    void conflicts()
    {
        const WordBorder thin{"single", "#778899", 0.5};
        const WordBorder wide_dash{"dashed", "#112233", 12};
        check(resolve_word_border(thin, wide_dash) == thin && resolve_word_border(wide_dash, thin) == thin,
            "Word dashed weight stays one, not its visual width");
        const WordBorder twice{"double", "#112233", 1};
        const WordBorder thick{"single", "#445566", 3};
        check(resolve_word_border(twice, thick) == thick, "equal weight uses Word style precedence");
        check(resolve_word_border({"nil", "#000000", 0}, thick).style == "nil" &&
                resolve_word_border(thick, {"nil", "#000000", 0}).style == "nil",
            "nil suppresses either side of a collapsed edge");
        check(resolve_word_border({}, thin) == thin && resolve_word_border(thin, {}).style == "single",
            "unspecified edge does not suppress a neighbor");
        const WordBorder bright{"single", "#FFFFFF", 1};
        const WordBorder dark{"single", "#000000", 1};
        check(resolve_word_border(bright, dark) == dark, "equal style uses darker weighted color");
        const WordBorder red{"single", "#020000", 1};
        const WordBorder green{"single", "#000100", 1};
        check(resolve_word_border(green, red) == red, "second brightness formula resolves first tie");
        const WordBorder blue{"single", "#000002", 1};
        check(resolve_word_border(green, blue) == blue, "third brightness formula resolves second tie");
    }

    void validation()
    {
        auto loaded = parse_word(fixture("<w:tr>" + cell() + cell() + "</w:tr>"));
        const auto before = loaded.document.tables[0].cells[0].border_rows;
        for (auto value : {WordBorder{"single", "#112233", -1}, WordBorder{"wave", "#112233", 1},
                 WordBorder{"single", "red", 1}, WordBorder{"single", "#112233", 0.3},
                 WordBorder{"single", "#112233", std::numeric_limits<double>::quiet_NaN()}})
            check(!set_word_cell_border(loaded.document, 0, 0, WordBorderEdge::Left, value).success &&
                    loaded.document.tables[0].cells[0].border_rows == before,
                "invalid transaction rejects atomically");
        check(!set_word_cell_border(loaded.document, 9, 0, WordBorderEdge::Left, {}).success &&
                !set_word_cell_border(loaded.document, 0, 9, WordBorderEdge::Left, {}).success &&
                !set_word_cell_border(loaded.document, 0, 0, static_cast<WordBorderEdge>(4), {}).success,
            "transaction bounds reject");
        loaded.document.tables[0].cells[0].border_rows[0][0].color = "#AABBCC";
        check(!serialize_word(loaded.document).success,
            "a changed inherited border cannot silently become an explicit border");
        loaded.document.tables[0].cells[0].border_rows = before;
        loaded.document.tables[0].cells[0].border_rows.clear();
        check(!serialize_word(loaded.document).success, "source border row identity cannot disappear");
    }
}

int run_word_table_border_tests(int argc, char** argv)
{
    if (argc == 2)
        output_directory = std::filesystem::u8path(argv[1]);
    parse_edges();
    merged_patch();
    inherited_and_logical();
    themes_and_prefixes();
    conflicts();
    validation();
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    return run_word_table_border_tests(argc, argv);
}
