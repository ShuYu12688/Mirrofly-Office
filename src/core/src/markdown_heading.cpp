#include "markdown_heading.hpp"
#include "markdown_link_parser.hpp"
#include "mirrorfly/text.hpp"

#include <algorithm>

namespace
{
    std::size_t line_start(const std::string& source, std::size_t position)
    {
        while (position > 0 && source[position - 1] != '\r' && source[position - 1] != '\n')
            --position;
        return position;
    }

    std::size_t line_end(const std::string& source, std::size_t position)
    {
        const auto end = source.find_first_of("\r\n", position);
        return end == std::string::npos ? source.size() : end;
    }

    std::string owner_prefix(const std::string& source, const mirrorfly::MarkdownParagraphInfo& paragraph)
    {
        auto prefix = source.substr(
            line_start(source, paragraph.start), paragraph.start - line_start(source, paragraph.start));
        if (paragraph.heading)
        {
            auto end = prefix.size();
            while (end > 0 && (prefix[end - 1] == ' ' || prefix[end - 1] == '\t'))
                --end;
            auto begin = end;
            while (begin > 0 && prefix[begin - 1] == '#')
                --begin;
            if (end - begin >= 1 && end - begin <= 6)
                prefix.resize(begin);
        }
        return prefix;
    }

    bool same_owners(
        const mirrorfly::MarkdownParagraphInfo& before, const mirrorfly::MarkdownParagraphInfo& after)
    {
        if (before.containers.size() != after.containers.size())
            return false;
        for (std::size_t index = 0; index < before.containers.size(); ++index)
        {
            const auto& a = before.containers[index];
            const auto& b = after.containers[index];
            if (a.kind != b.kind || a.ordered != b.ordered || a.ordinal != b.ordinal ||
                a.delimiter != b.delimiter)
                return false;
        }
        return true;
    }

    struct HeadingChange
    {
        std::size_t index;
        std::size_t start;
        std::size_t end;
        std::string prefix;
        std::string body;
    };

    bool preserves_paragraphs(const std::string& candidate,
        const std::vector<mirrorfly::MarkdownParagraphInfo>& before,
        const std::vector<HeadingChange>& changes, int level)
    {
        const auto after = mirrorfly::markdown_paragraphs(candidate);
        std::size_t changed = 0;
        std::size_t current = 0;
        for (std::size_t index = 0; index < before.size(); ++index)
        {
            const bool selected = changed < changes.size() && changes[changed].index == index;
            const bool removed =
                selected && level == 0 && before[index].heading && changes[changed].body.empty();
            if (selected)
                ++changed;
            // CommonMark has no empty paragraph node after an empty heading is reset.
            if (removed)
                continue;
            if (current >= after.size() ||
                after[current].heading_level != (selected ? level : before[index].heading_level) ||
                !same_owners(before[index], after[current]) ||
                mirrorfly::markdown_paragraph_text(before[index].runs) !=
                    mirrorfly::markdown_paragraph_text(after[current].runs))
                return false;
            ++current;
        }
        if (current != after.size())
            return false;
        return true;
    }
}

namespace mirrorfly::detail
{
    MarkdownEdit edit_markdown_heading(
        const std::string& source, std::size_t start, std::size_t end, int level)
    {
        if (level < 0 || level > 6)
            return {};
        const std::string marker = level > 0 ? std::string(static_cast<std::size_t>(level), '#') + ' ' : "";
        if (source.empty() ||
            (start == end && start == source.size() && (source.back() == '\r' || source.back() == '\n')))
        {
            MarkdownEdit edit{true, start, end, marker + (level > 0 ? u8"标题" : "")};
            if (!source.empty() && level > 0)
            {
                const auto inserted = markdown_paragraphs(source + edit.replacement);
                if (inserted.empty() || inserted.back().heading_level != level ||
                    inserted.back().start < start)
                    return {};
            }
            edit.selection_start = marker.size();
            edit.selection_end = edit.replacement.size();
            return edit;
        }
        const auto parsed = parse_markdown_links(source);
        for (const auto& block : parsed.blocks)
        {
            bool intersects = block.start < end && block.end > start;
            if (start == end)
                intersects = start >= block.start && start < block.end;
            if (intersects)
                return {};
        }
        std::vector<HeadingChange> changes;
        for (std::size_t index = 0; index < parsed.paragraphs.size(); ++index)
        {
            const auto& paragraph = parsed.paragraphs[index];
            const auto first = line_start(source, paragraph.start);
            auto last = line_end(source, paragraph.end);
            const auto prefix = owner_prefix(source, paragraph);
            const auto original_prefix = source.substr(first, paragraph.start - first);
            if (paragraph.heading && prefix == original_prefix)
            {
                // MD4C's content bounds exclude the recognized Setext underline.
                auto underline = last;
                if (underline < source.size() && source[underline] == '\r')
                    ++underline;
                if (underline < source.size() && source[underline] == '\n')
                    ++underline;
                last = line_end(source, underline);
            }
            const bool hit = start == end ? start >= first && start <= last : first < end && last >= start;
            if (!hit)
                continue;
            if (level == 0 && !paragraph.heading)
            {
                changes.push_back(
                    {index, first, last, prefix, source.substr(paragraph.start, last - paragraph.start)});
                continue;
            }
            if (level > 0 &&
                std::any_of(paragraph.runs.begin(), paragraph.runs.end(), [](const auto& run)
            {
                return run.style.hard_break;
            }))
                return {};
            const bool images = std::any_of(parsed.images.begin(), parsed.images.end(), [&](const auto& image)
            {
                return image.first < last && image.second > first;
            });
            // A multiline ATX heading is one line; opaque inline objects cannot be flattened as text.
            const bool multiline = line_start(source, paragraph.end) != first;
            if (multiline && (images || source.substr(first, last - first).find('<') != std::string::npos))
                return {};
            auto body = source.substr(paragraph.start, paragraph.end - paragraph.start);
            if (multiline)
                body = markdown_paragraph_text(paragraph.runs);
            if (level == 0)
                body = markdown_escape_paragraph_start(body);
            if (body.empty() && !paragraph.runs.empty())
                return {};
            changes.push_back({index, first, last, prefix, std::move(body)});
        }
        if (changes.empty())
            return {};
        const auto newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        MarkdownEdit edit;
        edit.start = changes.front().start;
        edit.end = changes.back().end;
        for (int separated = 0; separated < 2; ++separated)
        {
            edit.replacement.clear();
            auto position = edit.start;
            for (const auto& change : changes)
            {
                edit.replacement += source.substr(position, change.start - position);
                if (separated != 0 && change.start > 0)
                    edit.replacement += newline;
                edit.replacement += change.prefix + marker + change.body;
                if (separated != 0 && change.end < source.size())
                    edit.replacement += newline;
                position = change.end;
            }
            if (edit.replacement.size() > maximum_text_bytes - (source.size() - (edit.end - edit.start)))
                return {};
            auto candidate = source;
            candidate.replace(edit.start, edit.end - edit.start, edit.replacement);
            if (preserves_paragraphs(candidate, parsed.paragraphs, changes, level))
            {
                edit.valid = true;
                edit.selection_start = changes.front().prefix.size() + marker.size();
                edit.selection_end = edit.replacement.size();
                return edit;
            }
        }
        return {};
    }
}
