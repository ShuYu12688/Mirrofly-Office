#include "markdown_decode.hpp"
#include "markdown_parse_context.hpp"
#include <algorithm>

namespace mirrorfly::detail
{
    int style_index(MD_SPANTYPE type)
    {
        if (type == MD_SPAN_STRONG)
            return 0;
        if (type == MD_SPAN_EM)
            return 1;
        if (type == MD_SPAN_DEL)
            return 2;
        if (type == MD_SPAN_CODE)
            return 3;
        return -1;
    }

    int enter_block(MD_BLOCKTYPE type, void* detail, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        if (type == MD_BLOCK_TH || type == MD_BLOCK_TD)
            ++context.table_cell_depth;
        if (type == MD_BLOCK_QUOTE)
            ++context.quote_depth;
        if ((type == MD_BLOCK_UL || type == MD_BLOCK_OL) && context.list_depth == 0)
            ++context.list_group;
        if (type == MD_BLOCK_LI)
            ++context.list_depth;
        if (type == MD_BLOCK_HR)
            ++context.result.thematic_breaks;
        try
        {
            if (type == MD_BLOCK_H && context.active_paragraph < context.result.paragraphs.size())
                context.result.paragraphs[context.active_paragraph].heading_level =
                    static_cast<const MD_BLOCK_H_DETAIL*>(detail)->level;
            if (type == MD_BLOCK_CODE || type == MD_BLOCK_TABLE || type == MD_BLOCK_HR)
            {
                const auto kind = type == MD_BLOCK_CODE ? "code" : "table";
                context.result.blocks.push_back(
                    {type == MD_BLOCK_HR ? "thematicBreak" : kind, context.containers});
                context.result.blocks.back().start = context.block_start;
                context.result.blocks.back().end = context.block_end;
                if (type == MD_BLOCK_CODE)
                {
                    context.active_code = context.result.blocks.size() - 1;
                    context.result.blocks.back().language = mirrorfly::detail::markdown_attribute(
                        static_cast<const MD_BLOCK_CODE_DETAIL*>(detail)->lang);
                }
                else if (type == MD_BLOCK_TABLE)
                {
                    context.active_table = context.result.blocks.size() - 1;
                    context.result.blocks.back().columns =
                        static_cast<const MD_BLOCK_TABLE_DETAIL*>(detail)->col_count;
                }
            }
            if ((type == MD_BLOCK_TH || type == MD_BLOCK_TD) &&
                context.active_table < context.result.blocks.size())
                context.result.blocks[context.active_table].cells.emplace_back();
            if (type == MD_BLOCK_QUOTE)
                context.containers.push_back({++context.container_identity, "quote", "> ", "> "});
            else if (type == MD_BLOCK_UL)
            {
                const auto& data = *static_cast<const MD_BLOCK_UL_DETAIL*>(detail);
                context.lists.push_back({false, 0, data.mark, ++context.list_identity, data.is_tight != 0});
            }
            else if (type == MD_BLOCK_OL)
            {
                const auto& data = *static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
                context.lists.push_back(
                    {true, data.start, data.mark_delimiter, ++context.list_identity, data.is_tight != 0});
            }
            else if (type == MD_BLOCK_LI)
            {
                auto& list = context.lists.back();
                const unsigned ordinal = list.next;
                auto opening = list.ordered
                    ? std::to_string(std::min(list.next++, 999999999u)) + list.delimiter + " "
                    : std::string(1, list.delimiter) + " ";
                const auto continuation = std::string(opening.size(), ' ');
                const auto& data = *static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
                if (data.is_task)
                    opening += data.task_mark == ' ' ? "[ ] " : "[x] ";
                context.containers.push_back({++context.container_identity, "listItem", opening, continuation,
                    list.ordered, ordinal, list.delimiter, list.identity, list.tight});
            }
        }
        catch (...)
        {
            return 1;
        }
        return 0;
    }

    int leave_block(MD_BLOCKTYPE type, void*, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        if (type == MD_BLOCK_CODE)
            context.active_code = std::numeric_limits<std::size_t>::max();
        if (type == MD_BLOCK_TABLE)
            context.active_table = std::numeric_limits<std::size_t>::max();
        if (type == MD_BLOCK_TH || type == MD_BLOCK_TD)
            --context.table_cell_depth;
        if (type == MD_BLOCK_QUOTE)
            --context.quote_depth;
        if (type == MD_BLOCK_LI)
            --context.list_depth;
        if (type == MD_BLOCK_LI || type == MD_BLOCK_QUOTE)
            context.containers.pop_back();
        if (type == MD_BLOCK_UL || type == MD_BLOCK_OL)
            context.lists.pop_back();
        return 0;
    }

    int block_source_callback(MD_BLOCKTYPE type, MD_OFFSET start, MD_OFFSET end, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        context.active_paragraph = std::numeric_limits<std::size_t>::max();
        if (start > end || end > context.source.size())
            return 1;
        context.block_start = start;
        context.block_end = end;
        try
        {
            context.result.leaves.push_back({start, end, context.containers});
            if (type != MD_BLOCK_P && type != MD_BLOCK_H)
                return 0;
            context.active_paragraph = context.result.paragraphs.size();
            context.result.paragraphs.push_back({start, end, context.quote_depth, context.list_depth,
                context.list_depth == 0 ? 0 : context.list_group, type == MD_BLOCK_H});
            context.result.paragraphs.back().containers = context.containers;
        }
        catch (...)
        {
            return 1;
        }
        return 0;
    }

}
