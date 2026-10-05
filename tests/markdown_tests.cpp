#include "mirrorfly/markdown.hpp"
#include "mirrorfly/text.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace
{

    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }

        return condition;
    }

    std::string apply_edit(const std::string& source, const mirrorfly::MarkdownEdit& edit)
    {
        return edit.valid ? source.substr(0, edit.start) + edit.replacement + source.substr(edit.end)
                          : source;
    }

    std::string selected(const mirrorfly::MarkdownEdit& edit)
    {
        return edit.replacement.substr(edit.selection_start, edit.selection_end - edit.selection_start);
    }

    bool test_expected_text()
    {
        using namespace mirrorfly;
        const std::string source = u8"🦋 中文 target";
        const auto start = source.find(u8"中文");
        const auto end = start + std::string(u8"中文").size();
        MarkdownOptions options;
        options.expected_text_set = true;
        options.expected_text = u8"中文";
        bool passed = check(make_markdown_edit(source, start, end, "bold", options).valid &&
                make_markdown_edit(source, end, start, "bold", options).valid,
            "exact UTF-8 guard supports both selection directions");
        passed = check(!make_markdown_edit(source, start + 3, end, "bold", options).valid &&
                         !make_markdown_edit("prefix " + source, start, end, "bold", options).valid,
                     "stale and partial source selections reject atomically") &&
            passed;
        passed = check(make_markdown_edit(source, start, start, "hardBreak", options).valid &&
                         !make_markdown_edit(source, end, end, "hardBreak", options).valid,
                     "caret guards verify following text without consuming it") &&
            passed;
        options.expected_text = std::string("\xC0\xAF", 2);
        passed = check(!make_markdown_edit(source, start, start, "bold", options).valid,
                     "malformed expected UTF-8 cannot pass the source guard") &&
            passed;
        options.expected_text = "";
        passed = check(!make_markdown_edit(source, start, end, "bold", options).valid,
                     "empty guard does not match a nonempty selection") &&
            passed;
        return passed;
    }

    bool test_raw_html()
    {
        for (const std::string source : {"<b>x</b>", "> <div>x</div>", "<!-- comment -->", "<!DOCTYPE html>",
                 "text <i>x</i>", "![<b>x</b>](x.png)"})
            if (!check(mirrorfly::markdown_has_raw_html(source), "actual raw HTML is reported publicly"))
                return false;
        for (const std::string source :
            {"\\<b>x\\</b>", "`<b>x</b>`", "    <b>x</b>\n", "> ```html\n> <b>x</b>\n> ```\n",
                "- ```html\n  <b>x</b>\n  ```\n", "[x](../x \"<b>\")", "ordinary | | |"})
            if (!check(!mirrorfly::markdown_has_raw_html(source),
                    "literal HTML and attributes are not raw HTML"))
                return false;
        return true;
    }

    bool test_empty_headings()
    {
        for (const std::string prefix : {"", "> ", "- ", "> - ", "- > ", "100) "})
            for (int level = 1; level <= 6; ++level)
            {
                const auto source = prefix + "##\n";
                const auto before = mirrorfly::markdown_paragraphs(source);
                if (!check(before.size() == 1 && before[0].start == before[0].end && before[0].runs.empty(),
                        "empty ATX heading has a zero-length public content range"))
                    return false;
                mirrorfly::MarkdownOptions options;
                options.heading_level = level;
                const auto edit = mirrorfly::make_markdown_edit(
                    source, before[0].start, before[0].start, "heading", options);
                const auto reset =
                    mirrorfly::make_markdown_edit(source, before[0].start, before[0].start, "paragraph");
                const auto after = mirrorfly::markdown_paragraphs(apply_edit(source, edit));
                if (!check(edit.valid && reset.valid && after.size() == 1 &&
                            after[0].heading_level == level && after[0].runs.empty() &&
                            apply_edit(source, reset) == prefix + "\n",
                        "empty heading changes level or resets without placeholder and retains owner syntax"))
                    return false;
            }
        return true;
    }

    bool test_mixed_list_code()
    {
        bool passed = true;
        for (const std::string& source :
            {std::string("- first\n- parent\n\n  ```cpp\n  \tX\n  ```\n- tail\n"),
                std::string("- first\n- parent\n\n\t  X\n\t  Y\n\n- tail\n"),
                std::string("> - first\n> - parent\n>\n> \t    X\n> \t    Y\n>\n> - tail\n")})
        {
            const auto before = mirrorfly::markdown_blocks(source);
            const auto position = source.find("parent");
            const auto edit = mirrorfly::make_markdown_edit(source, position, position, "listIndent");
            const auto saved = apply_edit(source, edit);
            const auto after = mirrorfly::markdown_blocks(saved);
            const bool literal = before.size() == 1 && after.size() == 1 && before[0].kind == "code" &&
                before[0].text == after[0].text && before[0].language == after[0].language;
            if (!literal)
                std::cerr << "code move before=" << source << " after=" << saved << '\n';
            passed =
                check(edit.valid && literal && after[0].containers.size() == before[0].containers.size() + 1,
                    "source list move preserves literal tabs, indented code and ordered ownership") &&
                passed;
            if (edit.valid && literal)
            {
                const auto moved_position = saved.find("parent");
                const auto restored = apply_edit(saved,
                    mirrorfly::make_markdown_edit(saved, moved_position, moved_position, "listOutdent"));
                const auto leaves = mirrorfly::markdown_blocks(restored);
                passed = check(leaves.size() == 1 && leaves[0].text == before[0].text &&
                                 leaves[0].containers.size() == before[0].containers.size(),
                             "tabbed code source outdent restores literal content and parent depth") &&
                    passed;
            }
        }
        return passed;
    }

    bool test_thematic_breaks()
    {
        bool passed = true;
        const auto empty = mirrorfly::make_markdown_edit("", 0, 0, "thematicBreak");
        passed = check(empty.valid && mirrorfly::markdown_thematic_break_count(empty.replacement) == 1,
                     "a new empty Markdown document accepts a separator") &&
            passed;
        for (const auto& source : {std::string("before\nafter\n"), std::string("> before\r\n> after\r\n"),
                 std::string("# heading\n"), std::string("title\n=====\n"),
                 std::string("- parent\n  - child\n- sibling\n\nplain\n"), std::string("[label](../a)\n"),
                 std::string(u8"🦋前后\n")})
        {
            const auto position = source.find_first_not_of("> -#");
            const auto edit = mirrorfly::make_markdown_edit(source, position, position, "thematicBreak");
            const auto saved = apply_edit(source, edit);
            passed = check(edit.valid && mirrorfly::markdown_thematic_break_count(saved) == 1,
                         "source separator adds one parsed rule after a complete paragraph") &&
                passed;
            if (source.find("child") != std::string::npos)
                passed = check(saved.find("sibling") < saved.find("- - -") &&
                                 saved.find("plain") > saved.find("- - -"),
                             "separator follows the complete list without orphaning descendants") &&
                    passed;
            if (source.find("=====") != std::string::npos)
                passed = check(saved.find("=====") < saved.find("- - -"),
                             "separator preserves the Setext heading underline") &&
                    passed;
        }
        passed = check(mirrorfly::markdown_thematic_break_count("before\n---\n") == 0 &&
                         mirrorfly::markdown_thematic_break_count("***\n\n___\n\n- - -\n") == 3,
                     "rule metadata distinguishes Setext headings and real separators") &&
            passed;
        for (const auto& source :
            {std::string("```\nbody\n```\n"), std::string("| A |\n| --- |\n| body |\n")})
            passed = check(!mirrorfly::make_markdown_edit(
                               source, source.find("body"), source.find("body"), "thematicBreak")
                               .valid,
                         "code and table separator actions reject without changing source") &&
                passed;
        passed = check(!mirrorfly::make_markdown_edit("text\n", 0, 2, "thematicBreak").valid,
                     "separator source action requires a caret") &&
            passed;
        return passed;
    }

    bool test_hard_breaks()
    {
        bool passed = true;
        for (const auto& marked : {std::string("left|right\n"), std::string("> left|right\r\n"),
                 std::string("- [x] left|right\n"), std::string("100. left|right\n"),
                 std::string("- > left|right\n"), std::string("> - left|right\n"),
                 std::string("[**left|right**][r]\n\n[r]: ../same\n"), std::string(u8"🦋左|右\n")})
        {
            auto source = marked;
            const auto position = source.find('|');
            source.erase(position, 1);
            const auto edit = mirrorfly::make_markdown_edit(source, position, position, "hardBreak");
            const auto saved = apply_edit(source, edit);
            passed =
                check(edit.valid && mirrorfly::markdown_hard_breaks(saved).size() == 1 &&
                        edit.selection_start == edit.selection_end &&
                        saved.find(source.find("\r\n") != std::string::npos ? "  \r\n" : "  \n") !=
                            std::string::npos,
                    "public source hardBreak preserves Unicode, line endings and paragraph containers") &&
                passed;
            if (source.find("[r]:") != std::string::npos)
                passed = check(saved.find("[r]: ../same\n") != std::string::npos &&
                                 mirrorfly::markdown_links(saved).size() == 1,
                             "linked source hard breaks retain shared definitions and link identity") &&
                    passed;
        }
        const auto converted = mirrorfly::make_markdown_edit("left\nright\n", 4, 4, "hardBreak");
        for (const auto& spaces : {std::string(" "), std::string("   "), std::string("\t")})
            for (const auto& prefix :
                {std::string{}, std::string("> "), std::string("- "), std::string("- > ")})
            {
                const auto source = prefix + "left" + spaces + "right\n";
                const auto position = source.find("right");
                const auto edit = mirrorfly::make_markdown_edit(source, position, position, "hardBreak");
                const auto saved = apply_edit(source, edit);
                passed = check(edit.valid && saved.find("left" + spaces + "\\\n") != std::string::npos &&
                                 mirrorfly::markdown_hard_breaks(saved).size() == 1,
                             "hard break marker retains existing interior spaces and tabs") &&
                    passed;
            }
        passed = check(converted.valid && apply_edit("left\nright\n", converted) == "left  \nright\n",
                     "hardBreak converts an existing soft newline without adding a blank paragraph") &&
            passed;
        for (const auto& tail : {std::string("# heading"), std::string("> quote"), std::string("- item"),
                 std::string("1. item"), std::string("***"), std::string("~~~ language")})
        {
            const auto source = "before" + tail + '\n';
            const auto edit = mirrorfly::make_markdown_edit(source, 6, 6, "hardBreak");
            const auto saved = apply_edit(source, edit);
            passed = check(edit.valid && mirrorfly::markdown_hard_breaks(saved).size() == 1,
                         "new hard-break lines escape block openers without changing paragraph semantics") &&
                passed;
        }
        for (const auto& marked : {std::string("# le|ft\n"), std::string("`le|ft`\n"),
                 std::string("```\nle|ft\n```\n"), std::string("[left](../pa|th)\n"),
                 std::string("<https://exa|mple.com>\n"), std::string("| A |\n| --- |\n| bo!dy |\n"),
                 std::string("left|"), std::string("left|\n\nright\n")})
        {
            auto source = marked;
            const auto position = source.find(marked.find('!') != std::string::npos ? '!' : '|');
            source.erase(position, 1);
            passed =
                check(!mirrorfly::make_markdown_edit(source, position, position, "hardBreak").valid,
                    "hardBreak rejects code, headings, tables, destinations and terminal paragraph breaks") &&
                passed;
        }
        const auto ranges = mirrorfly::markdown_hard_breaks("> first  \r\n> second\n");
        passed = check(ranges.size() == 1 && ranges[0].start == 7 &&
                         std::string("> first  \r\n> second\n").substr(ranges[0].end).find("second") == 0,
                     "hard-break metadata gives exact source marker and continuation-prefix offsets") &&
            passed;
        const auto leading = mirrorfly::make_markdown_edit("before\n", 0, 0, "hardBreak");
        passed = check(leading.valid && apply_edit("before\n", leading) == "\\\nbefore\n" &&
                         mirrorfly::markdown_hard_breaks(apply_edit("before\n", leading)).size() == 1,
                     "a leading source break uses an explicit nonblank marker") &&
            passed;
        passed = check(!mirrorfly::make_markdown_edit("abcd", 1, 2, "hardBreak").valid,
                     "source hardBreak requires a caret rather than deleting selected text") &&
            passed;
        return passed;
    }

    bool test_unicode_and_inline()
    {
        using mirrorfly::make_markdown_edit;
        const std::string source = u8"前缀 你好🦋 尾部";
        const auto start = source.find(u8"你好");
        const auto end = source.find(u8" 尾部");
        const auto bold = make_markdown_edit(source, start, end, "bold");
        bool passed = check(bold.valid && bold.start == start && bold.end == end &&
                selected(bold) == u8"你好🦋" && apply_edit(source, bold) == u8"前缀 **你好🦋** 尾部",
            "UTF-8 byte selections preserve Chinese, emoji, and all surrounding source");
        const auto reversed = make_markdown_edit(source, end, start, "italic");
        passed = check(reversed.valid && apply_edit(source, reversed) == u8"前缀 *你好🦋* 尾部",
                     "reverse selections produce the same normalized range") &&
            passed;
        const auto placeholder = make_markdown_edit("", 0, 0, "bold");
        passed = check(placeholder.valid && selected(placeholder) == u8"粗体文字" &&
                         placeholder.replacement == u8"**粗体文字**",
                     "empty inline selections create an editable placeholder") &&
            passed;
        const auto strike = make_markdown_edit(source, start, end, "strike");
        passed = check(strike.valid && selected(strike) == u8"你好🦋" &&
                         apply_edit(source, strike) == u8"前缀 ~~你好🦋~~ 尾部",
                     "strikethrough preserves UTF-8 selection and surrounding source") &&
            passed;
        const auto link = make_markdown_edit("a[b]\\c", 0, 6, "link");
        passed = check(link.valid && link.replacement == "[a\\[b\\]\\\\c](https://example.com)",
                     "link labels escape brackets and backslashes without affecting other source") &&
            passed;
        for (const std::string value : {"  ", "\t middle\t", "    lead  ", u8"　空  "})
            for (const std::string action : {"bold", "italic", "strike"})
            {
                const std::string whitespace_source = u8"> - 🦋L" + value + "R\n> - tail\n";
                const auto offset = whitespace_source.find(value);
                const auto changed =
                    make_markdown_edit(whitespace_source, offset, offset + value.size(), action);
                const auto paragraphs =
                    mirrorfly::markdown_paragraphs(apply_edit(whitespace_source, changed));
                std::string styled_text;
                if (paragraphs.size() == 2)
                    for (const auto& run : paragraphs[0].runs)
                        if ((action == "bold" && run.style.bold) ||
                            (action == "italic" && run.style.italic) ||
                            (action == "strike" && run.style.strike))
                            styled_text += run.style.text;
                passed =
                    check(changed.valid && paragraphs.size() == 2 && paragraphs[0].containers.size() == 2 &&
                            styled_text == value,
                        "public source emphasis protects whitespace without trimming or changing parents") &&
                    passed;
            }
        return passed;
    }

    bool test_inline_code()
    {
        using mirrorfly::markdown_code_span;
        bool passed = check(markdown_code_span("a`b``c") == "```a`b``c```" &&
                markdown_code_span("`x`") == "`` `x` ``" && markdown_code_span(" x ") == "`  x  `" &&
                markdown_code_span("   ") == "`   `",
            "CommonMark delimiters protect embedded backticks and boundary spaces");
        passed = check(markdown_code_span("a\r\nb\nc") == "`a b c`" &&
                         markdown_code_span(std::string("\xC0\xAF", 2)).empty(),
                     "code spans normalize line endings and reject invalid Unicode") &&
            passed;
        const std::string original = u8"保留 🦋`x` 后面";
        const auto start = original.find(u8"🦋");
        const auto end = original.find(u8" 后面");
        const auto edit = mirrorfly::make_markdown_edit(original, end, start, "inlineCode");
        passed = check(edit.valid && selected(edit) == u8"🦋`x`" &&
                         apply_edit(original, edit) == u8"保留 `` 🦋`x` `` 后面",
                     "source inline code preserves reverse UTF-8 selections and surroundings") &&
            passed;
        const auto empty = mirrorfly::make_markdown_edit("", 0, 0, "inlineCode");
        passed =
            check(empty.valid && selected(empty) == u8"代码" &&
                    mirrorfly::make_markdown_edit("a\nb", 0, 3, "inlineCode").valid,
                "empty insertion selects its placeholder and soft multiline stays inside one code span") &&
            passed;
        for (const std::string source :
            {"> - a\r\n>   b after\r\n> - tail\r\n", "- > a\n  > b after\n- tail\n", "a\nb after\n"})
        {
            const auto first = source.find('a');
            const auto last = source.find(" after");
            const auto created = mirrorfly::make_markdown_edit(source, first, last, "inlineCode");
            const auto spans = mirrorfly::markdown_code_spans(apply_edit(source, created));
            passed = check(created.valid && spans.size() == 1 && spans[0].text == "a b",
                         "source soft multiline code excludes quote and list continuation prefixes") &&
                passed;
            if (spans.size() == 1)
            {
                const auto coded = apply_edit(source, created);
                const auto removed = mirrorfly::make_markdown_edit(
                    coded, spans[0].start + 1, spans[0].start + 1, "removeInlineCode");
                const auto plain = apply_edit(coded, removed);
                passed =
                    check(removed.valid && removed.start == spans[0].start && removed.end == spans[0].end &&
                            mirrorfly::markdown_code_spans(plain).empty() &&
                            plain.find("a b after") != std::string::npos,
                        "source caret removes the complete span as literal text") &&
                    passed;
            }
        }
        passed = check(!mirrorfly::make_markdown_edit("a\n\nb", 0, 4, "inlineCode").valid &&
                         !mirrorfly::make_markdown_edit("```\nbody\n```", 4, 8, "inlineCode").valid &&
                         !mirrorfly::make_markdown_edit("[a](target)", 4, 10, "inlineCode").valid &&
                         !mirrorfly::make_markdown_edit("plain", 0, 5, "removeInlineCode").valid &&
                         mirrorfly::markdown_code_spans("![`caption`](image)").empty(),
                     "code commands respect paragraph, fenced code, link target and image-alt boundaries") &&
            passed;
        return passed;
    }

    bool test_line_commands()
    {
        using mirrorfly::make_markdown_edit;
        mirrorfly::MarkdownOptions heading;
        heading.heading_level = 3;
        const std::string title = u8"# 原题\n尾部";
        const auto changed_heading = make_markdown_edit(title, 2, 2, "heading", heading);
        bool passed = check(changed_heading.valid && apply_edit(title, changed_heading) == u8"### 原题\n尾部",
            "heading changes replace the existing marker on the current line");
        const std::string source = u8"before\n甲\r\n乙\r\nafter";
        const auto start = source.find(u8"甲");
        const auto end = source.find("after");
        const auto ordered = make_markdown_edit(source, start, end, "ordered");
        passed = check(ordered.valid && apply_edit(source, ordered) == u8"before\n1. 甲\r\n2. 乙\r\nafter",
                     "line commands preserve CRLF and do not include the following unselected line") &&
            passed;
        passed = check(make_markdown_edit("item", 2, 2, "bullet").replacement == "- item" &&
                         make_markdown_edit("item", 0, 4, "task").replacement == "- [ ] item" &&
                         make_markdown_edit("a\nb", 0, 3, "quote").replacement == "> a\n> b",
                     "bullet, task, and quote commands operate on affected lines") &&
            passed;
        const auto empty = make_markdown_edit("", 0, 0, "heading");
        passed =
            check(selected(empty) == u8"标题", "an empty heading selects only its placeholder") && passed;
        return passed;
    }

    bool test_heading_paragraphs()
    {
        bool passed = true;
        for (int level = 1; level <= 6; ++level)
        {
            mirrorfly::MarkdownOptions options;
            options.heading_level = level;
            const std::string source = u8"> - **前🦋** [link](../x)\r\n>   title\r\n>   ===\r\n> - tail\r\n";
            const auto start = source.find(u8"前");
            const auto edit = mirrorfly::make_markdown_edit(source, start, start, "heading", options);
            const auto result = apply_edit(source, edit);
            const auto paragraphs = mirrorfly::markdown_paragraphs(result);
            passed =
                check(edit.valid && result.find("===") == std::string::npos &&
                        result.find("\r\n> - tail\r\n") != std::string::npos && paragraphs.size() == 2 &&
                        paragraphs[0].heading_level == level && paragraphs[0].containers.size() == 2 &&
                        paragraphs[0].containers[0].kind == "quote" &&
                        paragraphs[0].containers[1].kind == "listItem",
                    "six source heading levels consume the complete Setext block and retain CRLF/parents") &&
                passed;
            const auto reset = mirrorfly::make_markdown_edit(result, 0, 0, "paragraph");
            const auto plain = mirrorfly::markdown_paragraphs(apply_edit(result, reset));
            passed = check(reset.valid && plain.size() == 2 && !plain[0].heading &&
                             plain[0].heading_level == 0 && plain[0].containers.size() == 2,
                         "source paragraph resets only heading style and preserves the sibling") &&
                passed;
        }
        const std::string literal = "## # literal\n";
        const auto reset = mirrorfly::make_markdown_edit(literal, 3, 3, "paragraph");
        const auto plain = mirrorfly::markdown_paragraphs(apply_edit(literal, reset));
        std::string literal_text;
        if (plain.size() == 1)
            for (const auto& run : plain[0].runs)
                literal_text += run.style.text;
        passed = check(reset.valid && plain.size() == 1 && !plain[0].heading && literal_text == "# literal",
                     "restoring paragraph escapes heading-looking literal content") &&
            passed;
        for (const std::string& source : {std::string("a\\\nb\n"), std::string("```cpp\nx\n```\n"),
                 std::string("| A |\n| --- |\n| x |\n")})
        {
            const auto position = source.find(source.front() == 'a' ? "a" : "x");
            passed = check(!mirrorfly::make_markdown_edit(source, position, position, "heading").valid,
                         "code, table and hard-break source paragraphs retain their structural boundaries") &&
                passed;
        }
        for (int level : {0, 7, -1})
        {
            mirrorfly::MarkdownOptions options;
            options.heading_level = level;
            passed = check(!mirrorfly::make_markdown_edit("text\n", 0, 0, "heading", options).valid,
                         "core heading rejects invalid level without clamping") &&
                passed;
        }
        const std::string unchanged = "> - text\n>   continuation\n> - tail\n";
        const auto idempotent = mirrorfly::make_markdown_edit(unchanged, 4, 4, "paragraph");
        passed = check(idempotent.valid && apply_edit(unchanged, idempotent) == unchanged &&
                         !mirrorfly::make_markdown_edit("```cpp\nx\n", 9, 9, "heading").valid,
                     "plain paragraph is idempotent and EOF inside an open fence cannot create a heading") &&
            passed;
        return passed;
    }

    bool test_code_fences()
    {
        using mirrorfly::make_markdown_edit;
        mirrorfly::MarkdownOptions options;
        options.language = "cpp\n```injected";
        const std::string body = "```\nbody\n````";
        const std::string source = "before " + body + " after";
        const auto code = make_markdown_edit(source, 7, 7 + body.size(), "code", options);
        bool passed = check(code.valid && selected(code) == body &&
                code.replacement == "\n`````cpp\n" + body + "\n`````\n" &&
                apply_edit(source, code).substr(0, 7) == "before " &&
                apply_edit(source, code).substr(apply_edit(source, code).size() - 6) == " after",
            "fences exceed embedded backtick runs, filter language newlines, and preserve selected code");
        const auto empty = make_markdown_edit("", 0, 0, "code");
        passed = check(empty.valid && selected(empty) == u8"代码内容" &&
                         empty.replacement == u8"```\n代码内容\n```\n",
                     "empty code insertion selects the body inside a complete fence") &&
            passed;
        return passed;
    }

    bool test_table_alignment()
    {
        const std::string source = u8"前 🦋\r\n\r\n| 名称 | 数量 | 备注 |\r\n| :---- | ----: | :--: |\r\n| "
                                   u8"甲\\|乙 | 2 | 好 |\r\n\r\n尾";
        const auto position = source.find(u8"甲");
        mirrorfly::MarkdownOptions options;
        options.table_column = 1;
        options.table_alignment = "center";
        const auto edit = mirrorfly::make_markdown_edit(source, position, position, "tableAlign", options);
        auto expected = source;
        const auto delimiter = expected.find("----:");
        expected.replace(delimiter, 5, ":----:");
        const auto metadata = mirrorfly::markdown_tables(source);
        bool passed = check(edit.valid && apply_edit(source, edit) == expected && metadata.size() == 1 &&
                metadata[0].rows == 2 && metadata[0].columns.size() == 3 &&
                metadata[0].columns[0].alignment == "left" && metadata[0].columns[1].alignment == "right" &&
                metadata[0].columns[2].alignment == "center",
            "source column alignment preserves Unicode, escaped pipes, whitespace and CRLF");
        for (const auto& alignment : {"default", "left", "center", "right"})
        {
            options.table_column = 2;
            options.table_alignment = alignment;
            const auto change =
                mirrorfly::make_markdown_edit(source, position, position, "tableAlign", options);
            const auto tables = mirrorfly::markdown_tables(apply_edit(source, change));
            passed =
                check(change.valid && tables.size() == 1 && tables[0].columns[2].alignment == alignment &&
                        tables[0].columns[0].alignment == "left",
                    "all four alignments set only the requested column") &&
                passed;
        }
        options.table_column = 0;
        options.table_alignment = "right";
        for (const auto& prefix : {std::string("> "), std::string("  ")})
        {
            const auto nested = prefix + "| A | B |\n" + prefix + "| --- | --- |\n" + prefix + "| a | b |\n";
            const auto start = nested.find("| a");
            const auto change = mirrorfly::make_markdown_edit(nested, start, start, "tableAlign", options);
            passed = check(change.valid &&
                             apply_edit(nested, change).find(prefix + "| ---: | --- |") != std::string::npos,
                         "quoted and indented table source retains its container") &&
                passed;
        }
        const std::string inner = "| A |\n| --- |\n| value |\n";
        const std::string fence(3, '\x60');
        const auto literal = fence + "\n" + inner + fence + "\n";
        passed = check(mirrorfly::markdown_tables(literal).empty() &&
                         !mirrorfly::make_markdown_edit(
                             literal, literal.find("value"), literal.find("value"), "tableAlign", options)
                             .valid &&
                         mirrorfly::markdown_tables("    | A |\n    | --- |\n    | value |\n").empty(),
                     "fenced and indented literal code cannot be treated as editable tables") &&
            passed;
        options.table_column = 32;
        passed =
            check(!mirrorfly::make_markdown_edit(source, position, position, "tableAlign", options).valid,
                "out-of-range columns reject instead of clamping") &&
            passed;
        options.table_column = 0;
        options.table_alignment = "justify";
        passed =
            check(!mirrorfly::make_markdown_edit(source, position, position, "tableAlign", options).valid &&
                    mirrorfly::markdown_tables("| A | B |\n| --- |\n| value |\n").empty() &&
                    mirrorfly::markdown_tables("# A | B\n| --- | --- |\n").empty(),
                "unknown alignments and malformed or heading tables reject") &&
            passed;
        return passed;
    }

    bool test_list_hierarchy_and_tasks()
    {
        using mirrorfly::make_markdown_edit;
        const std::string source = u8"- 父🦋\r\n- [X] 第二 **保留**\r\n  - 子项\r\n- 末项\r\n";
        const auto second = source.find(u8"第二");
        const auto indented = make_markdown_edit(source, second, second, "listIndent");
        const std::string expected = u8"- 父🦋\r\n  - [X] 第二 **保留**\r\n    - 子项\r\n- 末项\r\n";
        bool passed = check(indented.valid && apply_edit(source, indented) == expected,
            "source indentation moves the selected parent and descendants, preserving CRLF and siblings");
        const auto restored =
            make_markdown_edit(expected, expected.find(u8"第二"), expected.find(u8"第二"), "listOutdent");
        passed = check(restored.valid && apply_edit(expected, restored) == source,
                     "source outdent restores nested descendants without touching literal content") &&
            passed;
        const auto converted = make_markdown_edit(source, 0, source.size(), "task");
        passed = check(converted.valid &&
                         apply_edit(source, converted) ==
                             u8"- [ ] 父🦋\r\n- [x] 第二 **保留**\r\n  - [ ] 子项\r\n- [ ] 末项\r\n",
                     "list style conversion replaces existing markers and preserves hierarchy and checked "
                     "state") &&
            passed;
        mirrorfly::MarkdownOptions options;
        options.task_checked = false;
        const auto unchecked = make_markdown_edit(source, second, second, "taskSet", options);
        passed = check(unchecked.valid && apply_edit(source, unchecked).find("- [ ] ") != std::string::npos &&
                         !make_markdown_edit(source, 0, 0, "taskSet", options).valid &&
                         !make_markdown_edit(source, 0, 0, "listIndent").valid &&
                         !make_markdown_edit(source, second, second, "listOutdent").valid,
                     "explicit task state and list hierarchy reject invalid item operations") &&
            passed;
        const std::string quoted = "> 10. parent\n> 11. child\n>     - grandchild\n";
        const auto quote_edit =
            make_markdown_edit(quoted, quoted.find("child"), quoted.find("child"), "listIndent");
        passed = check(quote_edit.valid &&
                         apply_edit(quoted, quote_edit) ==
                             "> 10. parent\n>     11. child\n>         - grandchild\n",
                     "ordered marker width controls quoted list nesting, preserving quote prefixes") &&
            passed;
        const std::string fence(3, '\x60');
        const auto literal = fence + "\n> - [x] literal\n- [ ] literal\n" + fence + "\n";
        passed =
            check(!make_markdown_edit(
                      literal, literal.find("literal"), literal.find("literal"), "taskSet", options)
                        .valid &&
                    !make_markdown_edit("    - [ ] literal\n", 8, 8, "taskSet", options).valid &&
                    !make_markdown_edit("* * *\n", 0, 0, "listIndent").valid &&
                    !make_markdown_edit("- one\n\nplain\n\n- two\n", 19, 19, "listIndent").valid,
                "literal code, thematic breaks and separate lists cannot masquerade as editable siblings") &&
            passed;
        const std::string body =
            "- one\n- [x] two\n  continuation\n\n  " + fence + "\n  - [ ] literal\n  " + fence + "\n";
        const auto style = make_markdown_edit(body, body.find("two"), body.size(), "task");
        passed =
            check(style.valid && apply_edit(body, style) == body,
                "converting an existing task retains continuation paragraphs and fenced literal markers") &&
            passed;
        const std::string spaced = "- parent\n    - child\n      - grandchild\n";
        const auto wide =
            make_markdown_edit(spaced, spaced.find("child"), spaced.find("child"), "listOutdent");
        passed = check(wide.valid && apply_edit(spaced, wide) == "- parent\n- child\n  - grandchild\n",
                     "outdent changes semantic level even when the source uses wider optional padding") &&
            passed;
        const std::string bullets = "- first\n  - child\n- second\n";
        const auto numbered = make_markdown_edit(bullets, 0, bullets.size(), "ordered");
        passed =
            check(numbered.valid && apply_edit(bullets, numbered) == "1. first\n   1. child\n2. second\n",
                "numbered style counts each sibling group and adjusts marker-width nesting") &&
            passed;
        const auto single = make_markdown_edit(bullets, 0, 0, "ordered");
        passed = check(single.valid && apply_edit(bullets, single) == "1. first\n   - child\n- second\n",
                     "changing one parent marker preserves the unselected child's hierarchy and style") &&
            passed;
        std::string maximum = "- root\n  - preceding\n  - moving\n";
        for (int depth = 3; depth <= 8; ++depth)
            maximum += std::string(static_cast<std::size_t>((depth - 1) * 2), ' ') + "- child\n";
        const auto moving = maximum.find("moving");
        passed = check(!make_markdown_edit(maximum, moving, moving, "listIndent").valid,
                     "descendants cannot be pushed beyond the declared maximum depth") &&
            passed;
        const auto indented_fence = fence + "\n    " + fence + "\n- [ ] literal\n" + fence + "\n";
        passed = check(!make_markdown_edit(indented_fence, indented_fence.find("literal"),
                           indented_fence.find("literal"), "taskSet", options)
                           .valid,
                     "an over-indented closing fence does not expose literal task markers") &&
            passed;
        const auto list_fence = "- " + fence + "\n  literal\n- [ ] outside\n";
        const auto outside = list_fence.find("outside");
        passed = check(make_markdown_edit(list_fence, outside, outside, "taskSet", options).valid,
                     "a fenced list block ends when the next item leaves its container") &&
            passed;
        const std::string tasks = "- [ ] first\n  - [x] nested\n- [ ] last\n";
        options.task_checked = true;
        const auto completed = make_markdown_edit(tasks, 0, tasks.size(), "taskSet", options);
        const auto checked = apply_edit(tasks, completed);
        const auto repeated = make_markdown_edit(checked, 0, checked.size(), "taskSet", options);
        passed = check(completed.valid && repeated.valid && apply_edit(checked, repeated) == checked &&
                         checked == "- [x] first\n  - [x] nested\n- [x] last\n",
                     "explicit multilevel task completion is idempotent") &&
            passed;
        return passed;
    }

    bool test_quote_containers()
    {
        using mirrorfly::make_markdown_edit;
        mirrorfly::MarkdownOptions options;
        options.quote_level = 2;
        const std::string nested = "> **parent**\r\n> > child🦋\r\nplain\r\n";
        const auto deeper = make_markdown_edit(nested, 3, 3, "quoteSet", options);
        bool passed = check(
            deeper.valid && apply_edit(nested, deeper) == "> > **parent**\r\n> > > child🦋\r\nplain\r\n",
            "quote depth carries nested children while preserving emphasis, Unicode and CRLF");
        const auto changed = apply_edit(nested, deeper);
        const auto again = make_markdown_edit(changed, 4, 4, "quoteSet", options);
        passed = check(again.valid && apply_edit(changed, again) == changed,
                     "explicit quote depth is idempotent") &&
            passed;
        const std::string table =
            "> - parent\n>\n>   | A | B |\n>   | --- | :---: |\n>   | a | b |\n>\n> - tail\n";
        const auto metadata = mirrorfly::markdown_tables(table);
        passed = check(metadata.size() == 1 && metadata[0].quote_level == 1 && metadata[0].list_indent == 2 &&
                         metadata[0].list_quote_level == 1,
                     "public table metadata identifies its quote and list containers") &&
            passed;
        const auto position = table.find("| a");
        const auto table_edit = make_markdown_edit(table, position, position, "quoteSet", options);
        const auto table_changed = apply_edit(table, table_edit);
        passed = check(table_edit.valid && table_changed.find(">   > | A | B |") != std::string::npos &&
                         table_changed.find(">   > | a | b |") != std::string::npos &&
                         table_changed.find("> - parent") == 0,
                     "a table caret changes the entire table without changing its list parent") &&
            passed;
        const auto inner = mirrorfly::markdown_tables(table_changed);
        passed = check(inner.size() == 1 && inner[0].quote_level == 2 && inner[0].list_indent == 2 &&
                         inner[0].list_quote_level == 1,
                     "quotes inside list tables retain their container order") &&
            passed;
        options.quote_level = 0;
        passed = check(!make_markdown_edit(table, position, position, "quoteSet", options).valid,
                     "a nested table cannot remove its parent's quote") &&
            passed;
        options.quote_level = 2;
        const auto parent = make_markdown_edit(table, 4, 4, "quoteSet", options);
        passed = check(parent.valid && apply_edit(table, parent).find("> > - parent") == 0 &&
                         apply_edit(table, parent).find("> >   | A | B |") != std::string::npos &&
                         apply_edit(table, parent).find("> - tail") != std::string::npos,
                     "parent quote changes move owned table descendants and preserve sibling containers") &&
            passed;
        const std::string fence(3, '\x60');
        const auto code = "> " + fence + "cpp\n> > literal\n> " + fence + "\nplain\n";
        const auto code_edit =
            make_markdown_edit(code, code.find("literal"), code.find("literal"), "quoteSet", options);
        passed = check(code_edit.valid &&
                         apply_edit(code, code_edit) ==
                             "> > " + fence + "cpp\n> > > literal\n> > " + fence + "\nplain\n",
                     "code quote edits include both fences and preserve literal greater-than characters") &&
            passed;
        options.quote_level = 8;
        passed = check(!make_markdown_edit(nested, 3, 3, "quoteSet", options).valid,
                     "nested quote depth limits reject the whole transaction") &&
            passed;
        options.quote_level = -1;
        passed = check(!make_markdown_edit("plain", 0, 0, "quoteSet", options).valid,
                     "negative quote depths are rejected") &&
            passed;
        const auto legacy = make_markdown_edit("> parent\n", 2, 2, "quote");
        passed = check(legacy.valid && apply_edit("> parent\n", legacy) == "> parent\n",
                     "legacy quote sets depth one rather than stacking duplicate prefixes") &&
            passed;
        options.quote_level = 0;
        const auto removed = make_markdown_edit("> > parent\n", 4, 4, "quoteSet", options);
        passed = check(removed.valid && apply_edit("> > parent\n", removed) == "parent\n",
                     "depth zero removes the selected quote container") &&
            passed;
        const std::string inline_table = "> - | A |\n>   | --- |\n>   | a |\n";
        const auto inline_edit = make_markdown_edit(
            inline_table, inline_table.find("| a"), inline_table.find("| a"), "quoteSet", options);
        passed = check(inline_edit.valid &&
                         apply_edit(inline_table, inline_edit) == "- | A |\n  | --- |\n  | a |\n",
                     "a table that is the list item's entire body can remove its outer quote") &&
            passed;
        const std::string empty_owner = "- \n\n  > | A |\n  > | --- |\n  > | a |\n";
        const auto owner_edit = make_markdown_edit(
            empty_owner, empty_owner.find("| a"), empty_owner.find("| a"), "quoteSet", options);
        const auto unquoted = apply_edit(empty_owner, owner_edit);
        const auto unquoted_tables = mirrorfly::markdown_tables(unquoted);
        passed =
            check(owner_edit.valid && unquoted_tables.size() == 1 && unquoted_tables[0].quote_level == 0 &&
                    unquoted_tables[0].list_indent == 2 && unquoted.find("- ") == 0,
                "removing an inner quote from an empty list owner preserves the list container") &&
            passed;
        return passed;
    }

    bool test_links()
    {
        using mirrorfly::make_markdown_edit;
        mirrorfly::MarkdownOptions options;
        options.url = u8"../路径 (草稿)/x?one=1&two=2";
        options.title = u8"查看 \"草稿\" & 说明";
        const std::string source = u8"前 你好🦋 后\r\n";
        const auto start = source.find(u8"你好");
        const auto end = source.find(u8" 后");
        const auto edit = make_markdown_edit(source, start, end, "link", options);
        const auto changed = apply_edit(source, edit);
        const auto links = mirrorfly::markdown_links(changed);
        bool passed = check(edit.valid && selected(edit) == u8"你好🦋" && links.size() == 1 &&
                links.front().url == options.url && links.front().title == options.title &&
                changed.substr(changed.size() - 2) == "\r\n",
            "link creation preserves Unicode, CRLF and escaped destinations/titles through public metadata");
        const std::string rich = u8"前 [**你好🦋** and "
                                 "\x60"
                                 "a[b]"
                                 "\x60"
                                 "](old 'tooltip') 后";
        const auto position = rich.find("a[b]");
        const auto update = make_markdown_edit(rich, position, position, "link", options);
        const auto updated = apply_edit(rich, update);
        const auto metadata = mirrorfly::markdown_links(updated);
        passed = check(update.valid && metadata.size() == 1 &&
                         updated.substr(
                             metadata[0].label_start, metadata[0].label_end - metadata[0].label_start) ==
                             u8"**你好🦋** and "
                             "\x60"
                             "a[b]"
                             "\x60" &&
                         metadata[0].url == options.url && metadata[0].title == options.title &&
                         apply_edit(updated,
                             make_markdown_edit(updated, position, position, "link", options)) == updated,
                     "a caret within formatted or code link text updates the whole link idempotently") &&
            passed;
        const auto remove = make_markdown_edit(updated, position, position + 1, "unlink");
        passed = check(remove.valid &&
                         apply_edit(updated, remove) ==
                             u8"前 **你好🦋** and "
                             "\x60"
                             "a[b]"
                             "\x60"
                             " 后",
                     "partial unlink keeps all label markup and surrounding text") &&
            passed;
        const std::string literal = "\x60"
                                    "a[b]"
                                    "\x60";
        const auto code_link =
            apply_edit(literal, make_markdown_edit(literal, 0, literal.size(), "link", options));
        passed =
            check(code_link.find("[" + literal + "]") == 0 &&
                    !make_markdown_edit(literal, 2, 3, "link", options).valid &&
                    !make_markdown_edit("a\nb", 0, 3, "link", options).valid &&
                    !make_markdown_edit("\x60\x60\x60txt\n[a](x)\n\x60\x60\x60\n", 8, 8, "link", options)
                        .valid &&
                    !make_markdown_edit("[a](x) b", 1, 8, "link", options).valid &&
                    !make_markdown_edit("plain", 0, 2, "unlink").valid,
                "whole inline code can be a label while code-literal, multiline and overlap edits reject") &&
            passed;
        for (const auto& url :
            {std::string{}, std::string("a\nb"), std::string(4097, 'x'), std::string("\xC0\xAF", 2)})
        {
            options.url = url;
            passed = check(!make_markdown_edit("a", 0, 1, "link", options).valid,
                         "invalid, empty or oversized link destinations reject without editing") &&
                passed;
        }
        passed =
            check(mirrorfly::markdown_links("[a](x(y(z))) [b](<../a b> \"title\") ![image](p.png) "
                                            "\x60"
                                            "[literal](x)"
                                            "\x60"
                                            "\n\n    [code](x)\n")
                            .size() == 2 &&
                    mirrorfly::markdown_links(std::string(100000, '[') + "](x)").size() == 1,
                "metadata handles balanced destinations and excludes images and code with bounded nesting") &&
            passed;
        const auto nested = mirrorfly::markdown_links("[outer [inner](b)](a)");
        passed = check(nested.size() == 1 && nested.front().url == "b" && nested.front().start == 7,
                     "actual nested links deactivate the outer link while keeping the inner link editable") &&
            passed;
        options.url = "#page";
        const std::string image = "![alt](pic.png \"image title\")";
        passed = check(apply_edit(image, make_markdown_edit(image, 0, image.size(), "link", options))
                             .find("[" + image + "]") == 0,
                     "linking a complete source image retains its image destination and title") &&
            passed;
        return passed;
    }

    bool test_resolved_links()
    {
        using mirrorfly::make_markdown_edit;
        using mirrorfly::markdown_links;
        mirrorfly::MarkdownOptions options;
        options.url = "../changed";
        options.title = "updated";
        const std::string references =
            u8"前 [**你好🦋**][r] [R][] [r]\r\n\r\n[r]: ../路径 \"&ouml; &#x1F98B; &NotEqualTilde;\"\r\n";
        const auto links = markdown_links(references);
        bool passed = check(links.size() == 3 && links[0].kind == "reference" &&
                links[0].text == u8"你好🦋" && links[0].url == u8"../路径" && links[0].title == u8"ö 🦋 ≂̸" &&
                links[1].url == links[0].url && links[2].end == references.find("\r\n"),
            "reference, collapsed and shortcut links resolve definitions and full named/numeric entities");
        const auto position = references.find(u8"你好");
        const auto updated =
            apply_edit(references, make_markdown_edit(references, position, position, "link", options));
        const auto changed = markdown_links(updated);
        passed = check(changed.size() == 3 && changed[0].kind == "inline" && changed[0].url == options.url &&
                         changed[1].url == u8"../路径" &&
                         updated.substr(updated.find("\r\n\r\n")) ==
                             references.substr(references.find("\r\n\r\n")) &&
                         updated.find(u8"[**你好🦋**](../changed \"updated\")") != std::string::npos,
                     "editing a reference occurrence preserves its markup, CRLF, shared definition and other "
                     "uses") &&
            passed;
        const auto removed =
            apply_edit(references, make_markdown_edit(references, position, position, "unlink"));
        passed = check(markdown_links(removed).size() == 2 && removed.find(u8"前 **你好🦋** [R][]") == 0,
                     "unlinking a reference occurrence preserves emphasis and other references") &&
            passed;
        const std::string multiline = "> [first\r\n> **second**][r]\r\n\r\n[r]: ../x\r\n";
        const auto ranges = markdown_links(multiline);
        const auto multi_edit = make_markdown_edit(
            multiline, multiline.find("first"), multiline.find("second") + 6, "link", options);
        const auto multi_updated = apply_edit(multiline, multi_edit);
        passed =
            check(ranges.size() == 1 && ranges[0].start == 2 && ranges[0].text == "first second" &&
                    multi_edit.valid && multi_updated.find("> [first\r\n> **second**](../changed") == 0 &&
                    markdown_links(multi_updated).size() == 1,
                "existing multiline labels retain source offsets and quote prefixes when updated") &&
            passed;
        const std::string automatic =
            "<https://example.com/a> <a@example.com> https://example.com/x www.example.com\n";
        const auto auto_links = markdown_links(automatic);
        passed = check(auto_links.size() == 4 && auto_links[0].kind == "autolink" &&
                         auto_links[1].url == "mailto:a@example.com" && auto_links[2].kind == "automatic" &&
                         auto_links[3].url == "http://www.example.com",
                     "angle URL/email and GFM bare URL/www forms expose their actual targets") &&
            passed;
        for (const auto& link : auto_links)
        {
            const auto replaced = apply_edit(
                automatic, make_markdown_edit(automatic, link.start + 1, link.start + 1, "link", options));
            const auto removed_auto = apply_edit(
                automatic, make_markdown_edit(automatic, link.start + 1, link.start + 1, "unlink"));
            const auto replaced_links = markdown_links(replaced);
            const auto removed_links = markdown_links(removed_auto);
            passed = check(replaced_links.size() == 4 && removed_links.size() == 3 &&
                             std::any_of(replaced_links.begin(), replaced_links.end(),
                                 [&](const auto& value)
            {
                return value.url == options.url && value.text == link.text;
            }),
                         "automatic link updates preserve literal captions, and unlink does not recreate the "
                         "link") &&
                passed;
        }
        const auto encoded =
            markdown_links("[a](../x?z=&copy;&unknown;&#65;&#x1F98B; \"&NotEqualTilde;&#0;&#xD800;\")");
        passed = check(encoded.size() == 1 && encoded[0].url == u8"../x?z=©&unknown;A🦋" &&
                         encoded[0].title == u8"≂̸��",
                     "full entities decode once while unknown names remain literal and invalid codepoints "
                     "replace") &&
            passed;
        const auto unicode = markdown_links(u8"[text][Ä]\n\n[ä]: ../fold\n");
        passed = check(unicode.size() == 1 && unicode[0].url == "../fold" &&
                         markdown_links("[missing][x] ![image][x]\n\n[x]: pic.png\n").size() == 1 &&
                         markdown_links("[missing][undefined]\n").empty() &&
                         markdown_links("```md\n[a][r]\n```\n\n    [b][r]\n\n[r]: ../x\n").empty(),
                     "Unicode reference folding, unresolved labels, image and literal block exclusions "
                     "follow the parser") &&
            passed;
        const std::string image = "![alt][pic]\n\n[pic]: image.png \"caption\"\n";
        const auto wrapped = apply_edit(image, make_markdown_edit(image, 0, 11, "link", options));
        passed = check(wrapped.find("[![alt][pic]](../changed") == 0 && markdown_links(wrapped).size() == 1,
                     "link creation around a whole reference image preserves its shared image definition") &&
            passed;
        return passed;
    }

    bool test_tables_and_bounds()
    {
        using mirrorfly::make_markdown_edit;
        mirrorfly::MarkdownOptions options;
        options.table_columns = 1000;
        options.table_rows = 1000;
        const std::string source = u8"保留原段|落";
        const auto table = make_markdown_edit(source, 0, source.size(), "table", options);
        const auto lines = std::count(table.replacement.begin(), table.replacement.end(), '\n');
        bool passed = check(table.valid && table.replacement.find(source + "\n\n") == 0 &&
                selected(table) == u8"列1" && lines == 24 && table.replacement.size() < 4096,
            "table insertion retains selected prose and clamps to twelve columns and twenty body rows");
        passed =
            check(!make_markdown_edit(u8"中", 1, 3, "bold").valid &&
                    !make_markdown_edit("abc", 0, 9, "bold").valid &&
                    !make_markdown_edit(std::string("\xC0\xAF", 2), 0, 2, "bold").valid &&
                    !make_markdown_edit("abc", 0, 3, "unknown").valid,
                "invalid Unicode, split UTF-8 offsets, invalid ranges, and unknown actions are rejected") &&
            passed;
        const std::string maximum(mirrorfly::maximum_text_bytes, 'x');
        passed = check(!make_markdown_edit(maximum, 0, 1, "bold").valid,
                     "formatting cannot grow source beyond the editor's size limit") &&
            passed;
        return passed;
    }

}

