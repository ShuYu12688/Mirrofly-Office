#pragma once
#include <string>
namespace mirrorfly::detail
{
    bool whitespace(unsigned point);
    std::string boundary_literal(const std::string& text, bool encoded);
    std::string literal_whitespace(const std::string& text);
    std::string protect_markdown_whitespace(const std::string& source);
}
