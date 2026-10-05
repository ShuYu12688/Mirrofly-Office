#include "markdown_blocks.hpp"
#include "markdown_list_source.hpp"
#include "markdown_structure.hpp"

#include <algorithm>

namespace
{
    using mirrorfly::detail::line_info;
    using mirrorfly::detail::ListItem;
    using mirrorfly::detail::source_lists;
    using mirrorfly::detail::SourceLine;

    bool is_line_break(char value)
    {
        return value == '\n' || value == '\r';
    }
}

namespace mirrorfly::detail
{
    MarkdownEdit edit_markdown_quote(const std::string& source, std::size_t start, std::size_t end, int level)
    {
        if (level < 0 || level > 8)
            return {};
        const auto prefix = [](int depth)
        {
            std::string value;
            for (int index = 0; index < depth; ++index)
                value += "> ";
            return value;
        };
        if (source.empty() || (start == end && start == source.size() && is_line_break(source.back())))
        {
            MarkdownEdit edit;
            edit.start = start;
            edit.end = end;
            edit.replacement = prefix(level) + (level > 0 ? u8"引用内容" : "");
            edit.selection_start = prefix(level).size();
            edit.selection_end = edit.replacement.size();
            edit.valid = true;
            return edit;
        }
        auto document = source_lists(source);
        restore_markdown_list_ownership(document, source);
        auto first = std::find_if(document.lines.begin(), document.lines.end(), [&](const SourceLine& line)
        {
            return start >= line.start && start <= line.end;
        });
        if (first == document.lines.end())
            return {};
        MarkdownEdit edit;
        edit.start = first->start;
        edit.end = end;
        while (edit.end < source.size() && !is_line_break(source[edit.end]))
            ++edit.end;
        if (end > start && end > 0 && is_line_break(source[end - 1]))
            edit.end = end;
        for (const auto& fence : document.fences)
            if (start >= fence.first && start <= fence.second)
            {
                edit.start = std::min(edit.start, fence.first);
                edit.end = std::max(edit.end, fence.second);
            }
        const auto tables = markdown_tables(source);
        const MarkdownTableInfo* active_table = nullptr;
        for (const auto& table : tables)
            if (start >= table.start && start <= table.end)
            {
                active_table = &table;
                edit.start = std::min(edit.start, table.start);
                edit.end = std::max(edit.end, table.end);
                const bool owns_header =
                    std::any_of(document.items.begin(), document.items.end(), [&](const ListItem& item)
                {
                    return document.lines[item.line].start == table.start;
                });
                const auto empty_owner =
                    std::find_if(document.items.begin(), document.items.end(), [&](const ListItem& item)
                {
                    const auto& line = document.lines[item.line];
                    return table.list_indent > 0 && line.start == table.list_owner_start &&
                        item.marker.body == line.text.size();
                });
                if (empty_owner != document.items.end())
                    edit.start = std::min(edit.start, table.list_owner_start);
                if (table.list_indent > 0 && !owns_header && empty_owner == document.items.end() &&
                    level < table.list_quote_level)
                    return {};
            }
        const auto original_end = edit.end;
        bool selected_owner = false;
        const ListItem* first_owner = nullptr;
        for (const auto& item : document.items)
            if (document.lines[item.line].start >= edit.start &&
                (document.lines[item.line].start < original_end ||
                    document.lines[item.line].start == edit.start))
            {
                // A list item's nested blocks belong to the same container style transaction.
                edit.end = std::max(edit.end, item.end);
                if (document.lines[item.line].start == edit.start)
                {
                    selected_owner = true;
                    first_owner = &item;
                }
            }
        first = std::lower_bound(document.lines.begin(), document.lines.end(), edit.start,
            [](const SourceLine& line, std::size_t position)
        {
            return line.start < position;
        });
        const int previous = active_table ? active_table->quote_level : first->quotes;
        const int delta = level - previous;
        const int owner_delta = selected_owner ? level - first->quotes : delta;
        if (first_owner)
        {
            // Quotes inside an item retain their position after the item's required indentation.
            for (auto next = first + 1; next != document.lines.end(); ++next)
            {
                if (next->start < edit.end)
                    continue;
                const auto outer = line_info(next->text, first->quotes);
                const bool blank = outer.marker_start == outer.text.size();
                if (!blank &&
                    (outer.quotes != first->quotes ||
                        outer.indent < static_cast<int>(first_owner->marker.content)))
                    break;
                edit.end = next->end;
            }
        }
        if (previous > 0 && !active_table)
            for (auto next = first + 1; next != document.lines.end(); ++next)
            {
                if (next->start < edit.end)
                    continue;
                if (next->quotes <= previous)
                    break;
                edit.end = next->end;
            }
        std::size_t table_index = 0;
        for (const auto& line : document.lines)
        {
            if (line.start < edit.start || line.start > edit.end ||
                (line.start == edit.end && line.start != edit.start))
                continue;
            int changed = line.quotes + owner_delta;
            const MarkdownTableInfo* container = nullptr;
            while (table_index < tables.size() && tables[table_index].end < line.start)
                ++table_index;
            if (table_index < tables.size() && line.start >= tables[table_index].start)
            {
                container = &tables[table_index];
                changed = container->quote_level + (container == active_table ? delta : owner_delta);
            }
            if (changed < 0 || changed > 8)
                return {};
            if (container)
            {
                const int inherited = container->list_indent > 0
                    ? container->list_quote_level + (selected_owner ? owner_delta : 0)
                    : 0;
                if (changed < inherited || inherited < 0)
                    return {};
                const auto pipe_offset = line.text.find('|');
                const auto pipe = pipe_offset == std::string_view::npos ? line.marker_start : pipe_offset;
                const auto header_item = std::lower_bound(document.items.begin(), document.items.end(),
                    line.start, [&](const ListItem& item, std::size_t position)
                {
                    return document.lines[item.line].start < position;
                });
                if (line.start == container->start && header_item != document.items.end() &&
                    document.lines[header_item->line].start == line.start)
                    edit.replacement += prefix(changed) +
                        std::string(line.text.substr(line.prefix_end, pipe - line.prefix_end));
                else
                    edit.replacement += prefix(inherited) +
                        std::string(static_cast<std::size_t>(container->list_indent), ' ') +
                        prefix(changed - inherited);
                edit.replacement += line.text.substr(pipe);
            }
            else if (selected_owner)
            {
                const auto outer = line_info(line.text, first->quotes);
                edit.replacement += prefix(level) + std::string(line.text.substr(outer.prefix_end));
            }
            else
                edit.replacement += prefix(changed) + std::string(line.text.substr(line.prefix_end));
            const auto ending = std::min(line.next, edit.end);
            if (ending > line.end)
                edit.replacement += source.substr(line.end, ending - line.end);
        }
        edit.valid = true;
        edit.selection_end = edit.replacement.size();
        return edit;
    }

}
