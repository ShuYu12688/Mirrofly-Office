#pragma once
#include <mirrorfly/office_progress.hpp>

#include <mirrorfly/word.hpp>

#include <functional>

namespace mirrorfly
{
    enum class WordLoadStage
    {
        Reading,
        Validating,
        Extracting,
        Parsing
    };

    using WordLoadProgress =
        std::function<void(WordLoadStage stage, std::size_t completed, std::size_t total)>;

    WordResult load_word_file(const std::string& path, const WordLoadProgress& progress = {});
    // expected_revision="missing" requires a new destination.
    WordResult save_word_file(const std::string& path, const WordDocument& document,
        const std::string& expected_revision = {}, const OfficeSaveProgress& progress = {});
}
