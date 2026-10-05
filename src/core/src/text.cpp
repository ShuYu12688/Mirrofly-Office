#include "mirrorfly/text.hpp"

#include <utf8/checked.h>

#include <array>
#include <cstdint>
#include <iterator>
#include <utility>

namespace
{

    std::string text_extension(std::string path)
    {
        if (path.empty() || path.find('\0') != std::string::npos || !utf8::is_valid(path.begin(), path.end()))
        {
            return {};
        }
        const auto slash = path.find_last_of("/\\");
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        {
            return {};
        }
        path.erase(0, dot);
        for (char& character : path)
        {
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return path;
    }

    bool starts_with(const std::string& bytes, const char* prefix, std::size_t length)
    {
        return bytes.size() >= length && bytes.compare(0, length, prefix, length) == 0;
    }

    std::string normalize_lines(const std::string& text, mirrorfly::TextFormat& format)
    {
        std::array<std::size_t, 3> counts{};
        std::array<std::size_t, 3> first_positions{text.size(), text.size(), text.size()};
        std::string normalized;
        normalized.reserve(text.size());

        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] != '\r' && text[index] != '\n')
            {
                normalized.push_back(text[index]);
                continue;
            }

            std::size_t kind = 0;

            if (text[index] == '\r')
            {
                kind = index + 1 < text.size() && text[index + 1] == '\n' ? 1 : 2;
            }

            if (counts[kind]++ == 0)
            {
                first_positions[kind] = index;
            }

            normalized.push_back('\n');

            if (kind == 1)
            {
                ++index;
            }
        }

        std::size_t dominant = 0;
        std::size_t distinct = 0;

        for (std::size_t kind = 0; kind < counts.size(); ++kind)
        {
            distinct += counts[kind] != 0 ? 1 : 0;

            if (counts[kind] > counts[dominant] ||
                (counts[kind] == counts[dominant] && first_positions[kind] < first_positions[dominant]))
            {
                dominant = kind;
            }
        }

        format.line_ending = static_cast<mirrorfly::LineEnding>(dominant);
        format.mixed_line_endings = distinct > 1;
        return normalized;
    }

    mirrorfly::TextError validate_utf8(const std::string& text)
    {
        if (text.size() > mirrorfly::maximum_text_bytes)
        {
            return mirrorfly::TextError::TooLarge;
        }

        if (!utf8::is_valid(text.begin(), text.end()))
        {
            return mirrorfly::TextError::InvalidEncoding;
        }

        return text.find('\0') == std::string::npos ? mirrorfly::TextError::None
                                                    : mirrorfly::TextError::BinaryContent;
    }

    std::string restore_lines(const std::string& normalized, mirrorfly::LineEnding line_ending)
    {
        std::string result;
        result.reserve(normalized.size());

        for (char character : normalized)
        {
            if (character == '\n')
            {
                if (line_ending != mirrorfly::LineEnding::Lf)
                {
                    result.push_back('\r');
                }

                if (line_ending == mirrorfly::LineEnding::Cr)
                {
                    continue;
                }
            }

            result.push_back(character);
        }

        return result;
    }

}

namespace mirrorfly
{

