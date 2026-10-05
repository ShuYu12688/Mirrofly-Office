#include "mirrorfly/markdown.hpp"
#include "markdown_blocks.hpp"
#include "markdown_break.hpp"
#include "markdown_code_source.hpp"
#include "markdown_emphasis_source.hpp"
#include "markdown_heading.hpp"
#include "markdown_image.hpp"
#include "markdown_link.hpp"
#include "markdown_rule.hpp"
#include "markdown_table.hpp"
#include "mirrorfly/text.hpp"

#include <utf8/checked.h>

#include <algorithm>
#include <string_view>

namespace
{

    bool is_boundary(const std::string& text, std::size_t offset)
    {
        return offset <= text.size() &&
            (offset == text.size() || (static_cast<unsigned char>(text[offset]) & 0xC0) != 0x80);
    }

}

namespace mirrorfly
{

    MarkdownEdit make_markdown_edit(const std::string& source, std::size_t selection_start,
        std::size_t selection_end, const std::string& action, const MarkdownOptions& options)
    {
        if (source.size() > maximum_text_bytes || !is_boundary(source, selection_start) ||
            !is_boundary(source, selection_end) || source.find('\0') != std::string::npos ||
            !utf8::is_valid(source.begin(), source.end()))
        {
            return {};
        }

        const auto start = std::min(selection_start, selection_end);
        const auto end = std::max(selection_start, selection_end);
        if (options.expected_text_set &&
            ((!utf8::is_valid(options.expected_text.begin(), options.expected_text.end())) ||
                (start != end && options.expected_text.size() != end - start) ||
                options.expected_text.size() > source.size() - start ||
                source.compare(start, options.expected_text.size(), options.expected_text) != 0))
            return {};
        MarkdownEdit edit;

        if (action == "image" || action == "removeImage")
        {
            edit = detail::edit_markdown_image(source, start, end, action, options);
        }
        else if (action == "thematicBreak")
        {
            edit = detail::insert_markdown_rule(source, start, end);
        }
        else if (action == "hardBreak")
        {
            edit = detail::insert_markdown_hard_break(source, start, end);
        }
        else if (action == "bold")
        {
            edit = detail::wrap_markdown_emphasis(source, start, end, "**", "**", u8"粗体文字");
        }
        else if (action == "italic")
        {
            edit = detail::wrap_markdown_emphasis(source, start, end, "*", "*", u8"斜体文字");
        }
        else if (action == "strike")
        {
            edit = detail::wrap_markdown_emphasis(source, start, end, "~~", "~~", u8"删除线文字");
        }
        else if (action == "inlineCode" || action == "removeInlineCode")
        {
            edit = detail::edit_markdown_code_span(source, start, end, action);
        }
        else if (action == "link" || action == "unlink")
        {
            edit = detail::edit_markdown_link(source, start, end, action, options);
        }
        else if (action == "quote" || action == "quoteSet")
        {
            edit =
                detail::edit_markdown_quote(source, start, end, action == "quote" ? 1 : options.quote_level);
        }
        else if (action == "heading" || action == "paragraph")
        {
            if (action == "heading" && (options.heading_level < 1 || options.heading_level > 6))
                return {};
            edit = detail::edit_markdown_heading(
                source, start, end, action == "heading" ? options.heading_level : 0);
        }
        else if (action == "bullet" || action == "ordered" || action == "task")
        {
            edit = detail::prefix_markdown_lines(source, start, end, action);
        }
        else if (action == "listIndent" || action == "listOutdent" || action == "taskSet")
        {
            edit = detail::edit_markdown_list(source, start, end, action, options);
        }
        else if (action == "code")
        {
            edit = detail::fenced_markdown_code(source, start, end, options.language);
        }
        else if (action == "table")
        {
            edit = detail::insert_markdown_source_table(source, start, end, options);
        }
        else if (action == "tableAlign")
        {
            edit = detail::align_markdown_source_table(source, start, end, options);
        }

        if (!edit.valid ||
            source.size() - (edit.end - edit.start) + edit.replacement.size() > maximum_text_bytes)
        {
            return {};
        }

        return edit;
    }

}
