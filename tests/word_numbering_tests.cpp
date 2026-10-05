#include "word_list_fixture.hpp"
#include <iostream>
#include <pugixml.hpp>
#include <sstream>

namespace
{
    using namespace mirrorfly;
    using namespace word_list_test;
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value)
        {
            ++failures;
            std::cerr << message << '\n';
        }
    }
    std::string xml_text(pugi::xml_node node)
    {
        std::ostringstream output;
        node.print(output);
        return output.str();
    }
    void preserved()
    {
        auto original = fixture();
        auto loaded = parse_word(original);
        check(loaded.success && loaded.document.paragraphs[0].list_marker == "upperRoman" &&
                loaded.document.paragraphs[0].list_start == 5 &&
                loaded.document.paragraphs[0].list_text == "(%1)" &&
                loaded.document.paragraphs[3].list_marker == "upperLetter" &&
                loaded.document.paragraphs[3].list_start == 27,
            "abstract level, level replacement and start override resolve in order");
        auto unchanged = serialize_word(loaded.document);
        for (auto& source : original)
            check(unchanged.success && part(unchanged.parts, source.path).bytes == source.bytes,
                "unchanged numbering and all other package parts remain byte identical");
        for (const std::string marker : {"lowerRoman", "lowerLetter", "upperLetter", "decimal"})
        {
            auto changed = loaded.document;
            changed.paragraphs[0].list_marker = marker;
            changed.paragraphs[0].list_start = 8;
            auto saved = serialize_word(changed);
            const auto reopened = parse_word(saved.parts);
            check(saved.success && reopened.success &&
                    reopened.document.paragraphs[0].list_marker == marker &&
                    reopened.document.paragraphs[0].list_start == 8 &&
                    reopened.document.paragraphs[0].list_instance != 11 &&
                    reopened.document.paragraphs[2].list_instance == 11 &&
                    reopened.document.paragraphs[2].list_start == 5,
                "marker-only and start edits cannot bypass preserved no-op or affect unselected siblings");
            pugi::xml_document before, after;
            before.load_string(part(original, "word/numbering.xml").bytes.c_str());
            after.load_string(part(saved.parts, "word/numbering.xml").bytes.c_str());
            auto root = after.document_element();
            for (auto node : before.document_element().children())
            {
                const auto key = std::string(node.name()) == "w:num" ? "w:numId" : "w:abstractNumId";
                check(xml_text(node) ==
                        xml_text(root.find_child_by_attribute(node.name(), key, node.attribute(key).value())),
                    "original concrete and abstract definitions are unchanged");
            }
            before.load_string(part(original, "word/document.xml").bytes.c_str());
            after.load_string(part(saved.parts, "word/document.xml").bytes.c_str());
            auto old = before.document_element().child("w:body").first_child();
            auto current = after.document_element().child("w:body").first_child();
            check(std::string(current.attribute("x:keep").value()) == "paragraph" &&
                    current.child("w:pPr").child("w:keepNext") &&
                    current.child("w:r").child("w:rPr").child("w:b"),
                "paragraph extensions, keepNext and bold survive list-only edits");
            for (old = old.next_sibling(), current = current.next_sibling(); old;
                old = old.next_sibling(), current = current.next_sibling())
                check(xml_text(old) == xml_text(current), "unselected paragraphs are semantically untouched");
        }
        auto marker_only = loaded.document;
        marker_only.paragraphs[0].list_marker = "decimal";
        auto saved = serialize_word(marker_only);
        check(saved.success && parse_word(saved.parts).document.paragraphs[0].list_marker == "decimal",
            "only marker change is serialized even when canonical paragraph XML is equal");
        auto level_only = loaded.document;
        level_only.paragraphs[0].list_level = 1;
        saved = serialize_word(level_only);
        check(saved.success &&
                part(saved.parts, "word/numbering.xml").bytes == part(original, "word/numbering.xml").bytes &&
                parse_word(saved.parts).document.paragraphs[0].list_marker == "lowerLetter",
            "level-only edits keep the original instance and resolve its target definition");
    }
    void inherited()
    {
        auto parts = fixture();
        auto& body = part(parts, "word/document.xml").bytes;
        const std::string direct = "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"11\"/></w:numPr>";
        body.replace(body.find(direct), direct.size(), "<w:pStyle w:val=\"InheritedList\"/>");
        auto& rels = part(parts, "word/_rels/document.xml.rels").bytes;
        rels.insert(rels.find("</Relationships>"),
            "<Relationship Id='listStyles' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles' "
            "Target='list-styles.xml'/>");
        parts.push_back({"word/list-styles.xml", R"xml(
<q:styles xmlns:q="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<q:style q:type="paragraph" q:styleId="BaseList"><q:pPr><q:numPr><q:ilvl q:val="0"/>
<q:numId q:val="11"/></q:numPr></q:pPr></q:style>
<q:style q:type="paragraph" q:styleId="InheritedList"><q:basedOn q:val="BaseList"/></q:style>
</q:styles>)xml"});
        auto loaded = parse_word(parts);
        check(loaded.success && loaded.document.paragraphs[0].list_start == 5 &&
                loaded.document.paragraphs[0].list_marker == "upperRoman",
            "style chain and namespace aliases resolve list start and marker");
        loaded.document.paragraphs[0].list_start = 9;
        auto saved = serialize_word(loaded.document);
        const auto reopened = parse_word(saved.parts);
        check(saved.success && reopened.success && reopened.document.paragraphs[0].list_start == 9 &&
                reopened.document.paragraphs[2].list_start == 5 &&
                part(saved.parts, "word/list-styles.xml").bytes == part(parts, "word/list-styles.xml").bytes,
            "editing an inherited list adds direct independent numbering without altering shared styles");
    }

    void authored()
    {
        for (const std::string marker :
            {"disc", "circle", "square", "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman"})
        {
            WordDocument document;
            auto& paragraph = document.paragraphs[0];
            paragraph.runs = {{"authored"}};
            paragraph.list_marker = marker;
            paragraph.list = marker == "disc" || marker == "circle" || marker == "square"
                ? WordListKind::Bullet
                : WordListKind::Numbered;
            paragraph.list_start = 6;
            paragraph.list_instance = 17;
            document.paragraphs.push_back(paragraph);
            const auto saved = serialize_word(document);
            const auto result = parse_word(saved.parts);
            check(saved.success && result.success && result.document.paragraphs[0].list_marker == marker &&
                    result.document.paragraphs[1].list_start == 6 &&
                    result.document.paragraphs[0].list_instance ==
                        result.document.paragraphs[1].list_instance,
                "all eight authored marker styles and shared list starts round trip");
            document.paragraphs[0].list_start = -1;
            check(!serialize_word(document).success, "negative starts reject");
            document.paragraphs[0].list_start = 1000001;
            check(!serialize_word(document).success, "unbounded starts reject");
        }
        auto parts = fixture();
        auto& xml = part(parts, "word/numbering.xml").bytes;
        const auto position = xml.find("<w:start w:val=\"3\"/>");
        xml.erase(position, std::string("<w:start w:val=\"3\"/>").size());
        const auto start = xml.find("<w:startOverride w:val=\"5\"/>");
        xml.erase(start, std::string("<w:startOverride w:val=\"5\"/>").size());
        check(
            parse_word(parts).document.paragraphs[0].list_start == 0, "omitted OOXML start defaults to zero");
    }
}
int run_word_numbering_tests()
{
    preserved();
    authored();
    inherited();
    return failures ? 1 : 0;
}
int main()
{
    return run_word_numbering_tests();
}