    DecodedText decode_text(std::string bytes)
    {
        DecodedText result;
        result.format.line_ending = LineEnding::Lf;

        if (bytes.size() > maximum_text_bytes)
        {
            result.error = TextError::TooLarge;
            return result;
        }

        if (starts_with(bytes, "\xFF\xFE\x00\x00", 4) || starts_with(bytes, "\x00\x00\xFE\xFF", 4))
        {
            result.error = TextError::InvalidEncoding;
            return result;
        }

        try
        {
            if (starts_with(bytes, "\xEF\xBB\xBF", 3))
            {
                result.format.encoding = TextEncoding::Utf8Bom;
                bytes.erase(0, 3);
            }
            else if (starts_with(bytes, "\xFF\xFE", 2) || starts_with(bytes, "\xFE\xFF", 2))
            {
                if (bytes.size() % 2 != 0)
                {
                    result.error = TextError::InvalidEncoding;
                    return result;
                }

                const bool little_endian = static_cast<unsigned char>(bytes[0]) == 0xFF;
                result.format.encoding =
                    little_endian ? TextEncoding::Utf16LittleEndian : TextEncoding::Utf16BigEndian;
                std::u16string units;
                units.reserve((bytes.size() - 2) / 2);

                for (std::size_t index = 2; index < bytes.size(); index += 2)
                {
                    const auto first = static_cast<unsigned char>(bytes[index]);
                    const auto second = static_cast<unsigned char>(bytes[index + 1]);
                    const auto unit = little_endian ? first | (second << 8) : (first << 8) | second;
                    units.push_back(static_cast<char16_t>(unit));
                }

                bytes.clear();
                utf8::utf16to8(units.begin(), units.end(), std::back_inserter(bytes));
            }

            result.error = validate_utf8(bytes);

            if (result.error == TextError::None)
            {
                result.text = normalize_lines(bytes, result.format);
            }
        }
        catch (const utf8::exception&)
        {
            result.error = TextError::InvalidEncoding;
        }

        return result;
    }

    EncodedText encode_text(std::string utf8_text, const TextFormat& format)
    {
        EncodedText result;
        result.error = validate_utf8(utf8_text);

        if (result.error != TextError::None)
        {
            return result;
        }

        if (format.line_ending != LineEnding::Lf && format.line_ending != LineEnding::CrLf &&
            format.line_ending != LineEnding::Cr)
        {
            result.error = TextError::InvalidEncoding;
            return result;
        }

        TextFormat normalized_format;
        utf8_text = restore_lines(normalize_lines(utf8_text, normalized_format), format.line_ending);
        result.error = validate_utf8(utf8_text);

        if (result.error != TextError::None)
        {
            return result;
        }

        try
        {
            switch (format.encoding)
            {
            case TextEncoding::Utf8:
                result.bytes = std::move(utf8_text);
                break;
            case TextEncoding::Utf8Bom:
                result.bytes = "\xEF\xBB\xBF" + utf8_text;
                break;
            case TextEncoding::Utf16LittleEndian:
            case TextEncoding::Utf16BigEndian:
            {
                std::u16string units;
                utf8::utf8to16(utf8_text.begin(), utf8_text.end(), std::back_inserter(units));

                if (units.size() > (maximum_text_bytes - 2) / 2)
                {
                    result.error = TextError::TooLarge;
                    return result;
                }

                const bool little_endian = format.encoding == TextEncoding::Utf16LittleEndian;
                result.bytes = little_endian ? "\xFF\xFE" : "\xFE\xFF";
                result.bytes.reserve(units.size() * 2 + 2);

                for (char16_t unit : units)
                {
                    const auto low = static_cast<char>(unit & 0xFF);
                    const auto high = static_cast<char>((unit >> 8) & 0xFF);
                    result.bytes.push_back(little_endian ? low : high);
                    result.bytes.push_back(little_endian ? high : low);
                }

                break;
            }
            default:
                result.error = TextError::InvalidEncoding;
                return result;
            }
        }
        catch (const utf8::exception&)
        {
            result.error = TextError::InvalidEncoding;
        }

        if (result.bytes.size() > maximum_text_bytes)
        {
            result.error = TextError::TooLarge;
        }

        if (result.error != TextError::None)
        {
            result.bytes.clear();
        }

        return result;
    }

    bool is_plain_text_path(std::string path)
    {
        const auto extension = text_extension(std::move(path));
        return extension == ".text" || extension == ".txt" || extension == ".md" || extension == ".markdown";
    }

    bool is_markdown_path(std::string path)
    {
        const auto extension = text_extension(std::move(path));
        return extension == ".md" || extension == ".markdown";
    }

}
