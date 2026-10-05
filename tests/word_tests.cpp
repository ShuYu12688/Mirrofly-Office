#include <mirrorfly/word.hpp>

#include <algorithm>
#include <iostream>

namespace
{
    int failures = 0;
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }

    void cases()
    {
        using namespace mirrorfly;
        check(word_pinyin(U'中') == "zhōng" && word_pinyin(U'你') == "nǐ" && word_pinyin(U'A').empty(),
            "offline Mandarin data returns tone marks and skips non-Han characters");
        WordDocument document;
        WordRun run;
        run.text = " 中文 <报告> & \"方案\"\t2026\n第二行 ";
        run.font = "Arial";
        run.east_asia_font = "宋体";
        run.bold = true;
        run.italic = true;
        run.underline = true;
        run.strike = true;
        run.outline = true;
        run.script = 1;
        run.color = "#123456";
        run.background = "#FFEEDD";
        run.border_color = "#667788";
        run.character_spacing = 1.5;
        document.paragraphs[0].background = "#CCDDEE";
        document.paragraphs[0].border_color = "#445566";
        run.size = 18.5;
        document.paragraphs[0].runs = {run};
        document.paragraphs[0].heading = 2;
        document.paragraphs[0].alignment = 1;
        document.paragraphs[0].right_to_left = true;
        document.paragraphs[0].line_spacing = 2;
        document.paragraphs[0].list = WordListKind::Numbered;
        document.paragraphs[0].list_level = 1;
        document.paragraphs[0].left_indent = 36;
        document.paragraphs[0].first_line_indent = -18;
        document.paragraphs[0].space_before = 9;
        document.paragraphs[0].space_after = 12;
        document.paragraphs.emplace_back();
        auto written = serialize_word(document);
        check(written.success, "basic document serializes");
        const auto parsed = parse_word(written.parts);
        check(parsed.success && parsed.document.paragraphs.size() == 2, "blank final paragraph round trips");
        if (parsed.success && !parsed.document.paragraphs[0].runs.empty())
        {
            const auto& paragraph = parsed.document.paragraphs[0];
            const auto& restored = paragraph.runs[0];
            check(restored.text == run.text && restored.font == run.font &&
                    restored.east_asia_font == run.east_asia_font && restored.size == run.size &&
                    restored.bold && restored.italic && restored.underline,
                "escaping, whitespace, line breaks, per-script fonts and emphasis survive round trip");
            check(restored.strike && restored.outline && restored.script == 1 &&
                    restored.color == "#123456" && restored.background == "#FFEEDD" &&
                    paragraph.background == "#CCDDEE" && paragraph.border_color == "#445566" &&
                    restored.border_color == "#667788" && restored.character_spacing == 1.5 &&
                    paragraph.right_to_left,
                "extended Word formats round trip");
            check(paragraph.heading == 2 && paragraph.alignment == 1 && paragraph.line_spacing == 2,
                "heading outline and paragraph formatting round trip");
            check(paragraph.list == WordListKind::Numbered && paragraph.list_level == 1 &&
                    paragraph.left_indent == 36 && paragraph.first_line_indent == -18 &&
                    paragraph.space_before == 9 && paragraph.space_after == 12,
                "real numbering, indentation and paragraph spacing round trip");
        }
        const auto numbering =
            std::find_if(written.parts.begin(), written.parts.end(), [](const OfficePart& part)
        {
            return part.path == "word/numbering.xml";
        });
        const auto main = std::find_if(written.parts.begin(), written.parts.end(), [](const OfficePart& part)
        {
            return part.path == "word/document.xml";
        });
        check(numbering != written.parts.end() &&
                numbering->bytes.find("w:numFmt w:val='decimal'") != std::string::npos &&
                main != written.parts.end() && main->bytes.find("w:numPr") != std::string::npos,
            "numbered paragraphs use DOCX numbering structures instead of handwritten prefixes");

        WordDocument templates;
        auto inserted = insert_word_template(templates, 1, WordTemplateKind::SourceRecord);
        check(inserted.success && inserted.inserted_paragraphs == 7 &&
                templates.paragraphs[1].runs[0].text == "来源登记" &&
                templates.paragraphs[2].runs[0].text.find("[待填写]") != std::string::npos,
            "source record template inserts explicit placeholders without invented references");
        inserted =
            insert_word_template(templates, templates.paragraphs.size(), WordTemplateKind::MeetingMinutes);
        check(inserted.success && templates.paragraphs.back().list == WordListKind::Bullet,
            "meeting template includes structured action placeholders and real bullet lists");
        inserted =
            insert_word_template(templates, templates.paragraphs.size(), WordTemplateKind::WeeklyReport);
        check(inserted.success && templates.paragraphs.back().list == WordListKind::Numbered,
            "weekly report template includes a numbered next-plan placeholder");
        const auto template_size = templates.paragraphs.size();
        inserted = insert_word_template(templates, template_size + 1, WordTemplateKind::WeeklyReport);
        check(!inserted.success && templates.paragraphs.size() == template_size,
            "invalid template insertion is atomic");
        auto parts = written.parts;
        parts.back().bytes = "<!DOCTYPE w [<!ENTITY x SYSTEM 'file:///secret'>]><w/>";
        check(!parse_word(parts).success, "DTD is rejected without resource access");
        parts = written.parts;
        parts.push_back(parts.back());
        check(!parse_word(parts).success, "duplicate package paths are rejected");
        parts = written.parts;
        parts[1].bytes =
            "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
            "<Relationship "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument'"
            " Target='https://example.invalid/document' TargetMode='External'/></Relationships>";
        check(!parse_word(parts).success, "external document relationships are not followed");
        parts = written.parts;
        auto& xml = parts.back().bytes;
        std::size_t position = 0;
        while ((position = xml.find("w:", position)) != std::string::npos)
        {
            xml.replace(position, 2, "x:");
        }
        xml.replace(xml.find("xmlns:w"), 7, "xmlns:x");
        check(parse_word(parts).success, "namespace aliases are supported");
        parts = written.parts;
        parts.back().bytes = "<w:document xmlns:w='wrong'><w:body><w:p/></w:body></w:document>";
        check(!parse_word(parts).success, "wrong Word namespace is rejected");
        document.paragraphs[0].runs[0].text.assign(maximum_word_paragraph_bytes + 1, 'x');
        check(!serialize_word(document).success, "oversized paragraphs cannot be saved");
        document.paragraphs[0].runs[0].text = std::string("a\0b", 3);
        check(!serialize_word(document).success, "NUL characters are rejected, not truncated");
        document.paragraphs[0].runs[0].text = "\xFF";
        check(!serialize_word(document).success, "invalid UTF-8 is rejected");
        check(is_word_path("大学报告.DOCX") && !is_word_path("legacy.doc"),
            "DOCX extension is case insensitive");
    }
}

int run_word_tests()
{
    cases();
    return failures ? 1 : 0;
}

int main()
{
    return run_word_tests();
}
