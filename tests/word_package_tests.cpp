#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <iostream>
#include <sstream>

namespace
{
    int failures = 0;

    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }

    std::vector<mirrorfly::OfficePart> fixture()
    {
        auto parts = mirrorfly::serialize_word(mirrorfly::WordDocument{}).parts;
        parts.back().bytes =
            R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:x="urn:preserved-extension">
<w:body><w:p><w:pPr><w:keepNext/><w:tabs><w:tab w:val="left" w:pos="300"/></w:tabs></w:pPr>
<w:bookmarkStart w:id="3" w:name="anchor"/><w:r><w:rPr><x:color x:val="retain"/><w:lang w:val="zh-CN"/></w:rPr><w:t>Title</w:t></w:r><w:bookmarkEnd w:id="3"/></w:p>
<w:tbl><w:tblPr><w:tblW w:w="6000" w:type="dxa"/></w:tblPr><w:tblGrid><w:gridCol w:w="3000"/><w:gridCol w:w="3000"/></w:tblGrid>
<w:tr><w:trPr><w:tblHeader/></w:trPr><w:tc><w:tcPr><w:vMerge w:val="restart"/></w:tcPr><w:p><w:r><w:t>merged cell</w:t></w:r></w:p></w:tc>
<w:tc><w:p><w:r><w:t>Cell one</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:tc><w:tcPr><w:vMerge/></w:tcPr><w:p/></w:tc><w:tc><w:p><w:r><w:t>Cell two</w:t></w:r></w:p></w:tc></w:tr></w:tbl>
<w:p><w:r><w:t>Tail</w:t></w:r><w:r><w:footnoteReference w:id="7"/></w:r></w:p>
<w:sectPr><w:pgSz w:w="16838" w:h="11906"/><w:pgMar w:top="900" w:bottom="900" w:left="1200" w:right="1200"/></w:sectPr>
</w:body></w:document>)xml";
        parts.push_back({"word/custom-preserved.xml", "<unknown>retain me</unknown>"});
        return parts;
    }

    mirrorfly::OfficePart* find_part(std::vector<mirrorfly::OfficePart>& parts, const std::string& path)
    {
        for (auto& part : parts)
            if (part.path == path)
                return &part;
        return nullptr;
    }

    void test_numbering()
    {
        using namespace mirrorfly;
        auto parts = fixture();
        auto loaded = parse_word(parts);
        loaded.document.paragraphs.front().list = WordListKind::Bullet;
        loaded.document.paragraphs.front().list_level = 2;
        loaded.document.paragraphs[2].list = WordListKind::Numbered;
        auto saved = serialize_word(loaded.document);
        check(saved.success, "add body and table lists without replacing source definitions");
        if (!saved.success)
        {
            std::cerr << saved.error << '\n';
            return;
        }
        auto reopened = parse_word(saved.parts);
        check(reopened.success && reopened.document.paragraphs.front().list == WordListKind::Bullet &&
                reopened.document.paragraphs.front().list_level == 2 &&
                reopened.document.paragraphs[2].list == WordListKind::Numbered,
            "new list kinds and levels survive save and reopen");
        pugi::xml_document old_xml, new_xml, document;
        old_xml.load_string(find_part(parts, "word/numbering.xml")->bytes.c_str());
        new_xml.load_string(find_part(saved.parts, "word/numbering.xml")->bytes.c_str());
        document.load_string(find_part(saved.parts, "word/document.xml")->bytes.c_str());
        const auto abstracts = new_xml.document_element().children("w:abstractNum");
        const auto numbers = new_xml.document_element().children("w:num");
        check(std::distance(abstracts.begin(), abstracts.end()) == 4 &&
                std::distance(numbers.begin(), numbers.end()) == 4,
            "existing numbering IDs are not reused");
        for (auto node : old_xml.document_element().children())
        {
            const auto id = node.first_attribute();
            std::ostringstream original, updated;
            node.print(original, "", pugi::format_raw);
            new_xml.document_element()
                .find_child_by_attribute(node.name(), id.name(), id.value())
                .print(updated, "", pugi::format_raw);
            check(original.str() == updated.str(), "pre-existing numbering definitions remain identical");
        }
        const auto before_level = find_part(saved.parts, "word/numbering.xml")->bytes;
        reopened.document.paragraphs.front().list_level = 1;
        auto level = serialize_word(reopened.document);
        check(level.success && find_part(level.parts, "word/numbering.xml")->bytes == before_level &&
                parse_word(level.parts).document.paragraphs.front().list_level == 1,
            "changing level retains original numbering instance and definitions");
        reopened.document.paragraphs.front().list = WordListKind::None;
        auto removed = serialize_word(reopened.document);
        check(removed.success &&
                parse_word(removed.parts).document.paragraphs.front().list == WordListKind::None,
            "removing numbering saves and reopens without a list");
        document.load_string(find_part(removed.parts, "word/document.xml")->bytes.c_str());
        check(std::string(document.child("w:document")
                      .child("w:body")
                      .child("w:p")
                      .child("w:pPr")
                      .child("w:numPr")
                      .child("w:numId")
                      .attribute("w:val")
                      .value()) == "0",
            "list removal explicitly cancels inherited numbering");
        reopened.document.paragraphs.front().list = WordListKind::Numbered;
        auto changed = serialize_word(reopened.document);
        check(changed.success &&
                parse_word(changed.parts).document.paragraphs.front().list == WordListKind::Numbered,
            "switching list kind patches the concrete ID, including canonical-ID collisions");

        parts = fixture();
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                        [](const auto& part)
        {
            return part.path == "word/numbering.xml" || part.path == "word/_rels/document.xml.rels";
        }),
            parts.end());
        auto* types = find_part(parts, "[Content_Types].xml");
        pugi::xml_document type_xml;
        type_xml.load_string(types->bytes.c_str());
        const auto definition = type_xml.document_element().find_child_by_attribute(
            "Override", "PartName", "/word/numbering.xml");
        definition.parent().remove_child(definition);
        std::ostringstream type_output;
        type_xml.print(type_output, "", pugi::format_raw);
        types->bytes = type_output.str();
        loaded = parse_word(parts);
        loaded.document.paragraphs.front().list = WordListKind::Bullet;
        saved = serialize_word(loaded.document);
        check(saved.success &&
                parse_word(saved.parts).document.paragraphs.front().list == WordListKind::Bullet &&
                find_part(saved.parts, "word/_rels/document.xml.rels") &&
                find_part(saved.parts, "word/numbering.xml"),
            "first list creates a package-local relationship and content declaration");

        parts = fixture();
        find_part(parts, "word/numbering.xml")->path = "word/lists/custom.xml";
        auto* rels = find_part(parts, "word/_rels/document.xml.rels");
        auto position = rels->bytes.find("Target='numbering.xml'");
        rels->bytes.replace(
            position, std::string("Target='numbering.xml'").size(), "Target='lists/custom.xml'");
        loaded = parse_word(parts);
        loaded.document.paragraphs.front().list = WordListKind::Bullet;
        saved = serialize_word(loaded.document);
        check(saved.success && !find_part(saved.parts, "word/numbering.xml") &&
                parse_word(saved.parts).document.paragraphs.front().list == WordListKind::Bullet,
            "numbering is resolved through its relationship rather than a hard-coded filename");
        auto broken = parts;
        find_part(broken, "word/_rels/document.xml.rels")->bytes =
            "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
            "<Relationship Id='bad' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering' "
            "Target='https://example.invalid/numbering.xml' TargetMode='External'/></Relationships>";
        check(!parse_word(broken).success, "external numbering relationships are rejected without fetching");
        broken = parts;
        auto* number = find_part(broken, "word/lists/custom.xml");
        number->bytes.insert(number->bytes.find("</w:numbering>"),
            "<w:num w:numId='0001'><w:abstractNumId w:val='0'/></w:num>");
        loaded = parse_word(broken);
        loaded.document.paragraphs.front().list = WordListKind::Bullet;
        check(!serialize_word(loaded.document).success, "numerically duplicate numbering IDs are rejected");
    }

    void test_namespace_shadowing()
    {
        using namespace mirrorfly;
        auto parts = fixture();
        find_part(parts, "word/document.xml")->bytes =
            R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:m="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
<m:p xmlns:w="urn:foreign-properties"><m:r><m:rPr><w:color w:val="retain"/></m:rPr><m:t>Alias</m:t></m:r></m:p>
</w:body></w:document>)xml";
        auto loaded = parse_word(parts);
        check(loaded.success, "namespace-shadowed Word paragraph loads");
        loaded.document.paragraphs[0].runs[0].color = "#AABBCC";
        loaded.document.paragraphs[0].runs[0].text = "Changed";
        auto saved = serialize_word(loaded.document);
        const auto reopened = parse_word(saved.parts);
        check(saved.success && reopened.success &&
                reopened.document.paragraphs[0].runs[0].color == "#AABBCC" &&
                reopened.document.paragraphs[0].runs[0].text == "Changed" &&
                find_part(saved.parts, "word/document.xml")->bytes.find("urn:foreign-properties") !=
                    std::string::npos,
            "new formatting uses an unshadowed prefix while foreign properties retain their namespace");
    }

    void test_preserved_whitespace()
    {
        using namespace mirrorfly;
        auto parts = fixture();
        find_part(parts, "word/document.xml")->bytes =
            R"xml(<?mirrorfly retain?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math"><w:body>
<!--retained-comment--><w:p><w:r><w:t>Title</w:t></w:r><w:r><w:t xml:space="preserve">   </w:t></w:r></w:p>
<w:p><m:oMathPara><m:oMath><m:r><m:t xml:space="preserve">      </m:t></m:r></m:oMath></m:oMathPara></w:p>
</w:body></w:document>)xml";
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.paragraphs[0].runs[1].text == "   ",
            "whitespace-only text runs retain their visible spaces");
        loaded.document.paragraphs[0].runs[0].text = "Changed";
        auto saved = serialize_word(loaded.document);
        auto* main = find_part(saved.parts, "word/document.xml");
        check(saved.success && main && main->bytes.find("<!--retained-comment-->") != std::string::npos &&
                main->bytes.find("<?mirrorfly retain?>") != std::string::npos &&
                main->bytes.find("<m:t xml:space=\"preserve\">      </m:t>") != std::string::npos &&
                parse_word(saved.parts).document.paragraphs[0].runs[1].text == "   ",
            "unrelated edits preserve formula spaces, XML comments and processing instructions");
    }
}

