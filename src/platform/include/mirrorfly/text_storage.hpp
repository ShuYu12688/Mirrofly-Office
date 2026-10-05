#pragma once

#include <mirrorfly/text.hpp>

#include <string>

namespace mirrorfly
{

    enum class TextFileError
    {
        None,
        UnsupportedType,
        TooLarge,
        ReadFailed,
        InvalidEncoding,
        BinaryContent,
        WriteFailed,
        ChangedOnDisk
    };

    struct TextFileResult
    {
        TextFileError error = TextFileError::None;
        std::string path;
        std::string text;
        TextFormat format;
        std::string revision;
    };

    TextFileResult load_text_file(const std::string& path);

    // A nonempty revision must match the file bytes before the atomic replacement.
    // expected_revision="missing" requires a new destination.
    TextFileResult save_text_file(const std::string& path, const std::string& text, const TextFormat& format,
        const std::string& expected_revision);

}
