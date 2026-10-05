#include "word_numbering_read.hpp"
#include "word_numbering_variants.hpp"
#include "word_package.hpp"
#include "word_paragraph_layout.hpp"
#include "word_styles.hpp"
#include "word_tabs.hpp"
#include "word_xml.hpp"
#include <mirrorfly/word.hpp>

#include <pugixml.hpp>
#include <utf8.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

namespace
{
    constexpr auto word_ns = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    constexpr auto rel_ns = "http://schemas.openxmlformats.org/package/2006/relationships";
    constexpr auto content_ns = "http://schemas.openxmlformats.org/package/2006/content-types";
    constexpr std::size_t xml_limit = mirrorfly::maximum_word_xml_bytes;

    std::string_view local_name(const char* name)
    {
        const std::string_view value(name);
        const auto colon = value.find(':');
        return colon == value.npos ? value : value.substr(colon + 1);
    }

    bool namespace_is(pugi::xml_node node, const char* name, const char* space)
    {
        const std::string value(name);
        const auto colon = value.find(':');
        const auto declaration =
            colon == value.npos ? std::string("xmlns") : "xmlns:" + value.substr(0, colon);
        for (; node; node = node.parent())
        {
            if (const auto attribute = node.attribute(declaration.c_str()))
            {
                return std::string_view(attribute.value()) == space;
            }
        }
        return false;
    }

    bool named(pugi::xml_node node, const char* name, const char* space = word_ns)
    {
        return local_name(node.name()) == name && namespace_is(node, node.name(), space);
    }

    pugi::xml_node child(pugi::xml_node node, const char* name)
    {
        for (auto item : node.children())
        {
            if (named(item, name))
            {
                return item;
            }
        }
        return {};
    }

    pugi::xml_attribute attribute(pugi::xml_node node, const char* name)
    {
        for (auto item : node.attributes())
        {
            if (local_name(item.name()) == name && namespace_is(node, item.name(), word_ns))
            {
                return item;
            }
        }
        return {};
    }

    bool valid_text(const std::string& text)
    {
        if (!utf8::is_valid(text.begin(), text.end()))
        {
            return false;
        }
        const auto control = [](unsigned char c)
        {
            return c < 32 && c != '\t' && c != '\n' && c != '\r';
        };
        return std::none_of(text.begin(), text.end(), control) && text.find("\xEF\xBF\xBE") == text.npos &&
            text.find("\xEF\xBF\xBF") == text.npos;
    }

    bool xml_read(const std::string& bytes, pugi::xml_document& document)
    {
        if (bytes.size() > xml_limit || !valid_text(bytes) || bytes.find("<!DOCTYPE") != bytes.npos ||
            bytes.find("<!ENTITY") != bytes.npos)
        {
            return false;
        }
        if (!document.load_buffer(
                bytes.data(), bytes.size(), mirrorfly::word_xml::parse_options, pugi::encoding_utf8))
        {
            return false;
        }
        std::vector<std::pair<pugi::xml_node, int>> pending{{document, 0}};
        std::size_t count = 0;
        while (!pending.empty())
        {
            const auto item = pending.back();
            pending.pop_back();
            if (item.second > 96 || ++count > 1000000)
            {
                return false;
            }
            for (auto node : item.first.children())
            {
                pending.emplace_back(node, item.second + 1);
            }
        }
        return true;
    }

    mirrorfly::WordResult failure(const char* message)
    {
        mirrorfly::WordResult result;
        result.error = message;
        return result;
    }

    bool enabled(pugi::xml_node property)
    {
        const std::string_view value = attribute(property, "val").value();
        return property && value != "0" && value != "false" && value != "off" && value != "none" &&
            value != "nil";
    }

    bool valid_color(const std::string& value)
    {
        return value.empty() ||
            (value.size() == 7 && value[0] == '#' &&
                value.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos);
    }

    std::string read_color(pugi::xml_node node, const char* key)
    {
        const std::string value = attribute(node, key).value();
        return value.size() == 6 && valid_color("#" + value) ? "#" + value : std::string{};
    }

