#pragma once

#include <map>
#include <mirrorfly/word.hpp>

namespace mirrorfly::word_detail
{
    std::string numbering_variants(std::vector<OfficePart>& parts, const WordDocument& document,
        const WordDocument* original, std::map<std::size_t, int>& ids);
    std::string materialize_numbering(std::vector<OfficePart>& parts, const WordDocument& document);
}
