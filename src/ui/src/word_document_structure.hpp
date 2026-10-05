#pragma once

#include <mirrorfly/word.hpp>

#include <map>

class QTextDocument;

namespace mirrorfly
{
    bool extract_word_structure(const QTextDocument& editor, const WordDocument& original,
        WordDocument& current, const std::map<int, std::size_t>& blocks, std::string& error);
}
