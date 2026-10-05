#pragma once
#include "mirrorfly/markdown.hpp"
namespace mirrorfly::detail
{
    MarkdownEdit wrap_markdown_emphasis(const std::string& source, std::size_t start, std::size_t end,
        const std::string& before, const std::string& after, const std::string& placeholder);
}
