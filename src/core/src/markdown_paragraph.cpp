#include "mirrorfly/markdown.hpp"
#include "mirrorfly/text.hpp"

namespace mirrorfly
{
    std::string markdown_paragraph_text(const std::vector<MarkdownParagraphRun>& runs, bool table_cell)
    {
        std::string result;
        for (std::size_t start = 0; start < runs.size();)
        {
            const auto& first = runs[start];
            if ((first.link_id == 0 && (!first.url.empty() || !first.title.empty())) ||
                (first.link_id != 0 && first.url.empty()))
                return {};
            auto end = start;
            std::vector<MarkdownInlineRun> styles;
            while (end < runs.size() && runs[end].link_id == first.link_id)
            {
                if (runs[end].url != first.url || runs[end].title != first.title)
                    return {};
                styles.push_back(runs[end++].style);
            }
            auto text = markdown_inline_label(styles, table_cell);
            if (text.empty())
                return {};
            if (first.link_id != 0)
            {
                const auto suffix = markdown_link_suffix(first.url, first.title);
                if (suffix.empty())
                    return {};
                text = "[" + text + "]" + suffix;
            }
            if (text.size() > maximum_text_bytes - result.size())
                return {};
            result += text;
            start = end;
        }
        return result;
    }
}
