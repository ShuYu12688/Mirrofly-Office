#pragma once
#include <md4c.h>
#include <string>
#include <string_view>

namespace mirrorfly::detail
{
    void append_markdown_text(std::string& output, MD_TEXTTYPE type, std::string_view text);
    std::string markdown_attribute(const MD_ATTRIBUTE& value);
}
