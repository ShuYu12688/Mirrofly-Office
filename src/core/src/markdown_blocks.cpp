#include "markdown_blocks.hpp"
#include "markdown_list_source.hpp"
#include "markdown_structure.hpp"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using mirrorfly::detail::line_info;
    using mirrorfly::detail::list_marker;
    using mirrorfly::detail::ListItem;
    using mirrorfly::detail::shifted_indent;
    using mirrorfly::detail::source_lists;
    using mirrorfly::detail::SourceLine;

    bool is_line_break(char value)
    {
        return value == '\n' || value == '\r';
    }

    bool space(char value)
    {
        return value == ' ' || value == '\t';
    }

}

namespace mirrorfly::detail
{
    MarkdownEdit edit_markdown_list(const std::string& source, std::size_t start, std::size_t end,
        const std::string& action, const MarkdownOptions& options)
    {
        auto document = source_lists(source);
        restore_markdown_list_ownership(document, source);
        std::vector<int> selected;
        for (std::size_t index = 0; index < document.items.size(); ++index)
        {
            const auto& item = document.items[index];
            const auto& line = document.lines[item.line];
            const bool caret = start >= line.start && start <= line.end;
            const bool intersects = line.start < end && line.end >= start;
            const bool hit = start == end ? caret : intersects;
            if (hit)
                selected.push_back(static_cast<int>(index));
        }
        if (selected.empty())
            return {};
        const auto& selected_line =
            document.lines[document.items[static_cast<std::size_t>(selected.front())].line];
        if (start < selected_line.start || start > selected_line.end)
            return {};
        if (action == "taskSet")
        {
            const auto first =
                document.lines[document.items[static_cast<std::size_t>(selected.front())].line];
            const auto last = document.lines[document.items[static_cast<std::size_t>(selected.back())].line];
            MarkdownEdit edit;
            edit.start = first.start;
            edit.end = last.end;
            edit.replacement = source.substr(edit.start, edit.end - edit.start);
            for (const int index : selected)
            {
                const auto& item = document.items[static_cast<std::size_t>(index)];
                if (item.marker.checkbox == std::string::npos)
                    return {};
                const auto position = document.lines[item.line].start + item.marker.checkbox - edit.start;
                edit.replacement[position] = options.task_checked ? 'x' : ' ';
            }
            edit.selection_end = edit.replacement.size();
            edit.valid = true;
            return edit;
        }
        const auto& first = document.items[static_cast<std::size_t>(selected.front())];
        const auto& first_line = document.lines[first.line];
        int preceding = -1;
        for (int index = selected.front() - 1; index >= 0; --index)
        {
            const auto& item = document.items[static_cast<std::size_t>(index)];
            if (item.level < first.level || document.lines[item.line].quotes != first_line.quotes)
                break;
            if (item.level == first.level && item.parent == first.parent)
            {
                preceding = index;
                break;
            }
        }
        if ((action == "listIndent" && preceding < 0) || (action == "listOutdent" && first.parent < 0))
            return {};
        if (action == "listIndent" &&
            document.items[static_cast<std::size_t>(preceding)].end != first_line.start)
            return {};
        int delta = 0;
        if (action == "listIndent")
            delta = static_cast<int>(document.items[static_cast<std::size_t>(preceding)].marker.content) -
                first_line.indent;
        else
        {
            const auto& parent = document.items[static_cast<std::size_t>(first.parent)];
            delta = document.lines[parent.line].indent - first_line.indent;
        }
        std::size_t last = first.end;
        for (const int index : selected)
        {
            const auto& item = document.items[static_cast<std::size_t>(index)];
            if (item.level < first.level || (item.level == first.level && item.parent != first.parent))
                return {};
            if (item.level == first.level)
                last = std::max(last, item.end);
        }
        for (const auto& item : document.items)
            if (document.lines[item.line].start >= first_line.start &&
                document.lines[item.line].start < last && action == "listIndent" && item.level >= 8)
                return {};
        MarkdownEdit edit;
        edit.start = first_line.start;
        edit.end = last;
        for (const auto& line : document.lines)
        {
            if (line.start < edit.start || line.start >= edit.end)
                continue;
            const auto shifted = shifted_indent(line_info(line.text, first_line.quotes), delta);
            if (shifted.empty() && !line.text.empty())
                return {};
            edit.replacement += shifted;
            edit.replacement += source.substr(line.end, line.next - line.end);
        }
        if (!preserve_moved_markdown_code(source, document, edit, first_line.quotes, delta))
            return {};
        if (action == "listIndent" && first.marker.body == first_line.text.size())
        {
            const auto blocks = markdown_blocks(edit.replacement);
            if (!blocks.empty() && blocks.front().kind == "code")
            {
                const auto newline = edit.replacement.find('\n');
                const auto fence_start = blocks.front().start;
                if (newline != std::string::npos && fence_start > newline)
                    edit.replacement.replace(newline, fence_start - newline, " ");
            }
        }
        edit.selection_end = edit.replacement.size();
        edit.valid = true;
        return edit;
    }

