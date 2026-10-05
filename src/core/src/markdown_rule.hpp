#pragma once

#include "mirrorfly/markdown.hpp"

namespace mirrorfly::detail
{
    MarkdownEdit insert_markdown_rule(const std::string& source, std::size_t start, std::size_t end);
}
