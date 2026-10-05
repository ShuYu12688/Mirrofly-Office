#include "markdown_code_source.hpp"
#include "markdown_table.hpp"
#include "mirrorfly/text.hpp"
#include <algorithm>
#include <utf8/checked.h>
namespace
{
    bool is_line_break(char character)
    {
        return character == '\n' || character == '\r';
    }

    std::string safe_language(const std::string& language)
    {
        std::string result;

        for (char character : language)
        {
            const bool letter =
                (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
            const bool digit = character >= '0' && character <= '9';

            if (result.size() == 32 ||
                (!letter && !digit && character != '_' && character != '-' && character != '+' &&
                    character != '#' && character != '.'))
            {
                break;
            }

            result.push_back(character);
        }

        return result;
    }

    std::size_t longest_run(const std::string& text, char marker)
    {
        std::size_t longest = 0;
        std::size_t current = 0;

        for (char character : text)
        {
            current = character == marker ? current + 1 : 0;
            longest = std::max(longest, current);
        }

        return longest;
    }

}
namespace mirrorfly::detail
{
    MarkdownEdit fenced_markdown_code(
        const std::string& source, std::size_t start, std::size_t end, const std::string& language)
    {
        mirrorfly::MarkdownEdit edit;
        edit.start = start;
        edit.end = end;
        const auto selected = start == end ? std::string(u8"代码内容") : source.substr(start, end - start);
        const auto line_ending = mirrorfly::detail::markdown_line_ending(source);
        const std::string fence(std::max<std::size_t>(3, longest_run(selected, '`') + 1), '`');
        const auto leading = start > 0 && !is_line_break(source[start - 1]) ? line_ending : std::string{};
        edit.replacement = leading + fence + safe_language(language) + line_ending;
        edit.selection_start = edit.replacement.size();
        edit.replacement += selected;
        edit.selection_end = edit.replacement.size();

        if (selected.empty() || !is_line_break(selected.back()))
        {
            edit.replacement += line_ending;
        }

        edit.replacement += fence;

        if (end == source.size() || !is_line_break(source[end]))
        {
            edit.replacement += line_ending;
        }

        edit.valid = true;
        return edit;
    }
}
namespace mirrorfly
{
    std::string markdown_code_span(const std::string& text)
    {
        if (text.empty() || text.size() > maximum_text_bytes || text.find('\0') != std::string::npos ||
            !utf8::is_valid(text.begin(), text.end()))
            return {};
        std::string normalized;
        normalized.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] == '\r')
            {
                normalized.push_back(' ');
                if (index + 1 < text.size() && text[index + 1] == '\n')
                    ++index;
            }
            else
                normalized.push_back(text[index] == '\n' ? ' ' : text[index]);
        }
        const std::string delimiter(longest_run(normalized, '`') + 1, '`');
        const bool spaces_only = std::all_of(normalized.begin(), normalized.end(), [](char character)
        {
            return character == ' ';
        });
        const bool padded = normalized.front() == '`' || normalized.back() == '`' ||
            (!spaces_only && normalized.front() == ' ' && normalized.back() == ' ');
        const std::string padding = padded ? " " : "";
        return delimiter + padding + normalized + padding + delimiter;
    }

}

namespace mirrorfly::detail
{
    MarkdownEdit edit_markdown_code_span(
        const std::string& source, std::size_t start, std::size_t end, const std::string& action)
    {
        MarkdownEdit edit;
        edit.start = start;
        edit.end = end;
        if (action == "removeInlineCode")
        {
            const auto spans = markdown_code_spans(source);
            const auto found = std::find_if(spans.begin(), spans.end(), [&](const auto& span)
            {
                return start >= span.start && start < span.end && end <= span.end;
            });
            if (found == spans.end())
                return {};
            edit.start = found->start;
            edit.end = found->end;
            edit.replacement = markdown_inline_label({{found->text}});
            if (!found->text.empty() && edit.replacement.empty())
                return {};
            edit.selection_end = edit.replacement.size();
        }
        else
        {
            const auto selected = start == end ? std::string(u8"代码") : source.substr(start, end - start);
            const std::string delimiter(longest_run(selected, '`') + 1, '`');
            const bool spaces_only = std::all_of(selected.begin(), selected.end(), [](char value)
            {
                return value == ' ';
            });
            const bool padded = selected.front() == '`' || selected.back() == '`' ||
                (!spaces_only && selected.front() == ' ' && selected.back() == ' ');
            const std::string padding = padded ? " " : "";
            edit.replacement = delimiter + padding + selected + padding + delimiter;
            edit.selection_start = delimiter.size() + padding.size();
            edit.selection_end = edit.selection_start + selected.size();
            const auto candidate = source.substr(0, start) + edit.replacement + source.substr(end);
            const auto spans = markdown_code_spans(candidate);
            if (std::none_of(spans.begin(), spans.end(), [&](const auto& span)
            {
                return span.start == start && span.end == start + edit.replacement.size();
            }))
                return {};
        }
        edit.valid = true;
        return edit;
    }
}
