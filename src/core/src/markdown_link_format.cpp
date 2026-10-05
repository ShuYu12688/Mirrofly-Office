#include "markdown_link_format.hpp"
#include "mirrorfly/markdown.hpp"
#include "mirrorfly/text.hpp"

#include <utf8/checked.h>

#include <algorithm>

namespace
{
    bool punctuation(char value)
    {
        return (value >= '!' && value <= '/') || (value >= ':' && value <= '@') ||
            (value >= '[' && value <= '\x60') || (value >= '{' && value <= '~');
    }

    bool valid_parameter(const std::string& text, std::size_t maximum)
    {
        return text.size() <= maximum && utf8::is_valid(text.begin(), text.end()) &&
            std::none_of(text.begin(), text.end(), [](unsigned char value)
        {
            return value < 0x20 || value == 0x7F;
        });
    }
}

namespace mirrorfly::detail
{
    std::string escape_markdown_link(const std::string& text, bool label)
    {
        std::string result;
        for (const char value : text)
        {
            if (value == '&')
                result += "\\&";
            else
            {
                if (value == '\\' || value == '[' || value == ']' || value == '|' ||
                    (!label &&
                        (value == '(' || value == ')' || value == '<' || value == '>' || value == '"')))
                    result += '\\';
                result += value;
            }
        }
        return result;
    }

}

namespace mirrorfly
{
    std::string markdown_link_label_literal(const std::string& text)
    {
        if (!valid_parameter(text, maximum_text_bytes))
            return {};
        std::string result;
        for (const char value : text)
        {
            if (value == '&')
                result += "\\&";
            else
            {
                if (punctuation(value))
                    result += '\\';
                result += value;
            }
        }
        return result;
    }

    std::string markdown_link_suffix(const std::string& url, const std::string& title)
    {
        if (url.empty() || !valid_parameter(url, 4096) || !valid_parameter(title, 1024))
            return {};
        const bool angle = url.find(' ') != std::string::npos;
        auto target = detail::escape_markdown_link(url, false);
        if (angle)
            target = "<" + target + ">";
        return "(" + target +
            (title.empty() ? "" : " \"" + detail::escape_markdown_link(title, false) + "\"") + ")";
    }

}
