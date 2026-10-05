#include "markdown_list_source.hpp"
#include "markdown_link_parser.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace mirrorfly::detail
{
    std::string shifted_indent(const SourceLine& line, int delta)
    {
        auto text = std::string(line.text);
        if (line.marker_start == line.text.size())
            return text;
        if (delta > 0)
            text.insert(line.prefix_end, static_cast<std::size_t>(delta), ' ');
        else
        {
            auto position = line.prefix_end;
            int consumed = 0;
            while (position < line.marker_start && consumed < -delta)
                consumed += text[position++] == '\t' ? 4 - consumed % 4 : 1;
            if (consumed < -delta && !line.text.substr(line.marker_start).empty())
                return {};
            text.replace(line.prefix_end, position - line.prefix_end,
                std::string(static_cast<std::size_t>(std::max(0, consumed + delta)), ' '));
        }
        return text;
    }

    bool preserve_moved_markdown_code(const std::string& source, const SourceLists& document,
        MarkdownEdit& edit, int quote_level, int delta)
    {
        const auto original = markdown_blocks(source);
        const auto same_code = [&]()
        {
            const auto candidate =
                markdown_blocks(source.substr(0, edit.start) + edit.replacement + source.substr(edit.end));
            std::vector<std::pair<std::string, std::string>> before, after;
            for (const auto& block : original)
                if (block.kind == "code")
                    before.emplace_back(block.language, block.text);
            for (const auto& block : candidate)
                if (block.kind == "code")
                    after.emplace_back(block.language, block.text);
            return before == after;
        };
        if (same_code())
            return true;
        // Structural tab stops can change when list prefixes move. Keep decoded code literal instead.
        std::string replacement;
        std::size_t consumed = edit.start;
        for (const auto& line : document.lines)
        {
            if (line.start < consumed || line.start >= edit.end)
                continue;
            const auto found = std::find_if(original.begin(), original.end(), [&](const auto& block)
            {
                return block.kind == "code" && block.start >= line.start && block.start <= line.end &&
                    block.end <= edit.end;
            });
            if (found == original.end())
            {
                replacement += shifted_indent(line_info(line.text, quote_level), delta);
                replacement += source.substr(line.end, line.next - line.end);
                consumed = line.next;
                continue;
            }
            std::string prefix;
            for (const auto& container : found->containers)
                prefix += container.continuation;
            auto shifted = shifted_indent(line_info(prefix + "x", quote_level), delta);
            if (shifted.empty())
                return false;
            shifted.pop_back();
            auto opening = shifted;
            const auto marker = list_marker(line_info(line.text, quote_level));
            if (marker.valid && line.start + marker.body == found->start)
            {
                opening = shifted_indent(
                    line_info(std::string(line.text.substr(0, marker.body)) + "x", quote_level), delta);
                if (opening.empty())
                    return false;
                opening.pop_back();
            }
            std::size_t longest = 0;
            std::size_t run = 0;
            const char fence_character = found->language.find('`') == std::string::npos ? '`' : '~';
            for (const char character : found->text)
            {
                run = character == fence_character ? run + 1 : 0;
                longest = std::max(longest, run);
            }
            const std::string fence(std::max<std::size_t>(3, longest + 1), fence_character);
            auto ending = source.substr(line.end, line.next - line.end);
            if (ending.empty())
                ending = "\n";
            replacement += opening + fence + found->language + ending;
            for (std::size_t position = 0; position < found->text.size();)
            {
                const auto next = found->text.find('\n', position);
                const auto end = next == std::string::npos ? found->text.size() : next;
                replacement += shifted + found->text.substr(position, end - position) + ending;
                position = next == std::string::npos ? found->text.size() : next + 1;
            }
            replacement += shifted + fence + ending;
            consumed = found->end;
        }
        edit.replacement = std::move(replacement);
        return same_code();
    }

    void restore_markdown_list_ownership(SourceLists& document, const std::string& source)
    {
        const auto parsed = parse_markdown_links(source);
        std::unordered_map<std::size_t, std::size_t> endings;
        std::unordered_set<std::size_t> active;
        for (const auto& leaf : parsed.leaves)
        {
            std::unordered_set<std::size_t> current;
            for (const auto& node : leaf.containers)
                if (node.kind == "listItem")
                    current.insert(node.identity);
            auto start = leaf.start;
            while (start > 0 && source[start - 1] != '\n' && source[start - 1] != '\r')
                --start;
            for (const auto identity : active)
                if (!current.count(identity))
                    endings.emplace(identity, start);
            active = std::move(current);
        }
        for (const auto identity : active)
            endings.emplace(identity, source.size());
        std::unordered_map<std::size_t, int> heads;
        for (std::size_t index = 0; index < document.items.size(); ++index)
        {
            auto& item = document.items[index];
            const auto& line = document.lines[item.line];
            const auto leaf = std::lower_bound(parsed.leaves.begin(), parsed.leaves.end(), line.start,
                [](const auto& block, std::size_t position)
            {
                return block.start < position;
            });
            if (leaf == parsed.leaves.end())
                continue;
            if (item.marker.body == line.text.size() && index + 1 < document.items.size())
            {
                const auto& next = document.lines[document.items[index + 1].line];
                if (next.start <= leaf->start && next.quotes == line.quotes && next.indent <= line.indent)
                    continue;
            }
            std::size_t identity = 0;
            int depth = 0;
            int parent = -1;
            for (const auto& container : leaf->containers)
            {
                if (container.kind != "listItem")
                    continue;
                ++depth;
                if (!heads.count(container.identity))
                {
                    identity = container.identity;
                    break;
                }
                parent = heads.at(container.identity);
            }
            if (identity == 0 || !endings.count(identity))
                continue;
            heads.emplace(identity, static_cast<int>(index));
            item.parent = parent;
            item.level = depth;
            item.end = endings.at(identity);
        }
        std::unordered_map<int, std::size_t> siblings;
        for (std::size_t index = 0; index < document.items.size(); ++index)
        {
            const auto& item = document.items[index];
            if (siblings.count(item.parent))
            {
                auto& previous = document.items[siblings.at(item.parent)];
                previous.end = std::min(previous.end, document.lines[item.line].start);
            }
            siblings[item.parent] = index;
        }
    }
}
