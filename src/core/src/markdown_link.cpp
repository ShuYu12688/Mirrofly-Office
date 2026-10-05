#include "markdown_link.hpp"
#include "markdown_link_format.hpp"
#include "markdown_link_parser.hpp"
#include "markdown_structure.hpp"

#include <algorithm>
#include <string_view>

namespace
{
    std::size_t code_end(std::string_view text, std::size_t start)
    {
        auto end = start;
        while (end < text.size() && text[end] == '\x60')
            ++end;
        const auto length = end - start;
        for (auto next = end; next < text.size();)
        {
            if (text[next] != '\x60')
            {
                ++next;
                continue;
            }
            auto closing = next;
            while (closing < text.size() && text[closing] == '\x60')
                ++closing;
            if (closing - next == length)
                return closing;
            next = closing;
        }
        return end;
    }

}

namespace mirrorfly::detail
{
    MarkdownEdit edit_markdown_link(const std::string& source, std::size_t start, std::size_t end,
        const std::string& action, const MarkdownOptions& options)
    {
        const auto suffix =
            action == "link" ? markdown_link_suffix(options.url, options.title) : std::string{};
        if (action == "link" && suffix.empty())
            return {};
        const auto parsed = parse_markdown_links(source);
        const auto& links = parsed.links;
        for (const auto& link : links)
        {
            if (start < link.start || start >= link.end || end > link.end)
                continue;
            MarkdownEdit edit;
            edit.start = link.start;
            edit.end = link.end;
            const auto label = link.kind == "autolink" || link.kind == "automatic"
                ? markdown_link_label_literal(link.text)
                : source.substr(link.label_start, link.label_end - link.label_start);
            edit.replacement = action == "unlink" ? label : "[" + label + "]" + suffix;
            edit.selection_start = action == "unlink" ? 0 : 1;
            edit.selection_end = edit.selection_start + label.size();
            edit.valid = true;
            return edit;
        }
        for (const auto& line : source_lists(source).lines)
            if (start >= line.start && start <= line.end)
            {
                if (line.literal || end > line.end)
                    return {};
                for (std::size_t position = 0; position < line.text.size();)
                {
                    if (line.text[position] == '\\' && position + 1 < line.text.size())
                        position += 2;
                    else if (line.text[position] == '\x60')
                    {
                        const auto next = code_end(line.text, position);
                        auto opening = position;
                        while (opening < line.text.size() && line.text[opening] == '\x60')
                            ++opening;
                        if (next > opening && start >= line.start + position && start < line.start + next &&
                            !(start == line.start + position && end >= line.start + next))
                            return {};
                        position = next;
                    }
                    else
                        ++position;
                }
            }
        if (action == "unlink" ||
            std::any_of(links.begin(), links.end(), [&](const MarkdownLinkInfo& link)
        {
            return start < link.end && end > link.start;
        }))
            return {};
        const auto label = start == end ? std::string(u8"链接文字") : source.substr(start, end - start);
        if (label.find_first_of("\r\n") != std::string::npos)
            return {};
        MarkdownEdit edit;
        edit.start = start;
        edit.end = end;
        std::string text;
        auto image = std::lower_bound(parsed.images.begin(), parsed.images.end(), start,
            [](const auto& range, std::size_t offset)
        {
            return range.first < offset;
        });
        for (std::size_t position = 0; position < label.size();)
        {
            while (image != parsed.images.end() && image->first < start + position)
                ++image;
            if (image != parsed.images.end() && image->first == start + position && image->second <= end)
            {
                const auto length = image->second - image->first;
                text += label.substr(position, length);
                position += length;
                continue;
            }
            const auto next = label[position] == '\x60' ? code_end(label, position) : position + 1;
            if (next > position + 1 && label[next - 1] == '\x60')
                text += label.substr(position, next - position);
            else
                text += escape_markdown_link(label.substr(position, next - position), true);
            position = next;
        }
        edit.replacement = "[" + text + "]" + suffix;
        edit.selection_start = 1;
        edit.selection_end = text.size() + 1;
        edit.valid = true;
        return edit;
    }
}