int run_word_package_tests()
{
    using namespace mirrorfly;
    test_numbering();
    test_namespace_shadowing();
    test_preserved_whitespace();
    const auto parts = fixture();
    auto loaded = parse_word(parts);
    check(loaded.success, "structured Word fixture loads");
    if (!loaded.success)
        return 1;
    check(!word_paragraph_capabilities(loaded.document, 0).split &&
            word_paragraph_capabilities(loaded.document, 1).split &&
            !word_paragraph_capabilities(loaded.document, loaded.document.paragraphs.size()).remove,
        "public capabilities distinguish protected paragraphs, editable cells and invalid indices");
    check(loaded.document.paragraphs.size() == 6 && loaded.document.tables.size() == 1 &&
            loaded.document.tables.front().cells.size() == 3 &&
            loaded.document.tables.front().cells.front().row_span == 2,
        "table grid and vertical merge are modeled without losing source paragraphs");
    check(loaded.document.blocks.size() == 3 && loaded.document.sections.size() == 1 &&
            loaded.document.sections.front().width > loaded.document.sections.front().height,
        "body reading order and landscape section are retained");
    const auto unchanged = serialize_word(loaded.document);
    check(unchanged.success && unchanged.parts.size() == parts.size(), "unchanged package serializes");
    if (unchanged.parts.size() == parts.size())
        for (std::size_t index = 0; index < parts.size(); ++index)
            check(parts[index].path == unchanged.parts[index].path &&
                    parts[index].bytes == unchanged.parts[index].bytes,
                "unchanged part remains byte-identical");
    loaded.document.paragraphs.front().runs.front().text = "Edited title";
    loaded.document.paragraphs.front().runs.front().bold = true;
    loaded.document.paragraphs.front().runs.front().color = "#ABCDEF";
    loaded.document.paragraphs[2].runs.front().color = "#123456";
    const auto edited = serialize_word(loaded.document);
    check(edited.success, "body text and table-cell text styles patch the original package");
    if (!edited.success)
        std::cerr << edited.error << '\n';
    else
    {
        check(edited.parts.back().bytes == parts.back().bytes, "unknown part is untouched by text edit");
        const auto reopened = parse_word(edited.parts);
        check(reopened.success && reopened.document.paragraphs.front().runs.front().text == "Edited title" &&
                reopened.document.paragraphs.front().runs.front().bold &&
                reopened.document.paragraphs[2].runs.front().color == "#123456" &&
                reopened.document.tables.front().cells.front().row_span == 2,
            "text edits survive reopening and keep table structure");
        const auto main = std::find_if(edited.parts.begin(), edited.parts.end(), [](const auto& part)
        {
            return part.path == "word/document.xml";
        });
        check(main != edited.parts.end() && main->bytes.find("bookmarkStart") != std::string::npos &&
                main->bytes.find("footnoteReference") != std::string::npos &&
                main->bytes.find("w:tabs") != std::string::npos &&
                main->bytes.find("w:lang") != std::string::npos &&
                main->bytes.find("<x:color x:val=\"retain\"") != std::string::npos,
            "unknown paragraph and run properties, bookmarks and footnotes remain");
    }
    auto invalid = loaded.document;
    invalid.tables.front().border_width += 1;
    check(!serialize_word(invalid).success, "unimplemented table edits cannot report a successful save");
    auto styled = loaded.document;
    auto& cell = styled.tables.front().cells.front();
    cell.background = "#123456";
    cell.vertical_alignment = 2;
    cell.margins = {9, 12, 15, 18};
    auto styled_result = serialize_word(styled);
    check(styled_result.success, "cell-only edits do not take the unchanged-text shortcut");
    if (styled_result.success)
    {
        auto reopened = parse_word(styled_result.parts);
        const auto& read = reopened.document.tables.front().cells.front();
        check(reopened.success && read.background == cell.background && read.vertical_alignment == 2 &&
                read.margins == cell.margins && read.row_span == 2,
            "cell fill, alignment and four margins survive merged-cell roundtrip");
        for (const auto& part : parts)
            if (part.path != "word/document.xml")
                check(find_part(styled_result.parts, part.path)->bytes == part.bytes,
                    "cell edits preserve every unrelated package part");
        pugi::xml_document xml;
        xml.load_string(find_part(styled_result.parts, "word/document.xml")->bytes.c_str());
        const auto table = xml.child("w:document").child("w:body").child("w:tbl");
        check(std::string(table.child("w:tr")
                      .next_sibling("w:tr")
                      .child("w:tc")
                      .child("w:tcPr")
                      .child("w:shd")
                      .attribute("w:fill")
                      .value()) == "123456",
            "vertical merge continuation receives the same shading");
        reopened.document.tables.front().cells.front().background.clear();
        const auto cleared = serialize_word(reopened.document);
        check(cleared.success &&
                parse_word(cleared.parts).document.tables.front().cells.front().background.empty(),
            "clearing cell shading survives reopen");
    }
    styled.tables.front().cells.front().vertical_alignment = 3;
    check(!serialize_word(styled).success, "invalid cell alignment is rejected");
    styled.tables.front().cells.front().vertical_alignment = 0;
    styled.tables.front().cells.front().background = "broken";
    check(!serialize_word(styled).success, "invalid cell color is rejected");
    invalid = loaded.document;
    invalid.blocks.erase(invalid.blocks.begin());
    check(!serialize_word(invalid).success, "unreferenced live source paragraphs cannot silently reappear");
    invalid = loaded.document;
    invalid.paragraphs[1].source_id = invalid.paragraphs[0].source_id;
    check(!serialize_word(invalid).success, "duplicate source paragraph identities are rejected");
    invalid = loaded.document;
    invalid.sections.front().paragraph_count -= 1;
    check(!serialize_word(invalid).success, "section ranges must match actual paragraph ownership");
    invalid = loaded.document;
    invalid.sections.front().width += 10;
    check(!serialize_word(invalid).success, "unimplemented section edits cannot report a successful save");
    invalid.source_package.reset();
    check(!serialize_word(invalid).success, "structured documents cannot fall through to a text-only writer");
    invalid = loaded.document;
    invalid.tables.front().cells.front().row_span = 1000000;
    check(!validate_word(invalid).empty(), "unbounded table merge rejected before rendering");
    invalid = loaded.document;
    invalid.tables.front().cells.front().blocks.push_back({WordBlock::Kind::Table, 0});
    check(!validate_word(invalid).empty(), "cyclic table graph rejected before rendering");
    return failures ? 1 : 0;
}

int main()
{
    return run_word_package_tests();
}
