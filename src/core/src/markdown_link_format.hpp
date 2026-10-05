#pragma once

#include <string>

namespace mirrorfly::detail
{
    std::string escape_markdown_link(const std::string& text, bool label);
}
