#include "presentation_find_replace.hpp"

#include <pugixml.hpp>
#include <utf8.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace
{
    using mirrorfly::PresentationParagraph;
    using mirrorfly::PresentationTextMatch;
    using Node = pugi::xml_node;

    std::string local(const char* name)
    {
        const auto colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    Node child(Node parent, const char* name)
    {
        for (auto node : parent.children())
            if (local(node.name()) == name)
                return node;
        return {};
    }

    bool valid_text(const std::string& value, bool allow_empty)
    {
        if ((!allow_empty && value.empty()) || !utf8::is_valid(value.begin(), value.end()))
            return false;
        for (unsigned char character : value)
            if (character < 32 || character == 127)
                return false;
        return true;
    }

    unsigned char fold(unsigned char character, bool case_sensitive)
    {
        if (!case_sensitive && character >= 'A' && character <= 'Z')
            return character + ('a' - 'A');
        return character;
    }

    bool matches_at(
        const std::string& text, std::size_t position, const std::string& query, bool case_sensitive)
    {
        if (position > text.size() || query.size() > text.size() - position)
            return false;
        for (std::size_t index = 0; index < query.size(); ++index)
            if (fold(static_cast<unsigned char>(text[position + index]), case_sensitive) !=
                fold(static_cast<unsigned char>(query[index]), case_sensitive))
                return false;
        return true;
    }

    std::string paragraph_text(const PresentationParagraph& paragraph)
    {
        std::string result;
        for (const auto& run : paragraph.runs)
            result += run.text;
        return result;
    }

    bool plain_span(const PresentationParagraph& paragraph, std::size_t start, std::size_t length)
    {
        const std::size_t end = start + length;
        std::size_t position = 0;
        for (const auto& run : paragraph.runs)
        {
            const auto next = position + run.text.size();
            if (position < end && next > start && !run.plain_text)
                return false;
            position = next;
        }
        return true;
    }

    std::string excerpt(const std::string& text, std::size_t start, std::size_t length)
    {
        std::size_t first = start > 48 ? start - 48 : 0;
        while (first > 0 && (static_cast<unsigned char>(text[first]) & 0xC0) == 0x80)
            --first;
        std::size_t last = std::min(text.size(), start + length + 48);
        while (last < text.size() && (static_cast<unsigned char>(text[last]) & 0xC0) == 0x80)
            ++last;
        return text.substr(first, last - first);
    }

    void replace_span(PresentationParagraph& paragraph, std::size_t start, std::size_t length,
        const std::string& replacement)
    {
        std::size_t position = 0;
        std::size_t first = paragraph.runs.size();
        std::size_t last = paragraph.runs.size();
        std::size_t first_offset = 0;
        std::size_t last_offset = 0;
        const auto end = start + length;
        for (std::size_t index = 0; index < paragraph.runs.size(); ++index)
        {
            const auto next = position + paragraph.runs[index].text.size();
            if (first == paragraph.runs.size() && start >= position && start < next)
            {
                first = index;
                first_offset = start - position;
            }
            if (end > position && end <= next)
            {
                last = index;
                last_offset = end - position;
                break;
            }
            position = next;
        }
        if (first == paragraph.runs.size() || last == paragraph.runs.size())
            throw std::runtime_error("文字匹配位置已失效。");
        if (first == last)
        {
            paragraph.runs[first].text.replace(first_offset, length, replacement);
            return;
        }
        paragraph.runs[first].text.resize(first_offset);
        paragraph.runs[first].text += replacement;
        for (std::size_t index = first + 1; index < last; ++index)
            paragraph.runs[index].text.clear();
        paragraph.runs[last].text.erase(0, last_offset);
    }

    Node shape_node(Node tree, const std::string& source_id)
    {
        for (auto node : tree.children())
        {
            const auto kind = local(node.name());
            if (kind != "sp" && kind != "cxnSp")
                continue;
            for (auto metadata : node.children())
                if (std::string(child(metadata, "cNvPr").attribute("id").value()) == source_id)
                    return node;
        }
        return {};
    }

    std::vector<Node> named_children(Node parent, const char* name)
    {
        std::vector<Node> result;
        for (auto node : parent.children())
            if (local(node.name()) == name)
                result.push_back(node);
        return result;
    }

    std::vector<Node> text_runs(Node paragraph)
    {
        std::vector<Node> result;
        for (auto node : paragraph.children())
        {
            const auto kind = local(node.name());
            if (kind == "r" || kind == "fld" || kind == "br" || kind == "tab" || kind == "m")
                result.push_back(node);
        }
        return result;
    }

    void patch_shape_text(
        Node tree, const mirrorfly::PresentationShape& before, const mirrorfly::PresentationShape& after)
    {
        auto node = shape_node(tree, before.source_id);
        auto paragraphs = named_children(child(node, "txBody"), "p");
        if (!node || before.text.paragraphs.size() != after.text.paragraphs.size() ||
            paragraphs.size() != before.text.paragraphs.size())
            throw std::runtime_error("目标文字结构无法安全局部替换。");
        for (std::size_t paragraph_index = 0; paragraph_index < paragraphs.size(); ++paragraph_index)
        {
            const auto& old_paragraph = before.text.paragraphs[paragraph_index];
            const auto& new_paragraph = after.text.paragraphs[paragraph_index];
            auto runs = text_runs(paragraphs[paragraph_index]);
            if (runs.size() != old_paragraph.runs.size() || runs.size() != new_paragraph.runs.size())
                throw std::runtime_error("目标文字片段无法安全对应原始文件。");
            for (std::size_t run_index = 0; run_index < runs.size(); ++run_index)
            {
                const auto& old_run = old_paragraph.runs[run_index];
                const auto& new_run = new_paragraph.runs[run_index];
                if (old_run.text == new_run.text)
                    continue;
                auto text = child(runs[run_index], "t");
                if (!old_run.plain_text || local(runs[run_index].name()) != "r" || !text ||
                    std::string(text.text().as_string()) != old_run.text)
                    throw std::runtime_error("目标文字包含字段或复杂结构，无法安全替换。");
                text.text().set(new_run.text.c_str());
                if (!new_run.text.empty() && (new_run.text.front() == ' ' || new_run.text.back() == ' '))
                {
                    auto space = text.attribute("xml:space");
                    if (!space)
                        space = text.append_attribute("xml:space");
                    space = "preserve";
                }
            }
        }
    }
}

namespace mirrorfly
{
    std::vector<PresentationTextMatch> find_presentation_text(const PresentationScene& scene,
        const std::string& query, bool case_sensitive, std::size_t max_results)
    {
        std::vector<PresentationTextMatch> results;
        if (!valid_text(query, false) || query.size() > 256 || max_results == 0)
            return results;
        for (std::size_t slide_index = 0; slide_index < scene.slides.size(); ++slide_index)
        {
            const auto& slide = scene.slides[slide_index];
            for (std::size_t shape_index = 0; shape_index < slide.shapes.size(); ++shape_index)
            {
                const auto& shape = slide.shapes[shape_index];
                for (std::size_t paragraph_index = 0; paragraph_index < shape.text.paragraphs.size();
                    ++paragraph_index)
                {
                    const auto& paragraph = shape.text.paragraphs[paragraph_index];
                    const auto text = paragraph_text(paragraph);
                    for (std::size_t start = 0; start + query.size() <= text.size();)
                    {
                        if (!matches_at(text, start, query, case_sensitive))
                        {
                            ++start;
                            continue;
                        }
                        const bool editable = shape.editable && shape.image_path.empty() &&
                            !shape.table_cell && shape.source_groups.empty() &&
                            (shape.source_part.empty() || shape.source_part == slide.source_part) &&
                            plain_span(paragraph, start, query.size());
                        results.push_back({shape.id, slide_index, shape_index, paragraph_index, start,
                            query.size(), editable, excerpt(text, start, query.size())});
                        if (results.size() >= max_results)
                            return results;
                        start += query.size();
                    }
                }
            }
        }
        return results;
    }

    PresentationEditResult replace_presentation_text_model(
        PresentationScene& scene, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!valid_text(command.find_query, false) || command.find_query.size() > 256 ||
            !valid_text(command.find_replacement, true) || command.find_replacement.size() > 4096)
            return {PresentationEditError::InvalidValue, "查找或替换文字无效。"};
        const auto matches =
            find_presentation_text(scene, command.find_query, command.find_case_sensitive, 10001);
        if (matches.size() > 10000)
            return {PresentationEditError::TooLarge, "匹配结果超过一次编辑的上限。"};
        std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::vector<PresentationTextMatch>> edits;
        for (const auto& match : matches)
        {
            if (!match.replaceable)
                continue;
            if (!command.find_all &&
                (match.slide_index != command.slide_index || match.shape_index != command.shape_index ||
                    match.shape_id != command.find_shape_id ||
                    match.paragraph_index != command.find_paragraph_index ||
                    match.start_byte != command.find_start_byte))
                continue;
            edits[{match.slide_index, match.shape_index, match.paragraph_index}].push_back(match);
        }
        if (edits.empty())
            return {PresentationEditError::InvalidValue, "没有可替换的匹配项，或搜索结果已经过期。"};
        std::size_t count = 0;
        for (auto& [location, occurrences] : edits)
        {
            const auto [slide_index, shape_index, paragraph_index] = location;
            auto& paragraph = scene.slides[slide_index].shapes[shape_index].text.paragraphs[paragraph_index];
            for (auto match = occurrences.rbegin(); match != occurrences.rend(); ++match)
            {
                replace_span(paragraph, match->start_byte, match->length_bytes, command.find_replacement);
                ++count;
            }
        }
        std::size_t text_bytes = 0;
        for (const auto& slide : scene.slides)
            for (const auto& shape : slide.shapes)
                for (const auto& paragraph : shape.text.paragraphs)
                    for (const auto& run : paragraph.runs)
                    {
                        if (run.text.size() > maximum_presentation_text_bytes - text_bytes)
                            return {PresentationEditError::TooLarge, "替换后文字超过编辑上限。"};
                        text_bytes += run.text.size();
                    }
        result.message = "已替换 " + std::to_string(count) + " 处。";
        return result;
    }

    void patch_presentation_text_matches(
        PresentationPackageState& state, const PresentationScene& before, const PresentationScene& after)
    {
        for (std::size_t slide_index = 0; slide_index < before.slides.size(); ++slide_index)
        {
            const auto& old_slide = before.slides[slide_index];
            const auto& new_slide = after.slides[slide_index];
            std::vector<std::size_t> changed;
            for (std::size_t shape_index = 0; shape_index < old_slide.shapes.size(); ++shape_index)
            {
                const auto& old_shape = old_slide.shapes[shape_index];
                const auto& new_shape = new_slide.shapes[shape_index];
                bool differs = false;
                for (std::size_t paragraph_index = 0;
                    paragraph_index < old_shape.text.paragraphs.size() && !differs; ++paragraph_index)
                    for (std::size_t run_index = 0;
                        run_index < old_shape.text.paragraphs[paragraph_index].runs.size(); ++run_index)
                        if (old_shape.text.paragraphs[paragraph_index].runs[run_index].text !=
                            new_shape.text.paragraphs[paragraph_index].runs[run_index].text)
                        {
                            differs = true;
                            break;
                        }
                if (differs)
                    changed.push_back(shape_index);
            }
            if (changed.empty())
                continue;
            const auto part = state.parts.find(old_slide.source_part);
            if (part == state.parts.end())
                throw std::runtime_error("原始页面部件不存在。");
            pugi::xml_document xml;
            if (!xml.load_buffer(
                    part->second->data(), part->second->size(), pugi::parse_default | pugi::parse_ws_pcdata))
                throw std::runtime_error("原始页面 XML 无法编辑。");
            auto tree = child(child(xml.document_element(), "cSld"), "spTree");
            for (const auto shape_index : changed)
                patch_shape_text(tree, old_slide.shapes[shape_index], new_slide.shapes[shape_index]);
            std::ostringstream output;
            xml.save(output, "", pugi::format_raw, pugi::encoding_utf8);
            auto bytes = output.str();
            if (bytes.size() > maximum_presentation_xml_bytes)
                throw std::runtime_error("替换后的页面 XML 超过上限。");
            state.parts[old_slide.source_part] = std::make_shared<const std::string>(std::move(bytes));
        }
    }
}
