#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mirrorfly
{

    struct MarkdownOptions
    {
        int heading_level = 2;
        int quote_level = 1;
        int table_rows = 3;
        int table_columns = 3;
        int table_column = -1;
        std::string table_alignment;
        std::string language;
        std::string url = "https://example.com";
        std::string title;
        bool task_checked = false;
        std::string image_alt;
        bool image_alt_set = false;
        // Optional exact source guard; a caret checks the text beginning there without consuming it.
        std::string expected_text;
        bool expected_text_set = false;
    };

    struct MarkdownEdit
    {
        bool valid = false;
        std::size_t start = 0;
        std::size_t end = 0;
        std::string replacement;
        std::size_t selection_start = 0;
        std::size_t selection_end = 0;
    };

    struct MarkdownTableColumn
    {
        std::size_t delimiter_start = 0;
        std::size_t delimiter_end = 0;
        std::string alignment;
    };

    struct MarkdownTableInfo
    {
        std::size_t start = 0;
        std::size_t end = 0;
        int rows = 0;
        // Absolute quote depth; list_quote_level is the depth outside the owning list.
        int quote_level = 0;
        // Required source continuation indentation, in spaces, after outer quote markers.
        int list_indent = 0;
        int list_quote_level = 0;
        std::size_t list_owner_start = 0;
        std::vector<MarkdownTableColumn> columns;
    };

    // GFM table/container metadata; UTF-8 offsets exclude the final line ending.
    std::vector<MarkdownTableInfo> markdown_tables(const std::string& source);

    struct MarkdownInlineRun
    {
        std::string text;
        bool bold = false;
        bool italic = false;
        bool strike = false;
        bool code = false;
        // Explicit line breaks use one or more '\n' characters; ordinary runs remain single-line.
        bool hard_break = false;
    };

    struct MarkdownLinkInfo
    {
        std::size_t start = 0;
        std::size_t end = 0;
        std::size_t label_start = 0;
        std::size_t label_end = 0;
        std::string url;
        std::string title;
        // inline, reference, autolink (angle brackets), or automatic (GFM bare address).
        std::string kind;
        std::string text;
        // Decoded textual caption styles, including inherited emphasis; images are not described here.
        std::vector<MarkdownInlineRun> runs;
    };

    // Resolved links, excluding images and code; source and label ranges are UTF-8, end exclusive.
    std::vector<MarkdownLinkInfo> markdown_links(const std::string& source);

    struct MarkdownCodeSpanInfo
    {
        // Original source, including delimiter strings, UTF-8 end exclusive.
        std::size_t start = 0;
        std::size_t end = 0;
        // CommonMark decoded literal: line endings become spaces; container prefixes are excluded.
        std::string text;
    };
    std::vector<MarkdownCodeSpanInfo> markdown_code_spans(const std::string& source);

    struct MarkdownImageInfo
    {
        // Outermost rendered images only; UTF-8 source bounds, end exclusive.
        std::size_t start = 0;
        std::size_t end = 0;
        std::string url;
        std::string title;
        // Decoded plain alternative text; nested captions flatten as CommonMark recommends.
        std::string alt;
    };
    std::vector<MarkdownImageInfo> markdown_images(const std::string& source);
    // Canonical inline image; empty destination/alt are valid. Invalid parameters return empty.
    std::string markdown_image_source(
        const std::string& url, const std::string& alt, const std::string& title = {});

    struct MarkdownParagraphRun
    {
        MarkdownInlineRun style;
        // Paragraph-local identity, zero for ordinary text; adjacent equal targets stay distinct.
        std::size_t link_id = 0;
        std::string url;
        std::string title;
    };
    // Encode a paragraph's literal styled text and independent links; invalid input returns empty.
    std::string markdown_paragraph_text(
        const std::vector<MarkdownParagraphRun>& runs, bool table_cell = false);

    struct MarkdownContainerInfo
    {
        // Parse-local identity; kind is quote or listItem, in outer-to-inner order.
        std::size_t identity = 0;
        std::string kind;
        std::string opening;
        std::string continuation;
        // List semantics follow the first marker; later source numerals do not restart a sequence.
        bool ordered = false;
        unsigned ordinal = 0;
        char delimiter = 0;
        // Parse-local list identity and CommonMark tightness, shared by sibling items.
        std::size_t list_identity = 0;
        bool tight = true;
    };

    struct MarkdownParagraphInfo
    {
        std::size_t start = 0;
        std::size_t end = 0;
        int quote_level = 0;
        int list_depth = 0;
        // Parse-local identity of the outer list; zero outside lists.
        std::size_t list_group = 0;
        bool heading = false;
        // Decoded textual runs; image objects are excluded, not represented as editable text.
        std::vector<MarkdownParagraphRun> runs;
        // Ordered ownership distinguishes quote-in-list from list-in-quote and continued items.
        std::vector<MarkdownContainerInfo> containers;
        // 0 for paragraphs, 1..6 for ATX/Setext headings; heading remains the legacy boolean view.
        int heading_level = 0;
    };
    // Parsed paragraph/heading content ranges, UTF-8 end exclusive; excludes leading block markers.
    // Empty ATX headings have start == end and no runs; they remain distinct heading nodes.
    std::vector<MarkdownParagraphInfo> markdown_paragraphs(const std::string& source);
    struct MarkdownBlockInfo
    {
        // Non-paragraph leaves in document order: code, table, thematicBreak.
        std::string kind;
        std::vector<MarkdownContainerInfo> containers;
        // Literal decoded code content and language; empty for other kinds.
        std::string text;
        std::string language;
        // GFM table cells in row-major order, with explicit inline styles; headers have no implied bold.
        unsigned columns = 0;
        std::vector<std::vector<MarkdownParagraphRun>> cells;
        // UTF-8 leaf content bounds, end exclusive; fenced code includes its delimiters.
        std::size_t start = 0;
        std::size_t end = 0;
    };
    std::vector<MarkdownBlockInfo> markdown_blocks(const std::string& source);
    // Actual raw HTML outside code, including image captions; escaped text/link attributes are excluded.
    bool markdown_has_raw_html(const std::string& source);
    // Actual thematic-break block count; code/HTML/Setext headings are excluded.
    std::size_t markdown_thematic_break_count(const std::string& source);

    struct MarkdownHardBreakInfo
    {
        std::size_t start = 0;
        std::size_t end = 0;
    };
    // Parsed hard breaks only; end includes the next line's container prefix, UTF-8 end exclusive.
    std::vector<MarkdownHardBreakInfo> markdown_hard_breaks(const std::string& source);

    struct MarkdownContinuation
    {
        std::size_t start = 0;
        std::size_t end = 0;
        std::string prefix;
    };
    // Source line ranges and the quote/list prefix required to continue the same paragraph.
    std::vector<MarkdownContinuation> markdown_continuations(const std::string& source);
    // Escape a newly split paragraph line's block opener, retaining inline markup; invalid input is empty.
    std::string markdown_escape_paragraph_start(const std::string& line);
    // CommonMark inline destination/title suffix; invalid or oversized parameters return empty.
    std::string markdown_link_suffix(const std::string& url, const std::string& title = {});
    std::string markdown_link_label_literal(const std::string& text);

    // Compose literal link-label runs without changing visible text. Empty/invalid/oversized input returns
    // empty. Whitespace at style boundaries is kept outside emphasis; table_cell escapes code pipes for GFM.
    // Character styles include boundary/all-whitespace runs; source may use character references.
    std::string markdown_inline_label(const std::vector<MarkdownInlineRun>& runs, bool table_cell = false);

    // Quote literal inline code using a collision-free CommonMark delimiter; invalid text returns empty.
    std::string markdown_code_span(const std::string& text);

    // All offsets are UTF-8 byte boundaries. Result selection offsets are relative to replacement.
    // heading (1..6) and paragraph change complete parsed paragraphs, preserving list/quote parents.
    // Setext/soft multiline headings normalize to ATX; hard breaks cannot become single-line headings.
    MarkdownEdit make_markdown_edit(const std::string& source, std::size_t selection_start,
        std::size_t selection_end, const std::string& action, const MarkdownOptions& options = {});

}
