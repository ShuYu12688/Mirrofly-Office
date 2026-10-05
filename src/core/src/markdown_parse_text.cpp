#include "markdown_decode.hpp"
#include "markdown_parse_context.hpp"
#include <algorithm>

namespace mirrorfly::detail
{
    int collect_markdown_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
    {
        auto& context = *static_cast<ParseContext*>(userdata);
        try
        {
            if (type == MD_TEXT_HTML)
                context.result.has_raw_html = true;
            if (context.image_depth > 0)
            {
                auto& alt = context.result.image_objects.back().alt;
                mirrorfly::detail::append_markdown_text(alt, type, {text, size});
                std::replace(alt.begin(), alt.end(), '\n', ' ');
                return 0;
            }
            if (context.active_span < context.result.code_spans.size())
                mirrorfly::detail::append_markdown_text(
                    context.result.code_spans[context.active_span].text, type, {text, size});
            if (type == MD_TEXT_CODE && context.active_code < context.result.blocks.size())
                context.result.blocks[context.active_code].text.append(text, size);
            if (context.active_table < context.result.blocks.size() && context.table_cell_depth > 0 &&
                context.image_depth == 0)
            {
                std::string decoded;
                mirrorfly::detail::append_markdown_text(decoded, type, {text, size});
                if (context.style_depth[3] > 0)
                    for (auto offset = decoded.find("\\|"); offset != std::string::npos;
                        offset = decoded.find("\\|", offset + 1))
                        decoded.erase(offset, 1);
                if (!decoded.empty())
                {
                    mirrorfly::MarkdownParagraphRun run;
                    run.style = {decoded, context.style_depth[0] != 0, context.style_depth[1] != 0,
                        context.style_depth[2] != 0, context.style_depth[3] != 0, type == MD_TEXT_BR};
                    if (context.active_link < context.result.links.size())
                    {
                        const auto& link = context.result.links[context.active_link];
                        run.link_id = link.start + 1;
                        run.url = link.url;
                        run.title = link.title;
                    }
                    context.result.blocks[context.active_table].cells.back().push_back(std::move(run));
                }
            }
            if (context.active_paragraph < context.result.paragraphs.size() && context.image_depth == 0)
            {
                std::string decoded;
                mirrorfly::detail::append_markdown_text(decoded, type, {text, size});
                if (!decoded.empty())
                {
                    mirrorfly::MarkdownParagraphRun run;
                    run.style = {decoded, context.style_depth[0] != 0, context.style_depth[1] != 0,
                        context.style_depth[2] != 0, context.style_depth[3] != 0, type == MD_TEXT_BR};
                    if (context.active_link < context.result.links.size())
                    {
                        const auto& link = context.result.links[context.active_link];
                        run.link_id = link.start + 1;
                        run.url = link.url;
                        run.title = link.title;
                    }
                    context.result.paragraphs[context.active_paragraph].runs.push_back(std::move(run));
                }
            }
            if (context.active_link < context.result.links.size())
            {
                auto& link = context.result.links[context.active_link];
                std::string decoded;
                mirrorfly::detail::append_markdown_text(decoded, type, {text, size});
                if (decoded.empty())
                    return 0;
                link.text += decoded;
                mirrorfly::MarkdownInlineRun run{decoded, context.style_depth[0] != 0,
                    context.style_depth[1] != 0, context.style_depth[2] != 0, context.style_depth[3] != 0,
                    type == MD_TEXT_BR};
                if (!link.runs.empty() && link.runs.back().bold == run.bold &&
                    link.runs.back().italic == run.italic && link.runs.back().strike == run.strike &&
                    link.runs.back().code == run.code && link.runs.back().hard_break == run.hard_break)
                    link.runs.back().text += decoded;
                else
                    link.runs.push_back(std::move(run));
            }
        }
        catch (...)
        {
            return 1;
        }
        return 0;
    }
}
