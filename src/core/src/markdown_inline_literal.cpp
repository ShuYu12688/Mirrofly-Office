#include "markdown_inline_literal.hpp"
#include "mirrorfly/markdown.hpp"
#include <utf8/checked.h>
namespace mirrorfly::detail
{
    bool whitespace(unsigned point)
    {
        return point == 0x09 || point == 0x20 || point == 0xA0 || point == 0x1680 ||
            (point >= 0x2000 && point <= 0x200A) || point == 0x202F || point == 0x205F || point == 0x3000;
    }

    std::string literal(const std::string& text)
    {
        std::string result;
        std::size_t start = 0;
        for (auto position = text.find('\t'); position != std::string::npos;
            position = text.find('\t', start))
        {
            result += mirrorfly::markdown_link_label_literal(text.substr(start, position - start));
            result += '\t';
            start = position + 1;
        }
        result += mirrorfly::markdown_link_label_literal(text.substr(start));
        return result;
    }

    std::string boundary_literal(const std::string& text, bool encoded)
    {
        if (!encoded || text.empty())
            return literal(text);
        auto first_end = text.begin();
        const auto first = utf8::next(first_end, text.end());
        std::string result = "&#" + std::to_string(first) + ';';
        if (first_end == text.end())
            return result;
        auto last_begin = text.end();
        const auto last = utf8::prior(last_begin, text.begin());
        result += literal(std::string(first_end, last_begin));
        result += "&#" + std::to_string(last) + ';';
        return result;
    }

    std::string literal_whitespace(const std::string& text)
    {
        std::string result;
        for (auto position = text.begin(); position != text.end();)
        {
            const auto point = utf8::next(position, text.end());
            result += "&#" + std::to_string(point) + ';';
        }
        return result;
    }

    std::string protect_markdown_whitespace(const std::string& source)
    {
        auto first = source.begin();
        while (first != source.end())
        {
            auto next = first;
            if (!whitespace(utf8::next(next, source.end())))
                break;
            first = next;
        }
        auto last = source.end();
        while (last != first)
        {
            auto previous = last;
            if (!whitespace(utf8::prior(previous, source.begin())))
                break;
            last = previous;
        }
        // Encode boundary whitespace, keeping the selected source's inner markup intact.
        return literal_whitespace(std::string(source.begin(), first)) + std::string(first, last) +
            literal_whitespace(std::string(last, source.end()));
    }
}
