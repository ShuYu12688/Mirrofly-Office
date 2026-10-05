#include <mirrorfly/markdown.hpp>
#include <mirrorfly/text.hpp>

#include <iostream>

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    mirrorfly::MarkdownInlineRun styled(std::string text, int mask)
    {
        return {std::move(text), (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, false};
    }
}

int run_markdown_inline_tests()
{
    using mirrorfly::markdown_inline_label;
    using mirrorfly::MarkdownInlineRun;
    bool passed = check(markdown_inline_label({{"plain [caption] & body"}}) == "plain \\[caption\\] \\& body",
        "literal captions escape source punctuation without introducing markup");
    for (int mask = 0; mask < 512; ++mask)
    {
        const auto body = mirrorfly::markdown_paragraph_text({{styled("a", mask & 7)},
            {styled("b", (mask >> 3) & 7), 1, "../body", "title"}, {styled("c", (mask >> 6) & 7)}});
        const auto paragraphs = mirrorfly::markdown_paragraphs(body + "\n");
        passed = check(!body.empty() && paragraphs.size() == 1,
                     "public paragraph composition parses ordinary styles and an independent link") &&
            passed;
        if (paragraphs.size() == 1)
        {
            std::vector<int> states;
            std::string text;
            std::string linked;
            for (const auto& run : paragraphs[0].runs)
            {
                const auto& style = run.style;
                text += style.text;
                const int state = (style.bold ? 1 : 0) | (style.italic ? 2 : 0) | (style.strike ? 4 : 0);
                states.insert(states.end(), style.text.size(), state);
                if (run.link_id != 0)
                {
                    linked += style.text;
                    passed = check(run.url == "../body" && run.title == "title",
                                 "public paragraph link runs preserve target and tooltip") &&
                        passed;
                }
            }
            passed = check(text == "abc" && linked == "b" &&
                             states == std::vector<int>{mask & 7, (mask >> 3) & 7, (mask >> 6) & 7},
                         "public paragraph runs retain all crossing styles and exact link boundaries") &&
                passed;
        }
        const auto label = markdown_inline_label(
            {styled("a", mask & 7), styled("b", (mask >> 3) & 7), styled("c", (mask >> 6) & 7)});
        const auto links = mirrorfly::markdown_links("[" + label + "](../x)");
        passed =
            check(!label.empty() && links.size() == 1 && links[0].text == "abc" && links[0].url == "../x",
                "all three-run bold/italic/strike combinations preserve literal caption text") &&
            passed;
        if (links.size() == 1)
        {
            std::vector<int> states;
            for (const auto& run : links[0].runs)
            {
                const int state = (run.bold ? 1 : 0) | (run.italic ? 2 : 0) | (run.strike ? 4 : 0);
                for (std::size_t index = 0; index < run.text.size(); ++index)
                    states.push_back(state);
            }
            passed = check(states == std::vector<int>{mask & 7, (mask >> 3) & 7, (mask >> 6) & 7},
                         "public decoded caption runs expose each crossing style without parser types") &&
                passed;
        }
    }
    for (int mask = 0; mask < 512; ++mask)
    {
        const auto source = markdown_inline_label(
            {styled(" \t", mask & 7), styled(u8"　x", (mask >> 3) & 7), styled(u8"  ", (mask >> 6) & 7)});
        const auto paragraphs = mirrorfly::markdown_paragraphs(source + "\n");
        std::string text;
        std::vector<int> states;
        if (paragraphs.size() == 1)
            for (const auto& run : paragraphs[0].runs)
            {
                text += run.style.text;
                const int style =
                    (run.style.bold ? 1 : 0) | (run.style.italic ? 2 : 0) | (run.style.strike ? 4 : 0);
                states.insert(states.end(), run.style.text.size(), style);
            }
        std::vector<int> expected(2, mask & 7);
        expected.insert(expected.end(), 4, (mask >> 3) & 7);
        expected.insert(expected.end(), 3, (mask >> 6) & 7);
        passed = check(paragraphs.size() == 1 && text == u8" \t　x  " && states == expected,
                     "all crossing whitespace styles retain exact UTF-8 bytes and style ownership") &&
            passed;
    }
    const auto combined = markdown_inline_label({styled(u8"中🦋", 3), styled("!", 5), styled("&", 6)});
    const auto links = mirrorfly::markdown_links("[" + combined + "](../x)");
    passed = check(links.size() == 1 && links[0].text == u8"中🦋!&",
                 "Unicode, punctuation and entity boundaries preserve captions") &&
        passed;
    const auto tab_links =
        mirrorfly::markdown_links("[" + markdown_inline_label({styled("a\tb", 1)}) + "](x)");
    passed =
        check(markdown_inline_label({styled("left ", 1), styled("  ", 3), styled("right", 2)}).find("****") ==
                    std::string::npos &&
                tab_links.size() == 1 && tab_links[0].text == "a\tb",
            "boundary whitespace has no empty style markers and tabs stay literal") &&
        passed;
    const std::vector<MarkdownInlineRun> code{
        {"before ", true}, {"a[b]|c", true, true, false, true}, {" after", false, true}};
    const auto code_label = markdown_inline_label(code);
    const auto code_links = mirrorfly::markdown_links("[" + code_label + "](x)");
    passed = check(code_links.size() == 1 && code_links[0].text == "before a[b]|c after" &&
                     markdown_inline_label(code, true).find("\\|") != std::string::npos,
                 "code runs compose with emphasis and escape pipes only for table cells") &&
        passed;
    for (const auto& text : {std::string("a\nb"), std::string("a\0b", 3), std::string("\xC0\xAF", 2),
             std::string(mirrorfly::maximum_text_bytes + 1, 'x')})
        passed = check(markdown_inline_label({{text}}).empty(),
                     "invalid and oversized captions reject without a partial result") &&
            passed;
    for (const auto& literal : {std::string("a|b"), std::string("a\\|b"), std::string("a\\\\|b")})
    {
        const auto label = markdown_inline_label({{literal, true, true, true, true}}, true);
        const auto table = mirrorfly::markdown_links("| A |\n| --- |\n| [" + label + "](x) |\n");
        passed =
            check(table.size() == 1 && table[0].text == literal && table[0].runs.size() == 1 &&
                    table[0].runs[0].code && table[0].runs[0].bold && table[0].runs[0].italic &&
                    table[0].runs[0].strike,
                "public table caption metadata removes exactly one pipe escape without losing slashes") &&
            passed;
    }
    passed = check(markdown_inline_label({}).empty() && markdown_inline_label({{"", true}}).empty(),
                 "empty styled captions do not produce delimiters") &&
        passed;
    for (const auto& tail : {std::string("\n    code\n"), std::string("\n```\ncode\n```\n"),
             std::string("\n| A |\n| --- |\n| body |\n"), std::string("\n<div>html</div>\n"),
             std::string("\n- - -\n")})
    {
        const auto paragraphs = mirrorfly::markdown_paragraphs("before\n" + tail);
        std::string text;
        if (paragraphs.size() == 1)
            for (const auto& run : paragraphs[0].runs)
                text += run.style.text;
        passed = check(paragraphs.size() == 1 && text == "before",
                     "leaf block transitions cannot append code/table/HTML text to preceding paragraphs") &&
            passed;
    }
    const auto adjacent = mirrorfly::markdown_paragraphs("[a](x)[b](x)\n");
    passed = check(adjacent.size() == 1 && adjacent[0].runs.size() == 2 &&
                     adjacent[0].runs[0].link_id != adjacent[0].runs[1].link_id,
                 "public paragraph identities distinguish adjacent links with equal targets") &&
        passed;
    passed = check(mirrorfly::markdown_paragraph_text({{{"plain"}, 0, "x"}}).empty() &&
                     mirrorfly::markdown_paragraph_text({{{"link"}, 1}}).empty() &&
                     mirrorfly::markdown_paragraph_text({{{"a"}, 1, "x"}, {{"b"}, 1, "y"}}).empty(),
                 "paragraph composition rejects inconsistent link metadata without partial output") &&
        passed;
    const auto owned = mirrorfly::markdown_paragraphs("- parent\n\n  > quote\n\n  after\n\n- tail\n");
    passed = check(owned.size() == 4 && owned[0].containers.size() == 1 && owned[1].containers.size() == 2 &&
                     owned[1].containers[0].kind == "listItem" && owned[1].containers[1].kind == "quote" &&
                     owned[0].containers[0].identity == owned[2].containers[0].identity &&
                     owned[0].containers[0].identity != owned[3].containers[0].identity,
                 "public ordered ownership distinguishes quote-in-list and continued sibling items") &&
        passed;
    const auto quoted = mirrorfly::markdown_paragraphs("> 100. parent\n>\n>      continuation\n");
    passed = check(quoted.size() == 2 && quoted[0].containers.size() == 2 &&
                     quoted[0].containers[0].kind == "quote" && quoted[0].containers[1].opening == "100. " &&
                     quoted[0].containers[1].continuation == "     " &&
                     quoted[0].containers[1].identity == quoted[1].containers[1].identity,
                 "ordered container metadata preserves wide-number continuation and list-in-quote order") &&
        passed;
    const auto parenthesized = mirrorfly::markdown_paragraphs("98) first\n7) second\n\n100) third\n");
    passed =
        check(parenthesized.size() == 3 && parenthesized[0].containers[0].ordered &&
                parenthesized[0].containers[0].ordinal == 98 &&
                parenthesized[1].containers[0].ordinal == 99 &&
                parenthesized[2].containers[0].ordinal == 100 &&
                parenthesized[0].containers[0].delimiter == ')' && !parenthesized[0].containers[0].tight &&
                parenthesized[0].containers[0].list_identity == parenthesized[2].containers[0].list_identity,
            "public list semantics expose starting numbers, delimiter and shared loose-list identity") &&
        passed;
    const auto maximum_number = mirrorfly::markdown_paragraphs("999999999) first\n999999999) second\n");
    passed = check(maximum_number.size() == 2 && maximum_number[1].containers[0].ordinal == 1000000000 &&
                     maximum_number[1].containers[0].opening == "999999999) " &&
                     maximum_number[1].containers[0].continuation.size() == 11,
                 "semantic counters beyond nine digits still expose a valid CommonMark source marker") &&
        passed;
    const auto leaves = mirrorfly::markdown_blocks(
        "100) parent\n\n     > ```cpp\n     > x & <y>\n     > ```\n     >\n"
        "     > | A |\n     > | --- |\n     > | [**b**](../x) |\n     > | `a\\|b` |\n"
        "     >\n     > ---\n");
    passed =
        check(leaves.size() == 3 && leaves[0].kind == "code" && leaves[0].language == "cpp" &&
                leaves[0].text == "x & <y>\n" && leaves[0].containers.size() == 2 &&
                leaves[0].containers[0].kind == "listItem" && leaves[0].containers[0].ordinal == 100 &&
                leaves[0].containers[1].kind == "quote" && leaves[1].kind == "table" &&
                leaves[1].columns == 1 && leaves[1].cells.size() == 3 && leaves[2].kind == "thematicBreak" &&
                leaves[0].containers[0].identity == leaves[2].containers[0].identity,
            "public block metadata exposes ordered code/table/rule ownership and literal code") &&
        passed;
    if (leaves.size() == 3 && leaves[1].cells.size() == 3)
    {
        const auto& cells = leaves[1].cells;
        std::string code_text;
        bool code_style = true;
        for (const auto& run : cells[2])
        {
            code_text += run.style.text;
            code_style = code_style && run.style.code;
        }
        passed = check(cells[0].size() == 1 && !cells[0][0].style.bold && cells[1].size() == 1 &&
                         cells[1][0].style.bold && cells[1][0].link_id != 0 && cells[1][0].url == "../x" &&
                         code_text == "a|b" && code_style,
                     "public cells retain explicit link emphasis and decode exactly one code pipe escape") &&
            passed;
    }
    for (const std::string& source : {std::string(u8"🦋前\n\n> - ```cpp\n>   x\n>   ```\n"),
             std::string("~~~cpp\n~~~\n"), std::string("```cpp\nx")})
    {
        const auto blocks = mirrorfly::markdown_blocks(source);
        const auto opening = source.find(source.find("~~~") != std::string::npos ? "~~~" : "```");
        passed = check(blocks.size() == 1 && blocks[0].kind == "code" && blocks[0].start == opening &&
                         blocks[0].end <= source.size() && blocks[0].end >= blocks[0].start &&
                         source.substr(blocks[0].start, blocks[0].end - blocks[0].start).find("cpp") !=
                             std::string::npos &&
                         (source.back() != '\n' || blocks[0].end == source.size() - 1),
                     "public UTF-8 leaf bounds cover opening and closing fences, empty code and unterminated "
                     "EOF") &&
            passed;
    }
    if (passed)
        std::cout << "Core inline label composition passed.\n";
    return passed ? 0 : 1;
}

int main()
{
    return run_markdown_inline_tests();
}
