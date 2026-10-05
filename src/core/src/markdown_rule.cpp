#include "markdown_rule.hpp"
#include "markdown_blocks.hpp"
#include "markdown_structure.hpp"
#include "markdown_table.hpp"
#include "mirrorfly/text.hpp"

#include <algorithm>

namespace mirrorfly::detail
{
    MarkdownEdit insert_markdown_rule(const std::string& source, std::size_t start, std::size_t end)
    {
        if (start != end)
            return {};
        const auto paragraphs = markdown_paragraphs(source);
        auto paragraph = std::find_if(paragraphs.begin(), paragraphs.end(), [&](const auto& value)
        {
            const auto line_start = value.start == 0 ? 0 : source.rfind('\n', value.start - 1) + 1;
            return start >= line_start && start <= value.end;
        });
        if (paragraph == paragraphs.end())
        {
            if (source.find_first_not_of(" \t\r\n") == std::string::npos)
            {
                const auto ending = markdown_line_ending(source);
                auto replacement = (source.empty() || source.back() == '\n' ? std::string{} : ending) +
                    "- - -" + ending + ending;
                if (source.size() + replacement.size() <= maximum_text_bytes)
                    return {true, source.size(), source.size(), replacement, replacement.size(),
                        replacement.size()};
            }
            return {};
        }
        const int quotes = paragraph->quote_level;
        auto insertion = paragraph->end;
        if (paragraph->list_depth > 0)
        {
            const auto group = paragraph->list_group;
            while (paragraph + 1 != paragraphs.end() && (paragraph + 1)->list_group == group)
                ++paragraph;
            insertion = paragraph->end;
            const auto structure = source_lists(source);
            for (const auto& line : structure.lines)
                if (paragraph->start >= line.start && paragraph->start <= line.end && line.owner >= 0)
                {
                    auto owner = line.owner;
                    while (structure.items[static_cast<std::size_t>(owner)].parent >= 0)
                        owner = structure.items[static_cast<std::size_t>(owner)].parent;
                    insertion = structure.items[static_cast<std::size_t>(owner)].end;
                    break;
                }
        }
        else
        {
            const auto newline = source.find('\n', insertion);
            insertion = newline == std::string::npos ? source.size() : newline + 1;
            const auto first_start = paragraph->start == 0 ? 0 : source.rfind('\n', paragraph->start - 1) + 1;
            const auto first_line =
                line_info(std::string_view(source).substr(first_start, paragraph->start - first_start));
            const bool atx = first_line.marker_start < first_line.text.size() &&
                first_line.text[first_line.marker_start] == '#';
            if (paragraph->heading && !atx && insertion < source.size())
            {
                const auto next_end = source.find('\n', insertion);
                auto underline = source.substr(insertion, next_end - insertion);
                if (!underline.empty() && underline.back() == '\r')
                    underline.pop_back();
                const auto content = line_info(underline).text.substr(line_info(underline).marker_start);
                if (!content.empty() && (content.front() == '=' || content.front() == '-') &&
                    std::all_of(content.begin(), content.end(), [&](char value)
                {
                    return value == content.front() || value == ' ' || value == '\t';
                }))
                    insertion = next_end == std::string::npos ? source.size() : next_end + 1;
            }
        }
        const auto ending = markdown_line_ending(source);
        std::string prefix;
        for (int index = 0; index < quotes; ++index)
            prefix += "> ";
        MarkdownEdit edit;
        edit.start = insertion;
        edit.end = insertion;
        if (insertion > 0 && source[insertion - 1] != '\n')
            edit.replacement += ending;
        edit.replacement += prefix + ending + prefix + "- - -" + ending + prefix + ending;
        edit.selection_start = edit.replacement.size() - ending.size();
        if (insertion == source.size())
        {
            edit.replacement += prefix;
            edit.selection_start = edit.replacement.size();
        }
        edit.selection_end = edit.selection_start;
        if (source.size() + edit.replacement.size() > maximum_text_bytes)
            return {};
        const auto result = source.substr(0, insertion) + edit.replacement + source.substr(insertion);
        if (markdown_thematic_break_count(result) != markdown_thematic_break_count(source) + 1)
            return {};
        edit.valid = true;
        return edit;
    }
}