    mirrorfly::WordRun read_run(pugi::xml_node node, int heading, pugi::xml_node properties, bool styled)
    {
        mirrorfly::WordRun run;
        if (styled)
            run.font = "Times New Roman";
        if (heading > 0)
        {
            run.bold = true;
            run.size = heading == 1 ? 22 : (heading == 2 ? 18 : 15);
        }
        const auto fonts = child(properties, "rFonts");
        if (const auto font = attribute(fonts, "ascii"))
        {
            run.font = font.value();
        }
        if (const auto font = attribute(fonts, "eastAsia"))
        {
            run.east_asia_font = font.value();
        }
        else
        {
            run.east_asia_font = run.font;
        }
        if (const auto size = attribute(child(properties, "sz"), "val"))
        {
            run.size = std::clamp(size.as_double(24) / 2, 6.0, 96.0);
        }
        if (const auto bold = child(properties, "b"))
        {
            run.bold = enabled(bold);
        }
        run.italic = enabled(child(properties, "i"));
        run.underline = enabled(child(properties, "u"));
        run.double_underline =
            run.underline && std::string_view(attribute(child(properties, "u"), "val").value()) == "double";
        run.strike = enabled(child(properties, "strike")) || enabled(child(properties, "dstrike"));
        run.double_strike = enabled(child(properties, "dstrike"));
        run.outline = enabled(child(properties, "outline"));
        const std::string vertical = attribute(child(properties, "vertAlign"), "val").value();
        run.script = vertical == "superscript" ? 1 : vertical == "subscript" ? -1 : 0;
        run.color = read_color(child(properties, "color"), "val");
        run.background = read_color(child(properties, "shd"), "fill");
        if (enabled(child(properties, "bdr")))
            run.border_color = read_color(child(properties, "bdr"), "color");
        run.character_spacing =
            std::clamp(attribute(child(properties, "spacing"), "val").as_double() / 20, -3.0, 20.0);
        const std::map<std::string, std::string> highlights{{"yellow", "#FFFF00"}, {"green", "#00FF00"},
            {"cyan", "#00FFFF"}, {"magenta", "#FF00FF"}, {"blue", "#0000FF"}, {"red", "#FF0000"},
            {"darkBlue", "#000080"}, {"darkCyan", "#008080"}, {"darkGreen", "#008000"},
            {"darkMagenta", "#800080"}, {"darkRed", "#800000"}, {"darkYellow", "#808000"},
            {"darkGray", "#808080"}, {"lightGray", "#C0C0C0"}, {"black", "#000000"}, {"white", "#FFFFFF"}};
        const auto highlight = highlights.find(attribute(child(properties, "highlight"), "val").value());
        if (highlight != highlights.end())
            run.background = highlight->second;
        for (auto item : node.children())
        {
            if (named(item, "t"))
            {
                run.text += item.text().as_string();
            }
            else if (named(item, "tab"))
            {
                run.text += '\t';
            }
            else if (named(item, "br") || named(item, "cr"))
            {
                run.text += '\n';
            }
        }
        return run;
    }

    std::string xml_bytes(const pugi::xml_document& document)
    {
        std::ostringstream stream;
        document.save(stream, "", pugi::format_raw, pugi::encoding_utf8);
        return stream.str();
    }

    mirrorfly::WordParagraph template_paragraph(const std::string& text, int heading = 0,
        mirrorfly::WordListKind list = mirrorfly::WordListKind::None)
    {
        mirrorfly::WordParagraph paragraph;
        paragraph.heading = heading;
        paragraph.list = list;
        mirrorfly::WordRun run;
        run.text = text;
        if (heading > 0)
        {
            run.bold = true;
            run.size = heading == 1 ? 22 : (heading == 2 ? 18 : 15);
            paragraph.space_after = 8;
        }
        paragraph.runs.push_back(std::move(run));
        return paragraph;
    }

