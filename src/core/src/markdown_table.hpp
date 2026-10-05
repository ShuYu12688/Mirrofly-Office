#pragma once

#include "mirrorfly/markdown.hpp"

namespace mirrorfly::detail
{
    std::string markdown_line_ending(const std::string& source);
    MarkdownEdit insert_markdown_source_table(
        const std::string& source, std::size_t start, std::size_t end, const MarkdownOptions& options);
    MarkdownEdit align_markdown_source_table(
        const std::string& source, std::size_t start, std::size_t end, const MarkdownOptions& options);
}
