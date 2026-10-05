#pragma once

#include "markdown_link_parser.hpp"
#include <array>
#include <limits>
#include <md4c.h>

namespace mirrorfly::detail
{
    struct ParseContext
    {
        const std::string& source;
        mirrorfly::detail::MarkdownSourceLinks result;
        mirrorfly::MarkdownLinkInfo pending;
        MD_SPANTYPE pending_type = MD_SPAN_A;
        std::size_t active_paragraph = std::numeric_limits<std::size_t>::max();
        std::size_t active_link = std::numeric_limits<std::size_t>::max();
        std::size_t active_code = std::numeric_limits<std::size_t>::max();
        std::size_t active_table = std::numeric_limits<std::size_t>::max();
        std::size_t block_start = 0;
        std::size_t block_end = 0;
        unsigned image_depth = 0;
        std::size_t active_span = std::numeric_limits<std::size_t>::max();
        unsigned table_cell_depth = 0;
        int quote_depth = 0;
        int list_depth = 0;
        std::size_t list_group = 0;
        std::array<unsigned, 4> style_depth{};
        struct List
        {
            bool ordered;
            unsigned next;
            char delimiter;
            std::size_t identity;
            bool tight;
        };
        std::vector<List> lists;
        std::vector<mirrorfly::MarkdownContainerInfo> containers;
        std::size_t container_identity = 0;
        std::size_t list_identity = 0;
    };

    int collect_markdown_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata);
    int style_index(MD_SPANTYPE type);
    int enter_block(MD_BLOCKTYPE type, void* detail, void* userdata);
    int leave_block(MD_BLOCKTYPE type, void* detail, void* userdata);
    int block_source_callback(MD_BLOCKTYPE type, MD_OFFSET start, MD_OFFSET end, void* userdata);
}
