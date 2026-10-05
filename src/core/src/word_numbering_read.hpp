#pragma once

#include <map>
#include <mirrorfly/word.hpp>

namespace mirrorfly::word_detail
{
    struct NumberingLevel
    {
        WordListKind kind = WordListKind::Numbered;
        std::string marker = "decimal";
        int start = 1;
        std::string text;
    };
    using NumberingLevels = std::map<std::pair<int, int>, NumberingLevel>;
    NumberingLevels read_numbering(const OfficePart* part);
}