namespace
{
    bool test_images()
    {
        using namespace mirrorfly;
        const std::string source =
            u8"🦋 [![a *b* ![c](nested)](pic.png \"title\")](page)\n\n![ref][p]\n\n[p]: pic.png\n";
        const auto images = markdown_images(source);
        bool passed = check(images.size() == 2 && images[0].alt == "a b c" && images[0].url == "pic.png" &&
                images[1].url == "pic.png",
            "public image metadata resolves captions and references");
        if (images.size() != 2)
            return false;
        MarkdownOptions options;
        options.url = "../new.png";
        const auto edit =
            make_markdown_edit(source, images[1].start + 3, images[1].start + 3, "image", options);
        const auto updated = apply_edit(source, edit);
        passed =
            check(edit.valid && updated.find("[p]: pic.png") != std::string::npos &&
                    markdown_images(updated)[0].url == "pic.png" && markdown_images(updated)[1].alt == "ref",
                "image occurrence updates preserve shared definitions and other occurrences") &&
            passed;
        const auto removed =
            apply_edit(source, make_markdown_edit(source, images[0].start, images[0].end, "removeImage"));
        passed = check(markdown_images(removed).size() == 1 && markdown_links(removed)[0].url == "page",
                     "removing image retains its outer hyperlink and alternative text") &&
            passed;
        passed = check(!make_markdown_edit("`inside`", 2, 2, "image", options).valid &&
                         !make_markdown_edit("```\ninside\n```", 5, 5, "image", options).valid &&
                         !make_markdown_edit("[a](target)", 6, 6, "image", options).valid,
                     "image insertion refuses code literals and link destinations") &&
            passed;
        options.image_alt_set = true;
        options.image_alt = "a\nb";
        passed = check(!make_markdown_edit("body", 0, 0, "image", options).valid &&
                         markdown_image_source("", "") == "![]()",
                     "invalid captions reject atomically and empty images are valid") &&
            passed;
        return passed;
    }
}

int run_markdown_tests()
{
    bool passed = test_unicode_and_inline();
    passed = test_expected_text() && passed;
    passed = test_raw_html() && passed;
    passed = test_empty_headings() && passed;
    passed = test_line_commands() && passed;
    passed = test_heading_paragraphs() && passed;
    passed = test_inline_code() && passed;
    passed = test_code_fences() && passed;
    passed = test_tables_and_bounds() && passed;
    passed = test_table_alignment() && passed;
    passed = test_list_hierarchy_and_tasks() && passed;
    passed = test_quote_containers() && passed;
    passed = test_images() && passed;
    passed = test_links() && passed;
    passed = test_resolved_links() && passed;
    passed = test_thematic_breaks() && passed;
    passed = test_hard_breaks() && passed;
    passed = test_mixed_list_code() && passed;

    if (passed)
    {
        std::cout << "Markdown operation tests passed.\n";
    }

    return passed ? 0 : 1;
}

int main()
{
    return run_markdown_tests();
}
