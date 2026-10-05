#pragma once

#include "mirrorfly/markdown.hpp"

namespace mirrorfly::detail
{
    MarkdownEdit edit_markdown_heading(
        const std::string& source, std::size_t start, std::size_t end, int level);
}
