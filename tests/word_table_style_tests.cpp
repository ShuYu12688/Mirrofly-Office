#include <mirrorfly/word.hpp>

#include <algorithm>
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

    OfficePart& part(std::vector<OfficePart>& parts, const std::string& path)
    {
        return *std::find_if(parts.begin(), parts.end(), [&](const auto& value)
        {
            return value.path == path;
        });
    }

    const std::string styles = R"xml(
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:docDefaults><w:rPrDefault><w:rPr><w:sz w:val="20"/></w:rPr></w:rPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:styleId="P"><w:rPr><w:color w:val="ABCDEF"/></w:rPr>
<w:pPr><w:jc w:val="right"/></w:pPr></w:style>
<w:style w:type="table" w:styleId="Base" w:default="1">
<w:tblPr><w:tblStyleRowBandSize w:val="1"/><w:tblStyleColBandSize w:val="1"/>
<w:tblCellMar><w:left w:w="100"/><w:right w:w="120"/></w:tblCellMar>
<w:tblBorders><w:top w:val="single" w:sz="12" w:color="123456"/></w:tblBorders></w:tblPr>
<w:tcPr><w:shd w:fill="EEEEEE"/></w:tcPr>
<w:pPr><w:jc w:val="center"/></w:pPr><w:rPr><w:b/><w:sz w:val="28"/></w:rPr>
<w:tblStylePr w:type="band1Horz"><w:tcPr><w:shd w:fill="111111"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="band2Horz"><w:tcPr><w:shd w:fill="222222"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="band1Vert"><w:tcPr><w:shd w:fill="333333"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="band2Vert"><w:tcPr><w:shd w:fill="444444"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="firstCol"><w:tcPr><w:shd w:fill="555555"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="lastCol"><w:tcPr><w:shd w:fill="666666"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="firstRow"><w:tcPr><w:shd w:fill="777777"/></w:tcPr>
<w:rPr><w:b/><w:color w:val="FFFFFF"/></w:rPr></w:tblStylePr>
<w:tblStylePr w:type="lastRow"><w:tcPr><w:shd w:fill="888888"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="nwCell"><w:tcPr><w:shd w:fill="990000"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="neCell"><w:tcPr><w:shd w:fill="009900"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="swCell"><w:tcPr><w:shd w:fill="000099"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="seCell"><w:tcPr><w:shd w:fill="999900"/></w:tcPr></w:tblStylePr>
</w:style>
<w:style w:type="table" w:styleId="Derived"><w:basedOn w:val="Base"/>
<w:rPr><w:b/><w:sz w:val="32"/></w:rPr>
<w:tblStylePr w:type="firstRow"><w:rPr><w:sz w:val="36"/></w:rPr></w:tblStylePr>
</w:style></w:styles>)xml";

    std::vector<OfficePart> fixture(const std::string& body)
    {
        auto parts = serialize_word(WordDocument{}).parts;
        part(parts, "word/document.xml").bytes =
            "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
            "<w:body>" +
            body + "</w:body></w:document>";
        auto& rels = part(parts, "word/_rels/document.xml.rels").bytes;
        rels.insert(rels.find("</Relationships>"),
            "<Relationship Id='s' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
            "relationships/styles' Target='styles.xml'/>");
        parts.push_back({"word/styles.xml", styles});
        return parts;
    }

    std::string cell(const std::string& properties = {}, const std::string& paragraph = {})
    {
        return "<w:tc><w:tcPr>" + properties + "</w:tcPr><w:p>" + paragraph +
            "<w:r><w:t>Cell</w:t></w:r></w:p></w:tc>";
    }

    std::string table(const std::string& properties, const std::string& body)
    {
        return "<w:tbl><w:tblPr><w:tblStyle w:val='Derived'/>" + properties +
            "</w:tblPr><w:tblGrid><w:gridCol w:w='1440'/><w:gridCol w:w='1440'/>"
            "<w:gridCol w:w='1440'/></w:tblGrid>" +
            body + "</w:tbl>";
    }

    void inheritance()
    {
        std::string rows;
        for (int i = 0; i < 4; ++i)
            rows += "<w:tr>" + cell() + cell() + cell() + "</w:tr>";
        auto parts = fixture(table("<w:tblLook w:val='01E0'/>", rows));
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables.size() == 1, "conditional table parses");
        if (!loaded.success || loaded.document.tables.empty())
            return;
        const auto& value = loaded.document.tables[0];
        const char* expected[]{"#990000", "#777777", "#009900", "#555555", "#333333", "#666666", "#555555",
            "#333333", "#666666", "#000099", "#888888", "#999900"};
        for (std::size_t i = 0; i < 12; ++i)
            check(value.cells[i].background == expected[i], "Word conditional precedence and corners");
        check(value.border_color == "#123456" && value.border_width == 1.5 &&
                value.cells[0].margins[0] == 5 && value.cells[0].margins[2] == 6,
            "inherited table borders and margins");
        check(loaded.document.paragraphs[0].runs[0].bold &&
                loaded.document.paragraphs[0].runs[0].size == 18 &&
                loaded.document.paragraphs[0].runs[0].color == "#FFFFFF" &&
                loaded.document.paragraphs[3].runs[0].size == 16 &&
                loaded.document.paragraphs[3].alignment == 1,
            "conditional font, absolute table bold and inherited paragraph formatting");
        auto unedited = serialize_word(loaded.document);
        check(unedited.success &&
                part(unedited.parts, "word/document.xml").bytes == part(parts, "word/document.xml").bytes,
            "reading resolved styles does not flatten the source XML");
        loaded.document.paragraphs[0].runs[0].size = 22;
        loaded.document.tables[0].cells[4].background = "#FEDCBA";
        const auto saved = serialize_word(loaded.document);
        const auto reopened = parse_word(saved.parts);
        check(saved.success && reopened.success && reopened.document.paragraphs[0].runs[0].size == 22 &&
                reopened.document.paragraphs[0].runs[0].bold &&
                reopened.document.tables[0].cells[4].background == "#FEDCBA" &&
                reopened.document.tables[0].cells[5].background == "#666666",
            "targeted direct overrides survive save and reopen without flattening other cells");
        for (const auto& original : parts)
            if (original.path != "word/document.xml")
                for (const auto& output : saved.parts)
                    if (output.path == original.path)
                        check(output.bytes == original.bytes, "style definitions and other parts preserved");
    }

    void geometry_and_overrides()
    {
        std::string rows;
        for (int i = 0; i < 5; ++i)
            rows += "<w:tr>" + cell() + cell() + cell() + "</w:tr>";
        auto result = parse_word(fixture(table("<w:tblLook w:val='01E0' w:firstRow='1' w:noVBand='1'/>"
                                               "<w:tblStyleRowBandSize w:val='2'/>",
            rows)));
        check(result.success && result.document.tables[0].cells[0].background == "#777777" &&
                result.document.tables[0].cells[3].background == "#111111" &&
                result.document.tables[0].cells[6].background == "#111111" &&
                result.document.tables[0].cells[9].background == "#222222",
            "explicit tblLook replaces legacy mask, first row excluded from two-row bands");
        auto parts = fixture(table("<w:tblLook w:val='0420'/>",
            "<w:tr>" +
                cell("<w:shd w:fill='ABCDEF'/><w:tcMar><w:top w:w='200'/></w:tcMar>",
                    "<w:pPr><w:pStyle w:val='P'/></w:pPr>") +
                cell("<w:gridSpan w:val='2'/>") + "</w:tr>"));
        auto& style = part(parts, "word/styles.xml").bytes;
        const auto pos = style.find("w:fill=\"777777\"");
        style.insert(pos, "w:themeFill='accent1' w:themeFillTint='99' ");
        result = parse_word(parts);
        check(result.success && result.document.tables[0].cells[0].background == "#ABCDEF" &&
                result.document.tables[0].cells[0].margins[1] == 10 &&
                result.document.tables[0].cells[0].margins[2] == 6 &&
                result.document.paragraphs[0].alignment == 2 &&
                result.document.paragraphs[0].runs[0].color == "#ABCDEF" &&
                result.document.tables[0].cells[1].column_span == 2,
            "direct cell and paragraph styles override table without corrupting spans or other margins");
        const auto inner = table("<w:tblLook w:noHBand='1' w:noVBand='1'/>", "<w:tr>" + cell() + "</w:tr>");
        result = parse_word(fixture(table("<w:tblLook w:val='0420'/>",
            "<w:tr><w:tc><w:p><w:r><w:t>Outer</w:t></w:r></w:p>" + inner + "<w:p/></w:tc></w:tr>")));
        check(result.success && result.document.tables.size() == 2 &&
                result.document.tables[0].cells[0].background == "#777777" &&
                result.document.tables[1].cells[0].background == "#EEEEEE" &&
                result.document.paragraphs[1].runs[0].size == 16,
            "nested table resolves nearest table and does not invalidate outer temporary properties");
    }

    void defaults_and_row_properties()
    {
        auto parts = fixture(table("<w:tblLook w:firstColumn='1' w:noHBand='1' w:noVBand='1'/>",
            "<w:tr>" + cell() + cell("<w:vAlign w:val='bottom'/>") + cell() + "</w:tr>"));
        auto& definition = part(parts, "word/styles.xml").bytes;
        const auto position = definition.find("<w:shd w:fill=\"555555\"");
        definition.insert(position, "<w:vAlign w:val='center'/><w:tcMar><w:top w:w='160'/></w:tcMar>");
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells[0].vertical_alignment == 1 &&
                loaded.document.tables[0].cells[1].vertical_alignment == 2 &&
                loaded.document.tables[0].cells[2].vertical_alignment == 1 &&
                loaded.document.tables[0].cells[2].margins[1] == 8,
            "Word conditional vertical alignment and margins apply to row; direct cell wins");
        parts = fixture("<w:tbl><w:tblPr><w:tblStyleRowBandSize w:val='0'/>"
                        "<w:tblStyleColBandSize w:val='0'/><w:tblBorders><w:top w:val='nil'/>"
                        "</w:tblBorders></w:tblPr><w:tr>" +
            cell() + "</w:tr></w:tbl>");
        loaded = parse_word(parts);
        check(loaded.success && loaded.document.tables[0].cells[0].background == "#EEEEEE" &&
                loaded.document.tables[0].border_width == 0 &&
                loaded.document.paragraphs[0].runs[0].size == 14,
            "default table style, disabled zero-width bands and explicitly absent border");
        loaded = parse_word(fixture(table("<w:tblLook w:val='0420'/>",
            "<w:tr><w:trPr><w:tblHeader/></w:trPr>" + cell() +
                "</w:tr>"
                "<w:tr><w:trPr><w:tblHeader/></w:trPr>" +
                cell() + "</w:tr><w:tr>" + cell() + "</w:tr>")));
        check(loaded.success && loaded.document.tables[0].header_rows == 2 &&
                loaded.document.tables[0].cells[1].background == "#777777" &&
                loaded.document.tables[0].cells[2].background == "#111111",
            "consecutive repeated headers share first-row style before body banding");
        loaded = parse_word(fixture(
            table("<w:tblLook w:val='01E0'/>", "<w:tr>" + cell("<w:gridSpan w:val='3'/>") + "</w:tr>")));
        check(loaded.success && loaded.document.tables[0].cells[0].background == "#999900",
            "single spanning cell uses last matching corner in Word precedence");
    }
}

int run_word_table_style_tests()
{
    inheritance();
    geometry_and_overrides();
    defaults_and_row_properties();
    return failures == 0 ? 0 : 1;
}

int main()
{
    return run_word_table_style_tests();
}