    std::vector<mirrorfly::WordParagraph> template_paragraphs(mirrorfly::WordTemplateKind kind)
    {
        using mirrorfly::WordListKind;
        switch (kind)
        {
        case mirrorfly::WordTemplateKind::SourceRecord:
            return {template_paragraph("来源登记", 2), template_paragraph("标题：[待填写]"),
                template_paragraph("作者或机构：[待填写]"), template_paragraph("发布日期：[待填写]"),
                template_paragraph("链接或出处：[待填写]"), template_paragraph("查阅日期：[待填写]"),
                template_paragraph("备注：[待填写]")};
        case mirrorfly::WordTemplateKind::MeetingMinutes:
            return {template_paragraph("会议纪要", 1), template_paragraph("会议主题：[待填写]"),
                template_paragraph("日期与时间：[待填写]"), template_paragraph("参会人员：[待填写]"),
                template_paragraph("议题", 2), template_paragraph("[待填写]", 0, WordListKind::Bullet),
                template_paragraph("决定事项", 2), template_paragraph("[待填写]", 0, WordListKind::Numbered),
                template_paragraph("后续行动", 2),
                template_paragraph(
                    "负责人：[待填写]；事项：[待填写]；截止日期：[待填写]", 0, WordListKind::Bullet)};
        case mirrorfly::WordTemplateKind::WeeklyReport:
            return {template_paragraph("周报", 1), template_paragraph("周期：[待填写]"),
                template_paragraph("本周完成", 2), template_paragraph("[待填写]", 0, WordListKind::Bullet),
                template_paragraph("进展与依据", 2), template_paragraph("[待填写]", 0, WordListKind::Bullet),
                template_paragraph("问题与风险", 2), template_paragraph("[待填写]", 0, WordListKind::Bullet),
                template_paragraph("下周计划", 2), template_paragraph("[待填写]", 0, WordListKind::Numbered)};
        }
        return {};
    }
}

namespace mirrorfly
{
    bool is_word_path(std::string path)
    {
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
        return path.size() >= 5 && path.substr(path.size() - 5) == ".docx";
    }

    std::string validate_word(const WordDocument& document)
    {
        if (document.paragraphs.empty() || document.paragraphs.size() > maximum_word_paragraphs)
        {
            return "正文需为 1–32768 段。";
        }
        if (!std::isfinite(document.default_tab_stop) || document.default_tab_stop <= 0 ||
            document.default_tab_stop > 1638.35)
            return "默认制表间距无效。";
        std::size_t total = 0;
        std::size_t runs = 0;
        for (const auto& paragraph : document.paragraphs)
        {
            std::size_t length = 0;
            for (const auto& tab : paragraph.tabs)
                if (!valid_text(tab.alignment) || !valid_text(tab.leader))
                    return "制表位文字无效。";
            if (!word_detail::valid_tab_stops(paragraph.tabs))
                return "自定义制表位无效。";
            if (paragraph.heading < 0 || paragraph.heading > 3 || paragraph.alignment < 0 ||
                paragraph.alignment > 4 || !std::isfinite(paragraph.line_spacing) ||
                paragraph.line_spacing < 1 || paragraph.line_spacing > 2 || paragraph.list_level < 0 ||
                (paragraph.list != WordListKind::None && paragraph.list != WordListKind::Bullet &&
                    paragraph.list != WordListKind::Numbered) ||
                paragraph.list_level > 2 || paragraph.list_instance < 0 || paragraph.list_start < 0 ||
                paragraph.list_start > 1000000 || paragraph.list_marker.size() > 64 ||
                paragraph.list_text.size() > 256 || !valid_text(paragraph.list_text) ||
                !valid_text(paragraph.list_marker) || !std::isfinite(paragraph.left_indent) ||
                !std::isfinite(paragraph.first_line_indent) || !std::isfinite(paragraph.space_before) ||
                !std::isfinite(paragraph.space_after) || paragraph.left_indent < 0 ||
                paragraph.left_indent > maximum_word_indent_points || paragraph.first_line_indent < -144 ||
                paragraph.first_line_indent > 144 ||
                paragraph.left_indent + paragraph.first_line_indent < 0 || paragraph.space_before < 0 ||
                paragraph.space_before > maximum_word_spacing_points || paragraph.space_after < 0 ||
                paragraph.space_after > maximum_word_spacing_points || !valid_color(paragraph.background) ||
                !valid_color(paragraph.border_color))
            {
                return "段落格式无效。";
            }
            for (const auto& run : paragraph.runs)
            {
                length += run.text.size() + run.ruby.size();
                if (++runs > 262144 || !valid_text(run.text) || !valid_text(run.font) ||
                    !valid_text(run.east_asia_font) || run.font.size() > 256 ||
                    run.east_asia_font.size() > 256 || !std::isfinite(run.size) || run.size < 6 ||
                    run.size > 96 || run.script < -1 || run.script > 1 || !valid_color(run.color) ||
                    !valid_color(run.background) || !valid_color(run.border_color) ||
                    !std::isfinite(run.character_spacing) || run.character_spacing < -3 ||
                    run.character_spacing > 20 || !valid_text(run.ruby) || run.ruby.size() > 128 ||
                    (!run.ruby.empty() &&
                        (run.text.empty() || run.text.find_first_of("\t\r\n") != std::string::npos ||
                            run.ruby.find_first_of("\t\r\n") != std::string::npos)))
                {
                    return "文本或字体格式无效，或样式片段过多。";
                }
            }
            total += length;
            if (length > maximum_word_paragraph_bytes || total > maximum_word_text_bytes)
            {
                return "正文最多 2 MiB，每段最多 8 KiB。请缩短内容或拆分段落。";
            }
        }
        return word_detail::validate_structure(document);
    }

