#include "markdown_decode.hpp"
extern "C"
{
#include <entity.h>
}
#include <iterator>
#include <utf8/checked.h>

namespace mirrorfly::detail
{
    void append_codepoint(std::string& output, unsigned value)
    {
        if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
            value = 0xFFFD;
        utf8::append(value, std::back_inserter(output));
    }

    void append_entity(std::string& output, std::string_view text)
    {
        if (text.size() > 3 && text[0] == '&' && text[1] == '#' && text.back() == ';')
        {
            const bool hexadecimal = text[2] == 'x' || text[2] == 'X';
            unsigned value = 0;
            for (std::size_t index = hexadecimal ? 3 : 2; index + 1 < text.size(); ++index)
            {
                const char character = text[index];
                const unsigned digit = character >= '0' && character <= '9' ? character - '0'
                    : character >= 'a' && character <= 'f'                  ? character - 'a' + 10
                    : character >= 'A' && character <= 'F'                  ? character - 'A' + 10
                                                                            : 16;
                const unsigned base = hexadecimal ? 16 : 10;
                if (digit >= base || value > 0x10FFFF / base)
                {
                    value = 0xFFFD;
                    break;
                }
                value = value * base + digit;
            }
            append_codepoint(output, value);
        }
        else if (const auto* entity = entity_lookup(text.data(), text.size()))
        {
            append_codepoint(output, entity->codepoints[0]);
            if (entity->codepoints[1] != 0)
                append_codepoint(output, entity->codepoints[1]);
        }
        else
            output.append(text);
    }

    void append_markdown_text(std::string& output, MD_TEXTTYPE type, std::string_view text)
    {
        if (type == MD_TEXT_ENTITY)
            append_entity(output, text);
        else if (type == MD_TEXT_NULLCHAR)
            append_codepoint(output, 0xFFFD);
        else if (type == MD_TEXT_SOFTBR)
            output += ' ';
        else if (type == MD_TEXT_BR)
            output += '\n';
        else
            output.append(text);
    }

    std::string markdown_attribute(const MD_ATTRIBUTE& value)
    {
        std::string output;
        if (value.size == 0)
            return output;
        for (std::size_t index = 0; value.substr_offsets[index] < value.size; ++index)
        {
            const auto begin = value.substr_offsets[index];
            const auto end = value.substr_offsets[index + 1];
            append_markdown_text(output, value.substr_types[index], {value.text + begin, end - begin});
        }
        return output;
    }

}