    MarkdownEdit prefix_markdown_lines(
        const std::string& source, std::size_t start, std::size_t end, const std::string& action)
    {
        const bool had_selection = start != end;
        mirrorfly::MarkdownEdit edit;
        edit.start = start;

        while (edit.start > 0 && !is_line_break(source[edit.start - 1]))
        {
            --edit.start;
        }

        edit.end = end;

        if (!had_selection || end == 0 || !is_line_break(source[end - 1]))
        {
            while (edit.end < source.size() && !is_line_break(source[edit.end]))
            {
                ++edit.end;
            }
        }

        const auto lists = source_lists(source);
        const bool list_action = action == "bullet" || action == "ordered" || action == "task";
        const bool converting = list_action &&
            std::any_of(lists.items.begin(), lists.items.end(), [&](const ListItem& item)
        {
            return lists.lines[item.line].start >= edit.start && lists.lines[item.line].start < edit.end;
        });
        const auto selected_end = edit.end;
        if (converting)
        {
            for (const auto& item : lists.items)
                if (lists.lines[item.line].start >= edit.start && lists.lines[item.line].start < selected_end)
                    edit.end = std::max(edit.end, item.end);
        }
        const std::string original = source.substr(edit.start, edit.end - edit.start);
        std::size_t position = 0;
        std::size_t first_prefix_length = 0;
        std::size_t first_removed_length = 0;
        int number = 1;
        std::vector<int> sibling_numbers(lists.items.size() + 1, 1);
        std::vector<int> content_deltas(lists.items.size(), 0);

        do
        {
            const auto line_end = original.find_first_of("\r\n", position);
            const auto content_end = line_end == std::string::npos ? original.size() : line_end;
            std::string line = original.substr(position, content_end - position);
            std::string prefix;
            std::size_t removed = 0;
            const auto global_position = edit.start + position;
            const auto global_line = std::lower_bound(lists.lines.begin(), lists.lines.end(), global_position,
                [](const SourceLine& candidate, std::size_t target)
            {
                return candidate.start < target;
            });
            const auto header = std::lower_bound(lists.items.begin(), lists.items.end(), global_position,
                [&](const ListItem& item, std::size_t target)
            {
                return lists.lines[item.line].start < target;
            });
            const bool list_header =
                header != lists.items.end() && lists.lines[header->line].start == global_position;
            const bool selected_header = list_header && global_position < selected_end;
            const bool retain_line = list_action && converting && !selected_header;
            if (list_action && !converting && global_line != lists.lines.end() && global_line->literal)
                return {};

            if (retain_line)
            {
                // Continuation paragraphs and literal code are contents of their existing list item.
                int delta = 0;
                if (list_header)
                {
                    delta = header->parent < 0 ? 0 : content_deltas[static_cast<std::size_t>(header->parent)];
                    content_deltas[static_cast<std::size_t>(header - lists.items.begin())] = delta;
                }
                else if (global_line != lists.lines.end() && global_line->owner >= 0)
                    delta = content_deltas[static_cast<std::size_t>(global_line->owner)];
                if (global_line != lists.lines.end() && delta != 0)
                    line = shifted_indent(*global_line, delta);
            }
            else if (action == "bullet")
            {
                prefix = "- ";
            }
            else if (action == "ordered")
            {
                const int ordinal =
                    list_header ? sibling_numbers[static_cast<std::size_t>(header->parent + 1)]++ : number++;
                prefix = std::to_string(ordinal) + ". ";
            }
            else if (action == "task")
            {
                prefix = "- [ ] ";
            }
            else
            {
                prefix = "> ";
            }

            if (list_action && !retain_line)
            {
                const auto info = line_info(line);
                const auto marker = list_marker(info);
                if (action == "task" && marker.checkbox != std::string::npos &&
                    (line[marker.checkbox] == 'x' || line[marker.checkbox] == 'X'))
                    prefix = "- [x] ";
                const int parent_delta = list_header && header->parent >= 0
                    ? content_deltas[static_cast<std::size_t>(header->parent)]
                    : 0;
                if (list_header)
                {
                    const auto index = static_cast<std::size_t>(header - lists.items.begin());
                    const int width = action == "ordered" ? static_cast<int>(prefix.size()) : 2;
                    content_deltas[index] =
                        parent_delta + width - static_cast<int>(header->marker.content - info.indent);
                }
                const auto adjusted = parent_delta != 0 ? shifted_indent(info, parent_delta) : line;
                const auto adjusted_info = line_info(adjusted);
                prefix = adjusted.substr(0, adjusted_info.marker_start) + prefix;
                removed = marker.valid ? marker.body : info.marker_start;
                line.erase(0, removed);
            }

            if (original.empty())
            {
                line = action == "quote" ? u8"引用内容" : u8"列表项";
            }

            if (position == 0)
            {
                first_prefix_length = prefix.size();
                first_removed_length = removed;
            }

            edit.replacement += prefix + line;

            if (line_end == std::string::npos)
            {
                break;
            }

            position = line_end + 1;
            edit.replacement.push_back(original[line_end]);

            if (original[line_end] == '\r' && position < original.size() && original[position] == '\n')
            {
                edit.replacement.push_back(original[position++]);
            }
        } while (position < original.size());

        if (had_selection)
        {
            edit.selection_end = edit.replacement.size();
        }
        else if (original.empty())
        {
            edit.selection_start = first_prefix_length;
            edit.selection_end = edit.replacement.size();
        }
        else
        {
            const auto original_column = start - edit.start;
            edit.selection_start = first_prefix_length +
                (original_column > first_removed_length ? original_column - first_removed_length : 0);
            edit.selection_end = edit.selection_start;
        }

        edit.valid = true;
        return edit;
    }

}
