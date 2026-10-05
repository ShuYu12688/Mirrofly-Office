#include "markdown_emphasis_source.hpp"
#include "markdown_inline_literal.hpp"
#include <utf8/checked.h>
namespace
{
    bool word_boundary(unsigned point)
    {
        return (point >= 'a' && point <= 'z') || (point >= 'A' && point <= 'Z') ||
            (point >= '0' && point <= '9') || (point >= 0x80 && !mirrorfly::detail::whitespace(point));
    }
    std::string reference(unsigned point)
    {
        return "&#" + std::to_string(point) + ';';
    }
}
namespace mirrorfly::detail
{
    MarkdownEdit wrap_markdown_emphasis(const std::string& source, std::size_t start, std::size_t end,
        const std::string& before, const std::string& after, const std::string& placeholder)
    {
        MarkdownEdit edit;
        edit.start = start;
        edit.end = end;
        const std::string original = start == end ? placeholder : source.substr(start, end - start);
        const auto selected = protect_markdown_whitespace(original);
        auto first = original.begin();
        auto last = original.end();
        const bool leading = whitespace(utf8::next(first, original.end()));
        const bool trailing = whitespace(utf8::prior(last, original.begin()));
        std::string prefix;
        std::string suffix;
        // Character references are punctuation in delimiter flanking. Encode an adjacent word
        // character too, so whitespace emphasis works inside words without inserting visible text.
        if (leading && start > 0)
        {
            auto previous = source.begin() + static_cast<std::ptrdiff_t>(start);
            const auto point = utf8::prior(previous, source.begin());
            if (word_boundary(point))
            {
                edit.start = static_cast<std::size_t>(previous - source.begin());
                prefix = reference(point);
                auto slashes = edit.start;
                while (slashes > 0 && source[slashes - 1] == '\\')
                    --slashes;
                if ((edit.start - slashes) % 2 != 0)
                {
                    --edit.start;
                    prefix = reference('\\') + prefix;
                }
            }
        }
        if (trailing && end < source.size())
        {
            auto next = source.begin() + static_cast<std::ptrdiff_t>(end);
            const auto point = utf8::next(next, source.end());
            if (word_boundary(point))
            {
                edit.end = static_cast<std::size_t>(next - source.begin());
                suffix = reference(point);
            }
        }
        edit.replacement = prefix + before + selected + after + suffix;
        edit.selection_start = prefix.size() + before.size();
        edit.selection_end = edit.selection_start + selected.size();
        edit.valid = true;
        return edit;
    }
}
