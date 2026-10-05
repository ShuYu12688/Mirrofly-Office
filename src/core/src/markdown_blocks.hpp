#pragma once

#include "mirrorfly/markdown.hpp"

namespace mirrorfly::detail
{
    MarkdownEdit prefix_markdown_lines(
        const std::string& source, std::size_t start, std::size_t end, const std::string& action);
    MarkdownEdit edit_markdown_list(const std::string& source, std::size_t start, std::size_t end,
        const std::string& action, const MarkdownOptions& options);
    MarkdownEdit edit_markdown_quote(
        const std::string& source, std::size_t start, std::size_t end, int level);
}
