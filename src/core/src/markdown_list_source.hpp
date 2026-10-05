#pragma once

#include "markdown_structure.hpp"

namespace mirrorfly::detail
{
    void restore_markdown_list_ownership(SourceLists& document, const std::string& source);
    std::string shifted_indent(const SourceLine& line, int delta);
    bool preserve_moved_markdown_code(const std::string& source, const SourceLists& document,
        MarkdownEdit& edit, int quote_level, int delta);
}
