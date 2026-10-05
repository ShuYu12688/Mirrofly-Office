#include <mandarin_data.hpp>
#include <mirrorfly/word.hpp>

#include <algorithm>
#include <iterator>

namespace mirrorfly
{
    std::string word_pinyin(std::uint32_t code_point)
    {
        const auto found = std::lower_bound(std::begin(mirrorfly_unihan::readings),
            std::end(mirrorfly_unihan::readings), code_point, [](const auto& item, std::uint32_t code)
        {
            return item.code < code;
        });
        if (found == std::end(mirrorfly_unihan::readings) || found->code != code_point)
            return {};
        return mirrorfly_unihan::syllables[found->index];
    }
}