    WordEditResult insert_word_template(
        WordDocument& document, std::size_t paragraph_index, WordTemplateKind kind)
    {
        WordEditResult result;
        const auto current_error = validate_word(document);
        if (!current_error.empty())
        {
            result.error = current_error;
            return result;
        }
        if (paragraph_index > document.paragraphs.size())
        {
            result.error = "模板插入位置无效。";
            return result;
        }
        auto paragraphs = template_paragraphs(kind);
        if (paragraphs.empty())
        {
            result.error = "模板类型无效。";
            return result;
        }
        WordDocument candidate = document;
        candidate.paragraphs.insert(
            candidate.paragraphs.begin() + static_cast<std::ptrdiff_t>(paragraph_index), paragraphs.begin(),
            paragraphs.end());
        result.error = validate_word(candidate);
        if (!result.error.empty())
        {
            return result;
        }
        document = std::move(candidate);
        result.success = true;
        result.changed = true;
        result.inserted_paragraphs = paragraphs.size();
        return result;
    }

    WordResult parse_word(std::vector<OfficePart> parts)
    {
        try
        {
            std::set<std::string> paths;
            const OfficePart* main = nullptr;
            const OfficePart* relationships = nullptr;
            const OfficePart* types = nullptr;
            const OfficePart* numbering = nullptr;
            std::size_t total = 0;
            for (const auto& part : parts)
            {
                total += part.bytes.size();
                if (parts.size() > 4096 || total > maximum_word_expanded_bytes ||
                    part.bytes.size() > maximum_word_part_bytes || part.path.empty() ||
                    part.path.front() == '/' || part.path.find("..") != part.path.npos ||
                    part.path.find_first_of("\\:") != part.path.npos || !paths.insert(part.path).second)
                {
                    return failure("DOCX 包路径或规模无效。");
                }
                if (part.path == "word/document.xml")
                {
                    main = &part;
                }
                if (part.path == "_rels/.rels")
                {
                    relationships = &part;
                }
                if (part.path == "[Content_Types].xml")
                {
                    types = &part;
                }
            }
            if (!main || !relationships || !types)
            {
                return failure("缺少 DOCX 正文或包声明；当前支持标准 word/document.xml 入口。");
            }
            pugi::xml_document rels, content, xml;
            if (!xml_read(relationships->bytes, rels) || !xml_read(types->bytes, content) ||
                !xml_read(main->bytes, xml))
            {
                return failure("DOCX XML 无效、过大或包含不支持的声明。");
            }
            if (!named(rels.document_element(), "Relationships", rel_ns) ||
                !named(content.document_element(), "Types", content_ns))
            {
                return failure("DOCX 包声明的命名空间无效。");
            }
            bool entry = false;
            for (auto node : rels.document_element().children())
            {
                if (named(node, "Relationship", rel_ns) &&
                    std::string_view(node.attribute("Type").value()) ==
                        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument")
                {
                    const std::string_view target = node.attribute("Target").value();
                    if (entry || std::string_view(node.attribute("TargetMode").value()) == "External" ||
                        (target != "word/document.xml" && target != "/word/document.xml"))
                    {
                        return failure("DOCX 正文入口不支持；不会读取外部资源。");
                    }
                    entry = true;
                }
            }
            bool type = false;
            for (auto node : content.document_element().children())
            {
                if (named(node, "Override", content_ns) &&
                    std::string_view(node.attribute("PartName").value()) == "/word/document.xml" &&
                    std::string_view(node.attribute("ContentType").value()) ==
                        "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml")
                {
                    type = true;
                }
            }
            const auto root = xml.document_element();
            const auto body = child(root, "body");
            if (!entry || !type || !named(root, "document") || !body)
            {
                return failure("文件不是当前支持的 DOCX 文档。");
            }
            WordResult result;
            result.document.paragraphs.clear();
            if (!word_detail::read_tab_interval(parts, result.document.default_tab_stop, result.error))
                return result;
            std::string numbering_error;
            const auto numbering_name = word_detail::numbering_path(parts, numbering_error);
            if (!numbering_error.empty())
            {
                result.error = numbering_error;
                return result;
            }
            numbering = word_xml::part(parts, numbering_name);
            const auto numbering_kinds = word_detail::read_numbering(numbering);
            word_detail::StyleResolver styles;
            if (!styles.load(parts, result.error))
                return result;
            // Read paragraph content before attaching its table and section ownership.
            for (auto node = body.first_child(); node;)
            {
                if (named(node, "p"))
                {
                    WordParagraph paragraph;
                    const auto properties = styles.paragraph_properties(node);
                    if (styles.has_styles())
                    {
                        paragraph.line_spacing = 1;
                        paragraph.space_after = 0;
                    }
                    paragraph.page_break_before = enabled(child(properties, "pageBreakBefore"));
                    paragraph.keep_with_next = enabled(child(properties, "keepNext"));
                    paragraph.background = read_color(child(properties, "shd"), "fill");
                    const auto borders = child(properties, "pBdr");
                    const auto bottom = child(borders, "bottom");
                    if (enabled(bottom))
                    {
                        paragraph.border_color = read_color(bottom, "color");
                        if (paragraph.border_color.empty())
                            paragraph.border_color = "#000000";
                        paragraph.border_bottom_only = !enabled(child(borders, "top"));
                    }
                    const std::string style = attribute(child(properties, "pStyle"), "val").value();
                    if (style == "Heading1" || style == "Heading2" || style == "Heading3")
                    {
                        paragraph.heading = style.back() - '0';
                    }
                    word_detail::read_paragraph_layout(properties, paragraph);
                    const auto number_properties = child(properties, "numPr");
                    const int number_id = attribute(child(number_properties, "numId"), "val").as_int(-1);
                    paragraph.list_instance = std::max(0, number_id);
                    paragraph.list_level =
                        std::clamp(attribute(child(number_properties, "ilvl"), "val").as_int(0), 0, 2);
                    auto list = numbering_kinds.end();
                    if (number_id > 0)
                        list = numbering_kinds.find({number_id, paragraph.list_level});
                    if (number_id > 0 && list == numbering_kinds.end())
                    {
                        list = numbering_kinds.find({number_id, 0});
                    }
                    if (list != numbering_kinds.end())
                    {
                        paragraph.list = list->second.kind;
                        paragraph.list_marker = list->second.marker;
                        paragraph.list_start = list->second.start;
                        paragraph.list_text = list->second.text;
                    }
                    // Inline containers such as hyperlinks contribute visible runs, never their targets.
                    const int fallback_heading = styles.has_styles() ? 0 : paragraph.heading;
                    for (auto item = node.first_child(); item;)
                    {
                        if (named(item, "r"))
                        {
                            const auto ruby = child(item, "ruby");
                            if (ruby)
                            {
                                WordRun combined;
                                bool first = true;
                                for (auto base : child(ruby, "rubyBase").children())
                                    if (named(base, "r"))
                                    {
                                        auto run = read_run(base, fallback_heading,
                                            styles.run_properties(base), styles.has_styles());
                                        if (first)
                                            combined = run;
                                        else
                                            combined.text += run.text;
                                        first = false;
                                    }
                                for (auto annotation : child(ruby, "rt").children())
                                    if (named(annotation, "r"))
                                    {
                                        const auto reading = read_run(annotation, 0,
                                            styles.run_properties(annotation), styles.has_styles());
                                        combined.ruby += reading.text;
                                    }
                                if (!combined.text.empty())
                                    paragraph.runs.push_back(std::move(combined));
                            }
                            else
                                paragraph.runs.push_back(read_run(item, fallback_heading,
                                    styles.run_properties(item), styles.has_styles()));
                        }
                        if (item.first_child() && !named(item, "r") && !named(item, "pPr"))
                        {
                            item = item.first_child();
                        }
                        else
                        {
                            while (item.parent() != node && !item.next_sibling())
                            {
                                item = item.parent();
                            }
                            item = item.next_sibling();
                        }
                    }
                    result.document.paragraphs.push_back(std::move(paragraph));
                    if (result.document.paragraphs.size() > maximum_word_paragraphs)
                    {
                        return failure("DOCX 段落超过 32768 段。");
                    }
                }
                if (node.first_child() && !named(node, "p"))
                {
                    node = node.first_child();
                }
                else
                {
                    while (node.parent() != body && !node.next_sibling())
                    {
                        node = node.parent();
                    }
                    node = node.next_sibling();
                }
            }
            if (result.document.paragraphs.empty())
            {
                result.document.paragraphs.emplace_back();
            }
            word_detail::attach_structure(result.document, body, std::move(parts), styles);
            result.error = validate_word(result.document);
            result.success = result.error.empty();
            return result;
        }
        catch (const std::bad_alloc&)
        {
            return failure("DOCX 内容规模超过可用内存。");
        }
    }

    WordResult serialize_word(const WordDocument& document)
    {
        if (document.source_package)
        {
            return word_detail::serialize_preserved(document);
        }
        if (!document.blocks.empty() || !document.tables.empty() || !document.images.empty() ||
            !document.sections.empty())
            return failure("新建表格、图片和分节的 DOCX 写出尚未完成，未丢弃这些内容。");
        return word_detail::serialize_text(document);
    }

    WordResult word_detail::serialize_text(const WordDocument& document, bool materialize_lists)
    {
        WordResult result;
        result.error = validate_word(document);
        if (!result.error.empty())
        {
            return result;
        }
        pugi::xml_document xml;
        auto root = xml.append_child("w:document");
        root.append_attribute("xmlns:w") = word_ns;
        auto body = root.append_child("w:body");
        const char* alignments[] = {"left", "center", "right", "both", "distribute"};
        for (const auto& paragraph : document.paragraphs)
        {
            auto p = body.append_child("w:p");
            auto properties = p.append_child("w:pPr");
            if (paragraph.heading)
            {
                properties.append_child("w:outlineLvl").append_attribute("w:val") = paragraph.heading - 1;
            }
            properties.append_child("w:jc").append_attribute("w:val") = alignments[paragraph.alignment];
            if (paragraph.page_break_before)
                properties.append_child("w:pageBreakBefore");
            if (paragraph.keep_with_next)
                properties.append_child("w:keepNext");
            if (paragraph.right_to_left)
                properties.append_child("w:bidi").append_attribute("w:val") = "1";
            if (paragraph.list != WordListKind::None)
            {
                auto numbering = properties.append_child("w:numPr");
                numbering.append_child("w:ilvl").append_attribute("w:val") = paragraph.list_level;
                numbering.append_child("w:numId").append_attribute("w:val") =
                    paragraph.list == WordListKind::Bullet ? 1 : 2;
            }
            if (paragraph.left_indent != 0 || paragraph.first_line_indent != 0 || paragraph.right_indent != 0)
            {
                auto indentation = properties.append_child("w:ind");
                indentation.append_attribute("w:left") =
                    static_cast<int>(std::round(paragraph.left_indent * 20));
                indentation.append_attribute("w:right") =
                    static_cast<int>(std::round(paragraph.right_indent * 20));
                if (paragraph.first_line_indent >= 0)
                {
                    indentation.append_attribute("w:firstLine") =
                        static_cast<int>(std::round(paragraph.first_line_indent * 20));
                }
                else
                {
                    indentation.append_attribute("w:hanging") =
                        static_cast<int>(std::round(-paragraph.first_line_indent * 20));
                }
            }
            write_tab_stops(properties, paragraph.tabs);
            auto spacing = properties.append_child("w:spacing");
            spacing.append_attribute("w:line") =
                static_cast<int>(std::round(paragraph.line_spacing_rule ? paragraph.line_spacing_points * 20
                                                                        : paragraph.line_spacing * 240));
            const char* spacing_rule = "auto";
            if (paragraph.line_spacing_rule == 1)
                spacing_rule = "exact";
            else if (paragraph.line_spacing_rule == 2)
                spacing_rule = "atLeast";
            spacing.append_attribute("w:lineRule") = spacing_rule;
            spacing.append_attribute("w:before") = static_cast<int>(std::round(paragraph.space_before * 20));
            spacing.append_attribute("w:after") = static_cast<int>(std::round(paragraph.space_after * 20));
            if (!paragraph.background.empty())
            {
                auto shading = properties.append_child("w:shd");
                shading.append_attribute("w:val") = "clear";
                shading.append_attribute("w:fill") = paragraph.background.substr(1).c_str();
            }
            if (!paragraph.border_color.empty())
            {
                auto borders = properties.append_child("w:pBdr");
                for (const auto* name : {"w:top", "w:left", "w:bottom", "w:right"})
                {
                    if (paragraph.border_bottom_only && std::string(name) != "w:bottom")
                        continue;
                    auto edge = borders.append_child(name);
                    edge.append_attribute("w:val") = "single";
                    edge.append_attribute("w:sz") = 6;
                    edge.append_attribute("w:space") = 2;
                    edge.append_attribute("w:color") = paragraph.border_color.substr(1).c_str();
                }
            }
            const std::vector<const char*> paragraph_order{"w:keepNext", "w:pageBreakBefore", "w:numPr",
                "w:pBdr", "w:shd", "w:tabs", "w:bidi", "w:spacing", "w:ind", "w:jc", "w:outlineLvl"};
            for (const auto* name : paragraph_order)
                if (auto node = properties.child(name))
                    properties.append_move(node);
            for (const auto& run : paragraph.runs)
            {
                auto container = p;
                if (!run.ruby.empty())
                {
                    auto ruby = p.append_child("w:r").append_child("w:ruby");
                    auto ruby_properties = ruby.append_child("w:rubyPr");
                    ruby_properties.append_child("w:rubyAlign").append_attribute("w:val") = "center";
                    ruby_properties.append_child("w:hps").append_attribute("w:val") =
                        static_cast<int>(std::round(run.size));
                    ruby_properties.append_child("w:hpsRaise").append_attribute("w:val") =
                        static_cast<int>(std::round(run.size * 2));
                    ruby_properties.append_child("w:hpsBaseText").append_attribute("w:val") =
                        static_cast<int>(std::round(run.size * 2));
                    ruby_properties.append_child("w:lid").append_attribute("w:val") = "zh-CN";
                    auto annotation = ruby.append_child("w:rt").append_child("w:r");
                    auto size = annotation.append_child("w:rPr").append_child("w:sz");
                    size.append_attribute("w:val") = static_cast<int>(std::round(run.size));
                    annotation.append_child("w:t").text().set(run.ruby.c_str());
                    container = ruby.append_child("w:rubyBase");
                }
                auto r = container.append_child("w:r");
                auto format = r.append_child("w:rPr");
                auto fonts = format.append_child("w:rFonts");
                fonts.append_attribute("w:ascii") = run.font.c_str();
                fonts.append_attribute("w:hAnsi") = run.font.c_str();
                fonts.append_attribute("w:eastAsia") = run.east_asia_font.c_str();
                format.append_child("w:b").append_attribute("w:val") = run.bold ? "1" : "0";
                format.append_child("w:i").append_attribute("w:val") = run.italic ? "1" : "0";
                format.append_child("w:u").append_attribute("w:val") =
                    run.underline ? (run.double_underline ? "double" : "single") : "none";
                format.append_child("w:strike").append_attribute("w:val") =
                    run.strike && !run.double_strike ? "1" : "0";
                format.append_child("w:dstrike").append_attribute("w:val") =
                    run.strike && run.double_strike ? "1" : "0";
                format.append_child("w:outline").append_attribute("w:val") = run.outline ? "1" : "0";
                if (run.script)
                    format.append_child("w:vertAlign").append_attribute("w:val") =
                        run.script > 0 ? "superscript" : "subscript";
                if (run.character_spacing)
                    format.append_child("w:spacing").append_attribute("w:val") =
                        static_cast<int>(std::round(run.character_spacing * 20));
                if (!run.border_color.empty())
                {
                    auto border = format.append_child("w:bdr");
                    border.append_attribute("w:val") = "single";
                    border.append_attribute("w:sz") = "4";
                    border.append_attribute("w:space") = "0";
                    border.append_attribute("w:color") = run.border_color.substr(1).c_str();
                }
                if (!run.color.empty())
                    format.append_child("w:color").append_attribute("w:val") = run.color.substr(1).c_str();
                if (!run.background.empty())
                {
                    auto shading = format.append_child("w:shd");
                    shading.append_attribute("w:val") = "clear";
                    shading.append_attribute("w:fill") = run.background.substr(1).c_str();
                }
                format.append_child("w:sz").append_attribute("w:val") =
                    static_cast<int>(std::round(run.size * 2));
                const std::vector<const char*> order{"w:rFonts", "w:b", "w:i", "w:strike", "w:dstrike",
                    "w:outline", "w:color", "w:spacing", "w:sz", "w:u", "w:bdr", "w:shd", "w:vertAlign"};
                for (const auto* name : order)
                    if (auto node = format.child(name))
                        format.append_move(node);
                std::string text;
                const auto flush = [&]()
                {
                    if (!text.empty())
                    {
                        auto t = r.append_child("w:t");
                        t.append_attribute("xml:space") = "preserve";
                        t.text().set(text.c_str());
                        text.clear();
                    }
                };
                for (char c : run.text)
                {
                    if (c == '\t' || c == '\n' || c == '\r')
                    {
                        flush();
                        r.append_child(c == '\t' ? "w:tab" : "w:br");
                    }
                    else
                    {
                        text += c;
                    }
                }
                flush();
            }
        }
        auto section = body.append_child("w:sectPr");
        auto page = section.append_child("w:pgSz");
        page.append_attribute("w:w") = 11906;
        page.append_attribute("w:h") = 16838;
        auto margins = section.append_child("w:pgMar");
        for (const auto name : {"w:top", "w:right", "w:bottom", "w:left"})
        {
            margins.append_attribute(name) = 1440;
        }
        result.parts = {{"[Content_Types].xml",
                            "<Types xmlns='http://schemas.openxmlformats.org/package/2006/content-types'>"
                            "<Default Extension='rels' "
                            "ContentType='application/vnd.openxmlformats-package.relationships+xml'/>"
                            "<Default Extension='xml' ContentType='application/xml'/>"
                            "<Override PartName='/word/document.xml' "
                            "ContentType='application/"
                            "vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml'/>"
                            "<Override PartName='/word/numbering.xml' "
                            "ContentType='application/"
                            "vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml'/></Types>"},
            {"_rels/.rels",
                "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
                "<Relationship Id='document' "
                "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument' "
                "Target='word/document.xml'/></Relationships>"},
            {"word/_rels/document.xml.rels",
                "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
                "<Relationship Id='numbering' "
                "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering' "
                "Target='numbering.xml'/></Relationships>"},
            {"word/numbering.xml",
                "<w:numbering xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
                "<w:abstractNum w:abstractNumId='0'><w:multiLevelType w:val='multilevel'/>"
                "<w:lvl w:ilvl='0'><w:start w:val='1'/><w:numFmt w:val='bullet'/>"
                "<w:lvlText w:val='•'/></w:lvl>"
                "<w:lvl w:ilvl='1'><w:start w:val='1'/><w:numFmt w:val='bullet'/>"
                "<w:lvlText w:val='◦'/></w:lvl>"
                "<w:lvl w:ilvl='2'><w:start w:val='1'/><w:numFmt w:val='bullet'/>"
                "<w:lvlText w:val='▪'/></w:lvl></w:abstractNum>"
                "<w:abstractNum w:abstractNumId='1'><w:multiLevelType w:val='multilevel'/>"
                "<w:lvl w:ilvl='0'><w:start w:val='1'/><w:numFmt w:val='decimal'/>"
                "<w:lvlText w:val='%1.'/></w:lvl>"
                "<w:lvl w:ilvl='1'><w:start w:val='1'/><w:numFmt w:val='decimal'/>"
                "<w:lvlText w:val='%1.%2.'/></w:lvl>"
                "<w:lvl w:ilvl='2'><w:start w:val='1'/><w:numFmt w:val='decimal'/>"
                "<w:lvlText w:val='%1.%2.%3.'/></w:lvl></w:abstractNum>"
                "<w:num w:numId='1'><w:abstractNumId w:val='0'/></w:num>"
                "<w:num w:numId='2'><w:abstractNumId w:val='1'/></w:num></w:numbering>"},
            {"word/document.xml", xml_bytes(xml)}};
        write_tab_interval(result.parts, document.default_tab_stop);
        if (materialize_lists)
            result.error = materialize_numbering(result.parts, document);
        if (!result.error.empty())
            return result;
        result.success = true;
        return result;
    }
}
