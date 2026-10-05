#pragma once

#include "mirrorfly/markdown.hpp"

#include <utility>

namespace mirrorfly::detail
{
    struct MarkdownLeafRange
    {
        std::size_t start;
        std::size_t end;
        std::vector<MarkdownContainerInfo> containers;
    };

    struct MarkdownSourceLinks
    {
        std::vector<MarkdownLinkInfo> links;
        std::vector<std::pair<std::size_t, std::size_t>> images;
        std::vector<MarkdownImageInfo> image_objects;
        std::vector<MarkdownCodeSpanInfo> code_spans;
        std::vector<MarkdownHardBreakInfo> breaks;
        std::vector<MarkdownParagraphInfo> paragraphs;
        std::vector<MarkdownBlockInfo> blocks;
        std::vector<MarkdownLeafRange> leaves;
        std::size_t thematic_breaks = 0;
        bool has_raw_html = false;
    };

    MarkdownSourceLinks parse_markdown_links(const std::string& source);
}
