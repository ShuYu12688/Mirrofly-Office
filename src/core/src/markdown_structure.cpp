#include "markdown_structure.hpp"

#include <algorithm>

namespace
{
    bool space(char value)
    {
        return value == ' ' || value == '\t';
    }
}

namespace mirrorfly::detail
{
    SourceLine line_info(std::string_view text, int maximum_quotes)
    {
        SourceLine line;
        line.text = text;
        while (line.quotes < maximum_quotes)
        {
            auto probe = line.prefix_end;
            while (probe < text.size() && space(text[probe]))
                ++probe;
            if (probe == text.size() || text[probe] != '>')
                break;
            line.prefix_end = probe + 1;
            if (line.prefix_end < text.size() && text[line.prefix_end] == ' ')
                ++line.prefix_end;
            ++line.quotes;
        }
        line.marker_start = line.prefix_end;
        while (line.marker_start < text.size() && space(text[line.marker_start]))
        {
            line.indent += text[line.marker_start++] == '\t' ? 4 - line.indent % 4 : 1;
        }
        return line;
    }

    Marker list_marker(const SourceLine& line)
    {
        auto position = line.marker_start;
        if (position == line.text.size())
            return {};
        const char first = line.text[position];
        if (first == '-' || first == '+' || first == '*')
        {
            int count = 0;
            bool thematic = true;
            for (const char value : line.text.substr(position))
            {
                if (value == first)
                    ++count;
                else if (!space(value))
                    thematic = false;
            }
            if (thematic && count >= 3)
                return {};
        }
        if (first == '-' || first == '+' || first == '*')
            ++position;
        else
        {
            int digits = 0;
            while (position < line.text.size() && line.text[position] >= '0' && line.text[position] <= '9' &&
                digits < 9)
            {
                ++digits;
                ++position;
            }
            if (digits == 0 || position == line.text.size() ||
                (line.text[position] != '.' && line.text[position] != ')'))
                return {};
            ++position;
        }
        if (position < line.text.size() && !space(line.text[position]))
            return {};
        const auto marker_end = position;
        while (position < line.text.size() && space(line.text[position]))
            ++position;
        Marker marker;
        marker.valid = true;
        marker.body = position;
        int content_column = line.indent + static_cast<int>(marker_end - line.marker_start);
        const int marker_column = content_column;
        for (auto offset = marker_end; offset < position; ++offset)
            content_column += line.text[offset] == '\t' ? 4 - content_column % 4 : 1;
        const int padding = content_column - marker_column;
        marker.content = marker_column + (padding < 1 || padding > 4 ? 1 : padding);
        if (padding > 4)
        {
            marker.body = marker_end + 1;
            return marker;
        }
        if (position + 2 < line.text.size() && line.text[position] == '[' && line.text[position + 2] == ']' &&
            (space(line.text[position + 1]) || line.text[position + 1] == 'x' ||
                line.text[position + 1] == 'X') &&
            (position + 3 == line.text.size() || space(line.text[position + 3])))
        {
            marker.checkbox = position + 1;
            position += 3;
            while (position < line.text.size() && space(line.text[position]))
                ++position;
            marker.body = position;
        }
        return marker;
    }

    SourceLists source_lists(const std::string& source)
    {
        SourceLists document;
        std::vector<int> stack;
        char fence = 0;
        std::size_t fence_length = 0;
        int fence_quotes = 0;
        int fence_base = 0;
        int quotes = 0;
        std::size_t fence_start = 0;
        bool blank = false;
        const auto close_items = [&](std::size_t position)
        {
            while (!stack.empty())
            {
                document.items[static_cast<std::size_t>(stack.back())].end = position;
                stack.pop_back();
            }
        };
        for (std::size_t start = 0; start < source.size();)
        {
            const auto ending = source.find_first_of("\r\n", start);
            const auto end = ending == std::string::npos ? source.size() : ending;
            auto line = line_info(
                std::string_view(source).substr(start, end - start), fence != 0 ? fence_quotes : 100000);
            line.start = start;
            line.end = end;
            line.next = end;
            if (line.next < source.size() && source[line.next++] == '\r' && line.next < source.size() &&
                source[line.next] == '\n')
                ++line.next;
            if (line.quotes != quotes)
            {
                close_items(start);
                if (fence != 0)
                    document.fences.emplace_back(fence_start, start);
                fence = 0;
                quotes = line.quotes;
            }
            auto content = line.text.substr(line.marker_start);
            if (fence != 0 && !stack.empty() && line.indent < fence_base && !content.empty())
            {
                // A fenced block cannot continue outside its list item's required indentation.
                close_items(start);
                document.fences.emplace_back(fence_start, start);
                fence = 0;
            }
            if (fence != 0)
            {
                line.literal = true;
                std::size_t count = 0;
                while (count < content.size() && content[count] == fence)
                    ++count;
                if (line.quotes == fence_quotes && line.indent < fence_base + 4 && count >= fence_length &&
                    std::all_of(content.begin() + count, content.end(), space))
                {
                    fence = 0;
                    document.fences.emplace_back(fence_start, end);
                }
            }
            else if (content.empty())
                blank = true;
            else
            {
                auto marker = list_marker(line);
                if (marker.valid)
                {
                    while (!stack.empty() &&
                        line.indent <
                            static_cast<int>(
                                document.items[static_cast<std::size_t>(stack.back())].marker.content))
                    {
                        document.items[static_cast<std::size_t>(stack.back())].end = start;
                        stack.pop_back();
                    }
                }
                const int parent = stack.empty() ? -1 : stack.back();
                const auto base =
                    parent < 0 ? 0 : document.items[static_cast<std::size_t>(parent)].marker.content;
                line.literal = line.indent >= static_cast<int>(base) + 4;
                if (marker.valid && !line.literal)
                {
                    const int level =
                        parent < 0 ? 1 : document.items[static_cast<std::size_t>(parent)].level + 1;
                    const int index = static_cast<int>(document.items.size());
                    document.items.push_back({document.lines.size(), source.size(), parent, level, marker});
                    stack.push_back(index);
                    content = line.text.substr(marker.body);
                }
                else if (!line.literal && !stack.empty() && line.indent < static_cast<int>(base) &&
                    (blank || content.front() == '#' || content.front() == '<'))
                    close_items(start);
                if (!line.literal && !content.empty() &&
                    (content.front() == '\x60' || content.front() == '~'))
                {
                    const char value = content.front();
                    std::size_t count = 0;
                    while (count < content.size() && content[count] == value)
                        ++count;
                    if (count >= 3 &&
                        (value != '\x60' || content.substr(count).find('\x60') == std::string_view::npos))
                    {
                        fence = value;
                        fence_start = start;
                        fence_length = count;
                        fence_quotes = line.quotes;
                        fence_base = 0;
                        if (!stack.empty())
                            fence_base = static_cast<int>(
                                document.items[static_cast<std::size_t>(stack.back())].marker.content);
                        line.literal = true;
                    }
                }
                blank = false;
            }
            line.owner = stack.empty() ? -1 : stack.back();
            document.lines.push_back(line);
            start = line.next;
        }
        if (fence != 0)
            document.fences.emplace_back(fence_start, source.size());
        return document;
    }
}
