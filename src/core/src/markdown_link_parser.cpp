#include "markdown_link_parser.hpp"
#include "markdown_decode.hpp"
#include "markdown_parse_context.hpp"
#include "mirrorfly/text.hpp"

#include <md4c.h>
#include <utf8/checked.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <string_view>

namespace
{
    using mirrorfly::detail::block_source_callback;
    using mirrorfly::detail::enter_block;
    using mirrorfly::detail::leave_block;
    using mirrorfly::detail::ParseContext;
    using mirrorfly::detail::style_index;

    int source_callback(MD_SPANTYPE type, MD_OFFSET start, MD_OFFSET end, MD_OFFSET label_start,
        MD_OFFSET label_end, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        if (start > label_start || label_start > label_end || label_end > end || end > context.source.size())
            return 1;
        context.pending.start = start;
        context.pending.end = end;
        context.pending.label_start = label_start;
        context.pending.label_end = label_end;
        context.pending_type = type;
        return 0;
    }

    int break_callback(MD_OFFSET start, MD_OFFSET end, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        if (start > end || end > context.source.size())
            return 1;
        try
        {
            if (context.image_depth == 0)
                context.result.breaks.push_back({start, end});
        }
        catch (...)
        {
            return 1;
        }
        return 0;
    }

    int enter_callback(MD_SPANTYPE type, void* detail, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        try
        {
            const int style = style_index(type);
            if (style >= 0)
                ++context.style_depth[style];
            if (type == MD_SPAN_CODE && context.image_depth == 0)
            {
                context.active_span = context.result.code_spans.size();
                context.result.code_spans.push_back({context.pending.start, context.pending.end, {}});
            }
            if (type == MD_SPAN_IMG)
            {
                context.result.images.emplace_back(context.pending.start, context.pending.end);
                if (context.image_depth == 0)
                {
                    const auto& image = *static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
                    context.result.image_objects.push_back({context.pending.start, context.pending.end,
                        mirrorfly::detail::markdown_attribute(image.src),
                        mirrorfly::detail::markdown_attribute(image.title), {}});
                }
                ++context.image_depth;
            }
            else if (type == MD_SPAN_A && context.image_depth == 0)
            {
                if (context.pending_type != type || context.pending.start == context.pending.end)
                    return 1;
                const auto& link_detail = *static_cast<const MD_SPAN_A_DETAIL*>(detail);
                auto link = context.pending;
                link.url = mirrorfly::detail::markdown_attribute(link_detail.href);
                link.title = mirrorfly::detail::markdown_attribute(link_detail.title);
                if (link_detail.is_autolink)
                    link.kind = context.source[link.start] == '<' ? "autolink" : "automatic";
                else
                    link.kind = link.label_end + 1 < context.source.size() &&
                            context.source[link.label_end + 1] == '('
                        ? "inline"
                        : "reference";
                context.active_link = context.result.links.size();
                context.result.links.push_back(std::move(link));
            }
        }
        catch (...)
        {
            // Stop through the C callback contract so the parser can release its allocations.
            return 1;
        }
        return 0;
    }

    int leave_callback(MD_SPANTYPE type, void*, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        const int style = style_index(type);
        if (style >= 0)
            --context.style_depth[style];
        if (type == MD_SPAN_CODE)
        {
            if (context.table_cell_depth != 0 && context.active_span < context.result.code_spans.size())
            {
                auto& text = context.result.code_spans[context.active_span].text;
                for (auto offset = text.find("\\|"); offset != std::string::npos;
                    offset = text.find("\\|", offset + 1))
                    text.erase(offset, 1);
            }
            context.active_span = std::numeric_limits<std::size_t>::max();
        }
        if (type == MD_SPAN_IMG)
            --context.image_depth;
        else if (type == MD_SPAN_A && context.image_depth == 0)
        {
            if (context.table_cell_depth != 0 && context.active_link < context.result.links.size())
                try
                {
                    auto& link = context.result.links[context.active_link];
                    link.text.clear();
                    for (auto& run : link.runs)
                    {
                        if (run.code)
                            for (auto offset = run.text.find("\\|"); offset != std::string::npos;
                                offset = run.text.find("\\|", offset + 1))
                                run.text.erase(offset, 1);
                        link.text += run.text;
                    }
                }
                catch (...)
                {
                    return 1;
                }
            context.active_link = std::numeric_limits<std::size_t>::max();
        }
        return 0;
    }

}

namespace mirrorfly::detail
{
    MarkdownSourceLinks parse_markdown_links(const std::string& source)
    {
        if (source.size() > maximum_text_bytes || source.find('\0') != std::string::npos ||
            !utf8::is_valid(source.begin(), source.end()))
            return {};
        ParseContext context{source, {}, {}};
        MD_PARSER parser{};
        parser.flags = MD_DIALECT_GITHUB;
        parser.enter_block = enter_block;
        parser.leave_block = leave_block;
        parser.enter_span = enter_callback;
        parser.leave_span = leave_callback;
        parser.text = collect_markdown_text;
        parser.source_span = source_callback;
        parser.source_break = break_callback;
        parser.source_block = block_source_callback;
        if (md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &context) != 0)
            return {};
        std::sort(context.result.links.begin(), context.result.links.end(),
            [](const MarkdownLinkInfo& left, const MarkdownLinkInfo& right)
        {
            return left.start < right.start;
        });
        return std::move(context.result);
    }
}

namespace mirrorfly
{
    bool markdown_has_raw_html(const std::string& source)
    {
        return detail::parse_markdown_links(source).has_raw_html;
    }

    std::vector<MarkdownBlockInfo> markdown_blocks(const std::string& source)
    {
        return detail::parse_markdown_links(source).blocks;
    }

    std::vector<MarkdownCodeSpanInfo> markdown_code_spans(const std::string& source)
    {
        return detail::parse_markdown_links(source).code_spans;
    }

    std::vector<MarkdownImageInfo> markdown_images(const std::string& source)
    {
        return detail::parse_markdown_links(source).image_objects;
    }

    std::vector<MarkdownLinkInfo> markdown_links(const std::string& source)
    {
        return detail::parse_markdown_links(source).links;
    }

    std::vector<MarkdownParagraphInfo> markdown_paragraphs(const std::string& source)
    {
        return detail::parse_markdown_links(source).paragraphs;
    }

    std::size_t markdown_thematic_break_count(const std::string& source)
    {
        return detail::parse_markdown_links(source).thematic_breaks;
    }

    std::vector<MarkdownHardBreakInfo> markdown_hard_breaks(const std::string& source)
    {
        return detail::parse_markdown_links(source).breaks;
    }
}
