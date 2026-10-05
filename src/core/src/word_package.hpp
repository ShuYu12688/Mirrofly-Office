#pragma once

#include <mirrorfly/word.hpp>

#include <map>
#include <pugixml.hpp>
#include <set>

namespace mirrorfly
{
    struct WordPackageState
    {
        std::shared_ptr<const std::vector<OfficePart>> parts;
        WordDocument original;
        std::vector<std::vector<std::size_t>> paragraph_paths;
        std::map<std::uint64_t, std::vector<std::size_t>> run_paths;
        std::vector<std::size_t> body_path;
        std::vector<std::vector<std::size_t>> table_paths;
        std::vector<std::vector<std::vector<std::size_t>>> cell_paths;
        std::vector<bool> paragraph_structure_editable;
    };

    namespace word_detail
    {
        class StyleResolver;
        void attach_structure(WordDocument& document, pugi::xml_node body, std::vector<OfficePart> parts,
            StyleResolver& styles);
        WordResult serialize_preserved(const WordDocument& document);
        WordResult serialize_text(const WordDocument& document, bool materialize_lists = true);
        std::string numbering_path(const std::vector<OfficePart>& parts, std::string& error);
        std::string append_numbering(std::vector<OfficePart>& parts, const std::set<WordListKind>& kinds,
            std::map<WordListKind, int>& ids);
        std::string validate_structure(const WordDocument& document);
    }
}
