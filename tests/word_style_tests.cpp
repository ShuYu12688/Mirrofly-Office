#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

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

    OfficePart& part(std::vector<OfficePart>& parts, const std::string& name)
    {
        return *std::find_if(parts.begin(), parts.end(), [&](const auto& item)
        {
            return item.path == name;
        });
    }

    std::vector<OfficePart> fixture(const std::string& body)
    {
        auto result = serialize_word(WordDocument{}).parts;
        part(result, "word/document.xml").bytes =
            "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main' "
            "xmlns:x='urn:keep'><w:body>" +
            body + "</w:body></w:document>";
        auto& relationships = part(result, "word/_rels/document.xml.rels").bytes;
        relationships.insert(relationships.find("</Relationships>"),
            "<Relationship Id='styles' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles' "
            "Target='formats/custom.xml'/>"
            "<Relationship Id='theme' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme' "
            "Target='theme/custom.xml'/>");
        result.push_back({"word/formats/custom.xml", R"xml(
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:asciiTheme="minorHAnsi" w:eastAsiaTheme="minorEastAsia"/>
<w:lang w:eastAsia="zh-CN"/><w:sz w:val="22"/></w:rPr></w:rPrDefault>
<w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259"/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:styleId="Normal" w:default="1"><w:pPr><w:jc w:val="both"/></w:pPr></w:style>
<w:style w:type="paragraph" w:styleId="Base"><w:basedOn w:val="Normal"/>
<w:pPr><w:keepNext/><w:pageBreakBefore/><w:bidi/><w:jc w:val="center"/><w:ind w:left="480" w:right="120" w:firstLine="240"/>
<w:spacing w:before="100" w:line="300" w:lineRule="exact"/><w:outlineLvl w:val="1"/>
<w:shd w:val="clear" w:fill="FFEEDD"/><w:pBdr><w:bottom w:val="single" w:color="123456"/></w:pBdr></w:pPr>
<w:rPr><w:b/><w:sz w:val="28"/><w:color w:val="000000" w:themeColor="accent1"/>
<w:dstrike/><w:highlight w:val="yellow"/><w:spacing w:val="20"/><w:vertAlign w:val="superscript"/>
<w:bdr w:val="single" w:color="ABCDEF"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Derived"><w:basedOn w:val="Base"/>
<w:rPr><w:b/><w:i/><w:sz w:val="21"/></w:rPr></w:style>
<w:style w:type="character" w:styleId="Emph"><w:rPr><w:i/><w:u/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="List"><w:basedOn w:val="Normal"/>
<w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr></w:style>
</w:styles>)xml"});
        result.push_back({"word/theme/custom.xml", R"xml(
<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><a:themeElements>
<a:clrScheme><a:dk1><a:sysClr a:unused="keep" val="windowText" lastClr="000000"/></a:dk1>
<a:accent1><a:srgbClr val="336699"/></a:accent1></a:clrScheme>
<a:fontScheme><a:minorFont><a:latin typeface="Calibri"/><a:ea typeface=""/>
<a:font script="Hans" typeface="SimSun"/></a:minorFont></a:fontScheme>
</a:themeElements></a:theme>)xml"});
        return result;
    }

    void line_variants()
    {
        for (int inherited = 0; inherited < 3; ++inherited)
        {
            auto parts =
                fixture("<w:p><w:pPr><w:pStyle w:val='Lines'/></w:pPr>"
                        "<w:r><w:rPr><w:u w:val='double' x:keep='yes'/></w:rPr><w:t>target</w:t></w:r>"
                        "<w:r><w:rPr><w:rStyle w:val='Emph'/></w:rPr><w:t>keep</w:t></w:r></w:p>");
            auto& styles = part(parts, "word/formats/custom.xml").bytes;
            styles.insert(styles.find("</w:styles>"),
                "<w:style w:type='paragraph' w:styleId='Lines'><w:rPr>" +
                    std::string(inherited == 2 ? "<w:dstrike/>"
                            : inherited == 1   ? "<w:strike/>"
                                               : "") +
                    "</w:rPr></w:style>");
            const auto original = parse_word(parts);
            check(original.success && original.document.paragraphs[0].runs[0].double_underline &&
                    original.document.paragraphs[0].runs[0].double_strike == (inherited == 2),
                "direct double underline and inherited strike variants import distinctly");
            const auto untouched = serialize_word(original.document);
            bool identical = untouched.success && untouched.parts.size() == parts.size();
            for (std::size_t i = 0; identical && i < parts.size(); ++i)
                identical = parts[i].bytes == untouched.parts[i].bytes;
            check(identical, "unmodified decoration package is byte-identical");
            for (const bool double_line : {false, true})
            {
                auto direct_parts = parts;
                auto& body = part(direct_parts, "word/document.xml").bytes;
                body.insert(body.find("<w:u "), double_line ? "<w:dstrike/>" : "<w:strike/>");
                const auto direct = parse_word(direct_parts);
                check(direct.success && direct.document.paragraphs[0].runs[0].strike &&
                        direct.document.paragraphs[0].runs[0].double_strike == double_line,
                    "active direct strike variant supersedes the opposite paragraph inheritance");
            }
            for (int underline = 0; underline < 3; ++underline)
                for (int strike = 0; strike < 3; ++strike)
                {
                    auto changed = original.document;
                    auto& run = changed.paragraphs[0].runs[0];
                    run.underline = underline != 0;
                    run.double_underline = underline == 2;
                    run.strike = strike != 0;
                    run.double_strike = strike == 2;
                    auto saved = serialize_word(changed);
                    const auto reopened = parse_word(saved.parts);
                    check(saved.success && reopened.success, "all line transitions save and reopen");
                    if (!reopened.success)
                        continue;
                    const auto& actual = reopened.document.paragraphs[0].runs;
                    check(actual[0].underline == (underline != 0) &&
                            actual[0].double_underline == (underline == 2) &&
                            actual[0].strike == (strike != 0) && actual[0].double_strike == (strike == 2) &&
                            actual[1].underline && !actual[1].double_underline &&
                            actual[1].strike == (inherited != 0) &&
                            actual[1].double_strike == (inherited == 2),
                        "line transitions override inheritance without changing the other run");
                    check(part(saved.parts, "word/formats/custom.xml").bytes == styles &&
                            part(saved.parts, "word/document.xml").bytes.find("keep=") != std::string::npos,
                        "line edits preserve shared styles and unknown underline attributes");
                }
        }
    }

    void theme_variants()
    {
        auto parts = fixture(
            "<w:p><w:pPr><w:shd w:fill='000000' w:themeFill='accent1' w:themeFillTint='99'/>"
            "<w:pBdr><w:bottom w:val='single' w:color='000000' w:themeColor='accent1' w:themeTint='99'/>"
            "</w:pBdr></w:pPr>"
            "<w:r><w:rPr><w:color w:val='000000' w:themeColor='accent1' "
            "w:themeTint='99'/></w:rPr><w:t>Tint</w:t></w:r>"
            "<w:r><w:rPr><w:color w:themeColor='accent1' w:themeTint='99' "
            "w:themeShade='00'/></w:rPr><w:t>Both</w:t></w:r>"
            "<w:r><w:rPr><w:color w:themeColor='accent1' w:themeShade='00'/></w:rPr><w:t>Black</w:t></w:r>"
            "<w:r><w:rPr><w:color w:themeColor='accent1' w:themeTint='00'/></w:rPr><w:t>White</w:t></w:r>"
            "<w:r><w:rPr><w:color w:themeColor='accent1' w:themeShade='FF'/></w:rPr><w:t>Identity</w:t></w:r>"
            "<w:r><w:rPr><w:color w:val='123456' w:themeColor='missing' "
            "w:themeTint='99'/></w:rPr><w:t>Fallback</w:t></w:r>"
            "<w:r><w:rPr><w:color w:val='123456' w:themeColor='accent1' "
            "w:themeTint='ZZ'/></w:rPr><w:t>Invalid</w:t></w:r>"
            "<w:r><w:rPr><w:shd w:themeFill='accent1' w:themeFillTint='99'/>"
            "<w:bdr w:val='single' w:themeColor='accent1' "
            "w:themeTint='99'/></w:rPr><w:t>Decorations</w:t></w:r></w:p>");
        auto& theme = part(parts, "word/theme/custom.xml").bytes;
        theme.replace(theme.find("336699"), 6, "4F81BD");
        auto parsed = parse_word(parts);
        check(parsed.success, "theme variants parse");
        if (!parsed.success)
            return;
        const auto& paragraph = parsed.document.paragraphs[0];
        const char* expected[]{"#95B3D7", "#95B3D7", "#000000", "#FFFFFF", "#4F81BD", "#123456", "#123456"};
        for (std::size_t i = 0; i < 7; ++i)
            check(paragraph.runs[i].color == expected[i],
                "theme color and tint precedence resolve independently of cached RGB");
        check(paragraph.background == "#95B3D7" && paragraph.border_color == "#95B3D7" &&
                paragraph.runs[7].background == "#95B3D7" && paragraph.runs[7].border_color == "#95B3D7",
            "paragraph and character decorations share resolved theme colors");
        parsed.document.paragraphs[0].runs[0].bold = true;
        const auto saved = serialize_word(parsed.document);
        check(saved.success, "unrelated formatting saves theme-aware document");
        if (!saved.success)
            return;
        const auto reopened = parse_word(saved.parts);
        check(reopened.success && reopened.document.paragraphs[0].runs[0].color == "#95B3D7",
            "theme color survives unrelated edits and reopen");
        for (const auto& original : parts)
            if (original.path != "word/document.xml")
                for (const auto& output : saved.parts)
                    if (output.path == original.path)
                        check(output.bytes == original.bytes,
                            "theme and unrelated parts are preserved byte for byte");
        check(saved.parts.size() == parts.size(), "theme editing does not add package parts");
        auto changed = reopened.document;
        changed.paragraphs[0].runs[0].color = "#ABCDEF";
        const auto overridden = parse_word(serialize_word(changed).parts);
        check(overridden.success && overridden.document.paragraphs[0].runs[0].color == "#ABCDEF",
            "direct color editing removes stale theme transforms");
    }

    void table_theme_colors()
    {
        auto parts = fixture("<w:tbl><w:tblPr><w:tblBorders><w:top w:val='single' w:sz='8' "
                             "w:color='000000' w:themeColor='accent1'/></w:tblBorders></w:tblPr>"
                             "<w:tblGrid><w:gridCol w:w='2000'/><w:gridCol w:w='2000'/></w:tblGrid>"
                             "<w:tr><w:tc><w:tcPr><w:shd w:fill='000000' w:themeFill='accent1' "
                             "w:themeFillTint='99'/></w:tcPr><w:p><w:r><w:t>First</w:t></w:r></w:p></w:tc>"
                             "<w:tc><w:tcPr><w:shd w:fill='123456' w:themeFill='missing'/></w:tcPr>"
                             "<w:p><w:r><w:t>Second</w:t></w:r></w:p></w:tc></w:tr></w:tbl>");
        auto& theme = part(parts, "word/theme/custom.xml").bytes;
        theme.replace(theme.find("336699"), 6, "4F81BD");
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables.size() == 1, "table theme fixture parses");
        if (!loaded.success || loaded.document.tables.empty())
            return;
        const auto& table = loaded.document.tables.front();
        check(table.border_color == "#4F81BD" && table.cells[0].background == "#95B3D7" &&
                table.cells[1].background == "#123456",
            "table border and cell shading share theme resolution with direct fallback");
        loaded.document.paragraphs[0].runs[0].size = 18;
        auto saved = serialize_word(loaded.document);
        check(saved.success &&
                part(saved.parts, "word/document.xml").bytes.find("themeFillTint") != std::string::npos,
            "unrelated table text formatting retains original theme attributes");
        auto reopened = parse_word(saved.parts);
        check(reopened.success && reopened.document.tables[0].cells[0].background == "#95B3D7",
            "table theme survives edit and reopen");
        reopened.document.tables[0].cells[0].background = "#ABCDEF";
        const auto overridden = parse_word(serialize_word(reopened.document).parts);
        check(overridden.success && overridden.document.tables[0].cells[0].background == "#ABCDEF" &&
                overridden.document.tables[0].border_color == "#4F81BD",
            "direct cell fill overrides stale theme without changing table border");
    }

    void inheritance()
    {
        auto parts = fixture("<w:p><w:pPr><w:pStyle w:val='Derived'/><w:spacing w:after='200'/></w:pPr>"
                             "<w:r><w:t>Inherited</w:t></w:r>"
                             "<w:r><w:rPr><w:rStyle w:val='Emph'/><w:rFonts w:ascii='Arial'/><w:b/><w:color "
                             "w:val='123456'/></w:rPr><w:t>Direct</w:t></w:r></w:p>"
                             "<w:p><w:r><w:t>Default</w:t></w:r></w:p>");
        auto loaded = parse_word(parts);
        check(loaded.success, "style-aware fixture parses");
        if (!loaded.success)
            return;
        const auto& paragraph = loaded.document.paragraphs[0];
        const auto& run = paragraph.runs[0];
        check(paragraph.heading == 2 && paragraph.alignment == 1 && paragraph.keep_with_next &&
                paragraph.page_break_before && paragraph.right_to_left && paragraph.left_indent == 24 &&
                paragraph.right_indent == 6 && paragraph.first_line_indent == 12,
            "paragraph basedOn chain resolves geometry and flags");
        check(paragraph.line_spacing_rule == 1 && paragraph.line_spacing_points == 15 &&
                paragraph.space_before == 5 && paragraph.space_after == 10,
            "direct spacing attributes do not erase inherited spacing");
        check(run.font == "Calibri" && run.east_asia_font == "SimSun" && run.size == 10.5 && !run.bold &&
                run.italic && run.color == "#336699",
            "theme fonts, language fallback, color, fractional size and toggle chains resolve");
        const auto& direct = paragraph.runs[1];
        check(direct.font == "Arial" && direct.east_asia_font == "SimSun" && direct.bold && !direct.italic &&
                direct.underline && direct.color == "#123456",
            "character styles toggle before absolute direct properties and direct fonts override themes");
        const auto& fallback = loaded.document.paragraphs[1];
        check(fallback.alignment == 3 && fallback.space_after == 8 &&
                std::abs(fallback.line_spacing - 259.0 / 240) < 0.0001 && fallback.runs[0].size == 11,
            "document defaults and default paragraph style apply without explicit references");
        auto saved = serialize_word(loaded.document);
        bool identical = saved.success && parts.size() == saved.parts.size();
        for (const auto& item : parts)
            identical = identical && part(saved.parts, item.path).bytes == item.bytes;
        check(identical, "reading inherited styles does not flatten or rewrite an untouched package");
    }

    void edit_inherited()
    {
        auto parts =
            fixture("<w:p><w:pPr><w:pStyle w:val='Base'/><w:spacing w:beforeLines='80' x:keep='spacing'/>"
                    "</w:pPr><w:r><w:rPr><w:rFonts w:asciiTheme='majorAscii' x:keep='fonts'/>"
                    "<w:lang w:val='en-GB'/></w:rPr><w:t>Edit</w:t></w:r></w:p>");
        auto loaded = parse_word(parts);
        check(loaded.success, "inherited edit fixture parses");
        if (!loaded.success)
            return;
        auto& paragraph = loaded.document.paragraphs[0];
        paragraph.heading = 0;
        paragraph.page_break_before = false;
        paragraph.keep_with_next = false;
        paragraph.right_to_left = false;
        paragraph.left_indent = 0;
        paragraph.right_indent = 0;
        paragraph.first_line_indent = 0;
        paragraph.background.clear();
        paragraph.border_color.clear();
        paragraph.line_spacing_points = 18;
        auto& run = paragraph.runs[0];
        run.bold = false;
        run.strike = false;
        run.script = 0;
        run.character_spacing = 0;
        run.font = "Arial";
        run.color.clear();
        run.background.clear();
        run.border_color.clear();
        auto saved = serialize_word(loaded.document);
        check(saved.success, "inherited properties can be disabled without removing style links");
        if (!saved.success)
        {
            std::cerr << saved.error << '\n';
            return;
        }
        auto reopened = parse_word(saved.parts);
        check(reopened.success, "edited inherited properties reopen");
        if (!reopened.success)
            return;
        const auto& p = reopened.document.paragraphs[0];
        const auto& r = p.runs[0];
        check(!p.heading && !p.page_break_before && !p.keep_with_next && !p.right_to_left && !p.left_indent &&
                !p.right_indent && !p.first_line_indent && p.background.empty() && p.border_color.empty() &&
                p.line_spacing_points == 18,
            "paragraph OFF and zero values override inherited formatting after reopening");
        check(!r.bold && !r.strike && !r.script && !r.character_spacing && r.font == "Arial" &&
                r.color.empty() && r.background.empty() && r.border_color.empty(),
            "run OFF, font and automatic color overrides survive inherited competing properties");
        for (const auto& source : parts)
            if (source.path != "word/document.xml")
                check(part(saved.parts, source.path).bytes == source.bytes,
                    "format edits leave styles, theme, numbering and other parts unchanged");
        pugi::xml_document xml;
        xml.load_string(part(saved.parts, "word/document.xml").bytes.c_str());
        const auto node = xml.child("w:document").child("w:body").child("w:p");
        const auto spacing = node.child("w:pPr").child("w:spacing");
        const auto fonts = node.child("w:r").child("w:rPr").child("w:rFonts");
        check(spacing.attribute("w:beforeLines") && spacing.attribute("x:keep") &&
                fonts.attribute("x:keep") && !fonts.attribute("w:asciiTheme") &&
                node.child("w:pPr").child("w:pStyle") && node.child("w:r").child("w:rPr").child("w:lang"),
            "targeted edits preserve unrelated property attributes and children");
        reopened.document.paragraphs[0].space_before = 7;
        auto next = serialize_word(reopened.document);
        check(next.success, "paragraph spacing override saves");
        if (next.success)
        {
            xml.load_string(part(next.parts, "word/document.xml").bytes.c_str());
            check(!xml.child("w:document")
                      .child("w:body")
                      .child("w:p")
                      .child("w:pPr")
                      .child("w:spacing")
                      .attribute("w:beforeLines"),
                "editing point spacing clears the competing direct line spacing attribute");
        }
    }

    void list_and_guards()
    {
        auto parts = fixture("<w:p><w:pPr><w:pStyle w:val='List'/></w:pPr><w:r><w:t>Item</w:t></w:r></w:p>");
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.paragraphs[0].list == WordListKind::Bullet &&
                loaded.document.paragraphs[0].list_instance == 1,
            "style-inherited numbering retains its concrete identity");
        loaded.document.paragraphs[0].list_level = 1;
        auto saved = serialize_word(loaded.document);
        check(saved.success && parse_word(saved.parts).document.paragraphs[0].list_level == 1 &&
                part(saved.parts, "word/numbering.xml").bytes == part(parts, "word/numbering.xml").bytes,
            "level changes work for numbering inherited from a style");
        loaded.document.paragraphs[0].list = WordListKind::None;
        saved = serialize_word(loaded.document);
        check(saved.success && parse_word(saved.parts).document.paragraphs[0].list == WordListKind::None,
            "numId zero cancels inherited lists");
        const auto bad_style = [&](const std::string& content)
        {
            auto changed = parts;
            part(changed, "word/formats/custom.xml").bytes =
                "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>" +
                content + "</w:styles>";
            return !parse_word(changed).success;
        };
        check(bad_style("<w:style w:type='paragraph' w:styleId='A'><w:basedOn w:val='B'/></w:style>"
                        "<w:style w:type='paragraph' w:styleId='B'><w:basedOn w:val='A'/></w:style>"),
            "cyclic style inheritance is rejected");
        check(bad_style(
                  "<w:style w:type='paragraph' w:styleId='A'/><w:style w:type='paragraph' w:styleId='A'/>"),
            "duplicate style IDs are rejected");
        auto changed = parts;
        auto& rels = part(changed, "word/_rels/document.xml.rels").bytes;
        const auto position = rels.find("Target='formats/custom.xml'");
        rels.insert(position, "TargetMode='External' ");
        check(!parse_word(changed).success, "external styles are not fetched");
        changed = parts;
        part(changed, "word/formats/custom.xml").path = "word/missing.xml";
        check(!parse_word(changed).success, "missing referenced style parts fail explicitly");
    }
}

int run_word_style_tests()
{
    line_variants();
    theme_variants();
    table_theme_colors();
    inheritance();
    edit_inherited();
    list_and_guards();
    return failures ? 1 : 0;
}

int main()
{
    return run_word_style_tests();
}
