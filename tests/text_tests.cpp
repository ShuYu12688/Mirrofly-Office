#include "mirrorfly/text.hpp"

#include <array>
#include <iostream>
#include <string>

namespace
{

    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }

        return condition;
    }

    bool test_encodings()
    {
        using namespace mirrorfly;
        const std::string content = u8"你好，Mirrorfly 🦋\n第二行\n";
        const std::array<TextEncoding, 4> encodings{TextEncoding::Utf8, TextEncoding::Utf8Bom,
            TextEncoding::Utf16LittleEndian, TextEncoding::Utf16BigEndian};
        bool passed = true;

        for (TextEncoding encoding : encodings)
        {
            const TextFormat format{encoding, LineEnding::CrLf, false};
            const auto encoded = encode_text(content, format);
            const auto decoded = decode_text(encoded.bytes);
            passed =
                check(encoded.error == TextError::None && decoded.error == TextError::None &&
                        decoded.text == content && decoded.format.encoding == encoding &&
                        decoded.format.line_ending == LineEnding::CrLf && !decoded.format.mixed_line_endings,
                    "Chinese and supplementary Unicode round-trip in every supported encoding") &&
                passed;
            passed = check(encode_text(decoded.text, decoded.format).bytes == encoded.bytes,
                         "unchanged supported document preserves original bytes") &&
                passed;

            const auto empty = encode_text("", format);
            const auto empty_decoded = decode_text(empty.bytes);
            passed = check(empty.error == TextError::None && empty_decoded.error == TextError::None &&
                             empty_decoded.text.empty() && empty_decoded.format.encoding == encoding,
                         "empty documents preserve the encoding marker") &&
                passed;
        }

        const auto plain = decode_text("");
        passed = check(plain.error == TextError::None && plain.format.encoding == TextEncoding::Utf8 &&
                         plain.format.line_ending == LineEnding::Lf &&
                         TextFormat{}.line_ending == LineEnding::CrLf,
                     "empty existing files use LF while new documents default to CRLF") &&
            passed;
        return passed;
    }

    bool test_line_endings()
    {
        using namespace mirrorfly;
        bool passed = true;
        const auto mixed = decode_text("one\r\ntwo\nthree\r\nfour\rfive");
        passed = check(mixed.error == TextError::None && mixed.text == "one\ntwo\nthree\nfour\nfive" &&
                         mixed.format.line_ending == LineEnding::CrLf && mixed.format.mixed_line_endings,
                     "mixed line endings normalize in memory and retain the dominant convention") &&
            passed;
        passed = check(encode_text(mixed.text, mixed.format).bytes == "one\r\ntwo\r\nthree\r\nfour\r\nfive",
                     "saving mixed line endings consistently applies the dominant convention") &&
            passed;
        const auto tied = decode_text("one\rtwo\nthree");
        passed = check(tied.format.line_ending == LineEnding::Cr && tied.format.mixed_line_endings,
                     "equal line-ending counts retain the first encountered convention") &&
            passed;
        passed = check(encode_text("a\nb\n", {TextEncoding::Utf8, LineEnding::Cr, false}).bytes == "a\rb\r",
                     "classic CR line endings can be preserved") &&
            passed;
        passed =
            check(encode_text("a\r\nb\rc", {TextEncoding::Utf8, LineEnding::Lf, false}).bytes == "a\nb\nc",
                "encoding normalizes incoming CR without doubling line breaks") &&
            passed;
        return passed;
    }

    bool test_rejections()
    {
        using namespace mirrorfly;
        const std::array<std::string, 7> malformed{std::string("\xC0\xAF", 2), std::string("\xE4\xB8", 2),
            std::string("\xED\xA0\x80", 3), std::string("\xF4\x90\x80\x80", 4),
            std::string("\xFF\xFE\x00\xD8", 4), std::string("\xFE\xFF\xDC\x00", 4),
            std::string("\xFF\xFE\x41", 3)};
        bool passed = true;

        for (const auto& bytes : malformed)
        {
            const auto result = decode_text(bytes);
            passed = check(result.error == TextError::InvalidEncoding && result.text.empty(),
                         "invalid Unicode is rejected without replacement characters") &&
                passed;
        }

        passed = check(decode_text(std::string("\xFF\xFE\x00\x00", 4)).error == TextError::InvalidEncoding &&
                         decode_text(std::string("\x00\x00\xFE\xFF", 4)).error == TextError::InvalidEncoding,
                     "UTF-32 BOMs are not mistaken for UTF-16") &&
            passed;
        passed =
            check(decode_text(std::string("a\0b", 3)).error == TextError::BinaryContent &&
                    decode_text(std::string("\xFF\xFE\x41\x00\x00\x00", 6)).error == TextError::BinaryContent,
                "NUL content is rejected for UTF-8 and UTF-16") &&
            passed;
        passed = check(encode_text(std::string("\xED\xA0\x80", 3), {}).error == TextError::InvalidEncoding &&
                         encode_text(std::string("a\0b", 3), {}).error == TextError::BinaryContent,
                     "saving validates Unicode and binary content before creating bytes") &&
            passed;
        return passed;
    }

    bool test_limits_and_paths()
    {
        using namespace mirrorfly;
        bool passed = true;
        const std::string maximum(maximum_text_bytes, 'a');
        passed = check(decode_text(maximum).error == TextError::None &&
                         encode_text(maximum, {TextEncoding::Utf8, LineEnding::Lf, false}).error ==
                             TextError::None,
                     "exactly 2 MiB of UTF-8 is accepted") &&
            passed;
        passed = check(decode_text(maximum + 'a').error == TextError::TooLarge &&
                         encode_text(maximum + 'a', {}).error == TextError::TooLarge,
                     "input above 2 MiB is rejected") &&
            passed;
        passed = check(encode_text(maximum, {TextEncoding::Utf8Bom, LineEnding::Lf, false}).error ==
                         TextError::TooLarge,
                     "the UTF-8 BOM counts toward the output limit") &&
            passed;
        passed = check(encode_text(std::string(maximum_text_bytes / 2, 'a'),
                           {TextEncoding::Utf16LittleEndian, LineEnding::Lf, false})
                             .error == TextError::TooLarge,
                     "UTF-16 expansion and its BOM count toward the output limit") &&
            passed;
        passed =
            check(encode_text(std::string(maximum_text_bytes / 2 + 1, '\n'), {}).error == TextError::TooLarge,
                "CRLF expansion is bounded after encoding") &&
            passed;

        std::string expanded = "\xFF\xFE";

        for (std::size_t index = 0; index < maximum_text_bytes / 2 - 1; ++index)
        {
            expanded += "\x2D\x4E";
        }

        passed = check(decode_text(expanded).error == TextError::TooLarge,
                     "UTF-16 content cannot expand the editor buffer beyond 2 MiB") &&
            passed;
        std::string chinese_lines;
        const std::size_t line_count = maximum_text_bytes / 10 - 1;
        chinese_lines.reserve(line_count * 10);

        for (std::size_t index = 0; index < line_count; ++index)
        {
            chinese_lines += u8"中中中\n";
        }

        const auto expanded_lines =
            encode_text(chinese_lines, {TextEncoding::Utf16LittleEndian, LineEnding::CrLf, false});
        passed =
            check(chinese_lines.size() <= maximum_text_bytes && line_count * 10 + 2 <= maximum_text_bytes &&
                    expanded_lines.error == TextError::TooLarge && expanded_lines.bytes.empty(),
                "UTF-16 save rejects CRLF-expanded UTF-8 that its own decoder would refuse") &&
            passed;
        passed = check(is_plain_text_path(u8"C:\\资料\\笔记.TeXt") &&
                         is_plain_text_path(u8"C:\\资料\\笔记.MD") && is_markdown_path("notes.MARKDOWN") &&
                         !is_markdown_path("notes.txt") && !is_markdown_path("notes.md/image.png") &&
                         is_plain_text_path("/work/readme.TXT") && !is_plain_text_path("/work.txt/name") &&
                         !is_plain_text_path("note.docx") && !is_plain_text_path(std::string("x\0.txt", 6)),
                     "plain text extension checks are case-insensitive and validate the filename") &&
            passed;
        return passed;
    }

}

int run_text_codec_tests()
{
    bool passed = test_encodings();
    passed = test_line_endings() && passed;
    passed = test_rejections() && passed;
    passed = test_limits_and_paths() && passed;

    if (passed)
    {
        std::cout << "Text codec tests passed.\n";
    }

    return passed ? 0 : 1;
}

int main()
{
    return run_text_codec_tests();
}
