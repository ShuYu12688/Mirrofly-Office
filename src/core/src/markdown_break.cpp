#include "markdown_break.hpp"
#include "markdown_link_parser.hpp"
#include "markdown_structure.hpp"
#include "markdown_table.hpp"
#include "mirrorfly/text.hpp"

#include <utf8/checked.h>

#include <algorithm>
#include <string_view>

namespace mirrorfly
{
    std::string markdown_escape_paragraph_start(const std::string& line)
    {
        if (line.size() > maximum_text_bytes || line.find_first_of("\r\n") != std::string::npos ||
            line.find('\0') != std::string::npos || !utf8::is_valid(line.begin(), line.end()))
            return {};
        auto output = line;
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
            return output;
        std::size_t escape = std::string::npos;
        const auto text = std::string_view(line).substr(first);
        const char marker = text.front();
        const auto space = [](char value)
        {
            return value == ' ' || value == '\t';
        };
        if (marker == '>')
            escape = first;
        else if (marker == '#')
        {
            const auto count = text.find_first_not_of('#');
            if (count == std::string_view::npos || (count <= 6 && space(text[count])))
                escape = first;
        }
        else if (marker == '-' || marker == '+' || marker == '*' || marker == '_')
        {
            const auto count = std::count(text.begin(), text.end(), marker);
            const bool thematic = (marker == '-' || marker == '*' || marker == '_') && count >= 3 &&
                std::all_of(text.begin(), text.end(), [&](char value)
            {
                return value == marker || space(value);
            });
            if (thematic || (marker != '_' && (text.size() == 1 || space(text[1]))))
                escape = first;
        }
        else if (marker == '\x60' || marker == '~')
        {
            const auto count = text.find_first_not_of(marker);
            if ((count == std::string_view::npos || count >= 3) &&
                (marker != '\x60' || count == std::string_view::npos ||
                    text.substr(count).find(marker) == std::string_view::npos))
                escape = first;
        }
        else if (marker >= '0' && marker <= '9')
        {
            const auto count = text.find_first_not_of("0123456789");
            if (count != std::string_view::npos && count <= 9 && (text[count] == '.' || text[count] == ')') &&
                (count + 1 == text.size() || space(text[count + 1])))
                escape = first + count;
        }
        if (escape != std::string::npos)
            output.insert(escape, 1, '\\');
        return output;
    }

    std::vector<MarkdownContinuation> markdown_continuations(const std::string& source)
    {
        if (source.size() > maximum_text_bytes || source.find('\0') != std::string::npos ||
            !utf8::is_valid(source.begin(), source.end()))
            return {};
        const auto document = detail::source_lists(source);
        std::vector<MarkdownContinuation> result;
        for (const auto& line : document.lines)
        {
            auto prefix = std::string(line.text.substr(0, line.prefix_end));
            auto indentation = line.indent;
            if (line.owner >= 0)
                indentation = std::max(indentation,
                    static_cast<int>(document.items[static_cast<std::size_t>(line.owner)].marker.content));
            prefix += std::string(static_cast<std::size_t>(indentation), ' ');
            const auto marker = detail::list_marker(line);
            if (marker.valid)
            {
                const auto inner = detail::line_info(line.text.substr(marker.body));
                prefix += std::string(inner.text.substr(0, inner.prefix_end));
            }
            result.push_back({line.start, line.end, std::move(prefix)});
        }
        return result;
    }
}

namespace mirrorfly::detail
{
    MarkdownEdit insert_markdown_hard_break(const std::string& source, std::size_t start, std::size_t end)
    {
        if (start != end)
            return {};
        const auto parsed = parse_markdown_links(source);
        for (const auto& link : parsed.links)
            if (start > link.start && start < link.end &&
                (link.kind == "autolink" || link.kind == "automatic" || start < link.label_start ||
                    start > link.label_end))
                return {};
        for (const auto& image : parsed.images)
            if (start > image.first && start < image.second)
                return {};
        for (const auto& table : markdown_tables(source))
            if (start >= table.start && start <= table.end)
                return {};
        const auto continuations = markdown_continuations(source);
        const auto line = std::find_if(continuations.begin(), continuations.end(), [&](const auto& candidate)
        {
            return start >= candidate.start && start <= candidate.end;
        });
        if (line == continuations.end())
            return {};
        MarkdownEdit edit;
        edit.start = start;
        edit.end = end;
        const auto ending = markdown_line_ending(source);
        // Space markers would absorb existing visible whitespace at an interior caret.
        const bool preserve_space = start > line->start && start < line->end &&
            (source[start - 1] == ' ' || source[start - 1] == '\t');
        const std::size_t marker_size = preserve_space ? 1 : 2;
        edit.replacement = (preserve_space ? "\\" : "  ") + ending;
        if (start == line->end && start < source.size())
        {
            edit.end +=
                source[start] == '\r' && start + 1 < source.size() && source[start + 1] == '\n' ? 2 : 1;
        }
        else
        {
            edit.replacement += line->prefix;
            const auto rest = source.substr(start, line->end - start);
            const auto escaped = markdown_escape_paragraph_start(rest);
            if (escaped != rest)
            {
                edit.replacement += escaped;
                edit.end = line->end;
            }
        }
        if (source.size() - (edit.end - edit.start) + edit.replacement.size() > maximum_text_bytes)
            return {};
        const auto candidate = source.substr(0, start) + edit.replacement + source.substr(edit.end);
        const auto breaks = markdown_hard_breaks(candidate);
        const auto added_end =
            start + marker_size + ending.size() + (start == line->end ? 0 : line->prefix.size());
        // The parser confirms paragraph semantics, excluding code, headings, tables and terminal breaks.
        if (std::none_of(breaks.begin(), breaks.end(), [&](const auto& range)
        {
            return range.start <= start && range.end >= added_end;
        }))
        {
            // A leading break needs a nonblank backslash marker rather than a whitespace-only line.
            edit.replacement.replace(0, marker_size, "\\");
            const auto leading_candidate =
                source.substr(0, start) + edit.replacement + source.substr(edit.end);
            const auto leading_breaks = markdown_hard_breaks(leading_candidate);
            if (std::none_of(leading_breaks.begin(), leading_breaks.end(), [&](const auto& range)
            {
                return range.start <= start && range.end >= added_end - marker_size + 1;
            }))
                return {};
            edit.selection_start = added_end - start - marker_size + 1;
            edit.selection_end = edit.selection_start;
            edit.valid = true;
            return edit;
        }
        edit.selection_start = added_end - start;
        edit.selection_end = edit.selection_start;
        edit.valid = true;
        return edit;
    }
}
