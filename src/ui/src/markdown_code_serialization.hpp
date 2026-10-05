#pragma once
#include "markdown_break.hpp"
#include "markdown_code.hpp"
namespace mirrorfly
{
    struct MarkdownCodeSnapshot
    {
        std::vector<CodeRange> ranges;
        std::vector<QString> tokens;
    };
    MarkdownCodeSnapshot protect_markdown_code_blocks(QTextDocument& document);
    std::vector<MarkdownInlineToken> markdown_code_bodies(const MarkdownCodeSnapshot& snapshot);
}
