#pragma once

#include <cstddef>
#include <string>

namespace mirrorfly
{

    constexpr std::size_t maximum_text_bytes = 2 * 1024 * 1024;

    enum class TextEncoding
    {
        Utf8,
        Utf8Bom,
        Utf16LittleEndian,
        Utf16BigEndian
    };

    enum class LineEnding
    {
        Lf,
        CrLf,
        Cr
    };

    struct TextFormat
    {
        TextEncoding encoding = TextEncoding::Utf8;
        LineEnding line_ending = LineEnding::CrLf;
        bool mixed_line_endings = false;
    };

    enum class TextError
    {
        None,
        TooLarge,
        InvalidEncoding,
        BinaryContent
    };

    struct DecodedText
    {
        TextError error = TextError::None;
        std::string text;
        TextFormat format;
    };

    struct EncodedText
    {
        TextError error = TextError::None;
        std::string bytes;
    };

    // Decode strictly to UTF-8 with LF line endings; never replace invalid Unicode.
    DecodedText decode_text(std::string bytes);
    EncodedText encode_text(std::string utf8_text, const TextFormat& format);
    bool is_plain_text_path(std::string path);
    bool is_markdown_path(std::string path);

}
