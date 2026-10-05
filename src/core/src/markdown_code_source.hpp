#pragma once
#include "mirrorfly/markdown.hpp"
namespace mirrorfly::detail
{
    MarkdownEdit fenced_markdown_code(
        const std::string& source, std::size_t start, std::size_t end, const std::string& language);
    MarkdownEdit edit_markdown_code_span(
        const std::string& source, std::size_t start, std::size_t end, const std::string& action);
}
