"""Read real EditorTools link exports with markdown-it-py, independently of Qt."""

import argparse
import json
import sys
from pathlib import Path
from urllib.parse import unquote


def verify(directory, runtime):
    if runtime:
        sys.path.insert(0, str(runtime.resolve()))
    from markdown_it import MarkdownIt
    from mdit_py_plugins.tasklists import tasklists_plugin

    parser = MarkdownIt("commonmark").enable(["table", "strikethrough"]).use(tasklists_plugin)
    fixtures = {}
    for name in ("styled", "table", "adjacent"):
        source = (directory / (name + ".md")).read_text(encoding="utf-8")
        assert "MIRRORFLY" not in source, "serialization marker leaked"
        blocks = parser.parse(source)
        inline = [child for block in blocks if block.type == "inline" for child in block.children]
        links = [token for token in inline if token.type == "link_open"]
        assert len(links) == 1, (name, "one semantic link", len(links))
        text = "".join(token.content for token in inline if token.type in ("text", "code_inline"))
        fixtures[name] = (blocks, inline, links[0], text)
    _, inline, link, text = fixtures["styled"]
    assert text == "前 你好🦋 and a[b] 后", text
    assert unquote(link.attrGet("href")) == "../路径 (草稿)/x?one=1&two=2"
    assert link.attrGet("title") == '查看 "草稿" & 说明'
    assert any(token.type == "strong_open" for token in inline)
    assert any(token.type == "code_inline" and token.content == "a[b]" for token in inline)
    blocks, _, link, text = fixtures["table"]
    assert sum(token.type == "td_open" for token in blocks) == 2
    assert unquote(link.attrGet("href")) == "../a|b(x)"
    assert link.attrGet("title") == "a|b"
    assert text == "ABlabelbody"
    _, inline, link, text = fixtures["adjacent"]
    assert text == "x" * 78 + "labeltail"
    assert not any(token.type == "softbreak" for token in inline)
    assert link.attrGet("href") == "x" * 2048
    assert link.attrGet("title") == "t" * 256
    for name, count, expected in (
        ("reference", 2, "你好🦋 R"),
        ("multiline", 1, "first second"),
        ("automatic", 3, "https://example.com/a a@example.com https://example.com/x www.example.com"),
        ("complex", 1, "bold nested rest strike tail"),
        ("whitespace", 1, "left   right"),
        ("source-reference", 2, "🦋前 甲 ref"),
        ("tab", 0, "a@example.com\tbody"),
    ):
        source = (directory / (name + ".md")).read_text(encoding="utf-8")
        assert "MIRRORFLY" not in source, name
        blocks = parser.parse(source)
        inline = [child for block in blocks if block.type == "inline" for child in block.children]
        links = [token for token in inline if token.type == "link_open"]
        assert len(links) == count, (name, len(links))
        text = "".join(token.content if token.type != "softbreak" else " "
                       for token in inline if token.type in ("text", "code_inline", "softbreak"))
        assert text == expected, (name, text)
        if name == "reference":
            assert links[0].attrGet("href") == "../changed"
            assert links[0].attrGet("title") == "updated"
            assert unquote(links[1].attrGet("href")) == "../路径?x=©A"
            assert links[1].attrGet("title") == "ö ≂̸"
        elif name == "multiline":
            assert links[0].attrGet("href") == "../multi"
            assert any(token.type == "blockquote_open" for token in blocks)
            assert any(token.type == "strong_open" for token in inline)
        elif name == "automatic":
            assert [token.attrGet("href") for token in links] == [
                "https://example.com/a", "https://example.com/x", "http://www.example.com"]
            assert "a\\@example\\.com" in source
        elif name == "complex":
            assert links[0].attrGet("href") == "../complex"
            styles = []
            for token in inline:
                if token.type.endswith("_open"):
                    styles.append(token.type)
                elif token.type.endswith("_close"):
                    styles.pop()
                elif token.type == "text" and token.content == "nested":
                    assert "strong_open" in styles and "em_open" in styles
                elif token.type == "text" and token.content == "strike":
                    assert "s_open" in styles
        elif name == "source-reference":
            assert [token.attrGet("href") for token in links] == ["../new", "../original"]
            assert '[ref]: ../original "提示"' in source
    corpus = json.loads((directory / "crossing-styles.json").read_text(encoding="utf-8"))
    assert len(corpus) == 512
    for case in corpus:
        blocks = parser.parse(case["source"])
        inline = [child for block in blocks if block.type == "inline" for child in block.children]
        links = [token for token in inline if token.type == "link_open"]
        assert len(links) == 1 and links[0].attrGet("href") == "../styles"
        assert "MIRRORFLY" not in case["source"]
        depth = {"strong": 0, "em": 0, "s": 0}
        text, states = "", []
        for token in inline:
            for kind in depth:
                if token.type == kind + "_open":
                    depth[kind] += 1
                elif token.type == kind + "_close":
                    depth[kind] -= 1
            if token.type in ("text", "code_inline"):
                mask = (1 if depth["strong"] else 0) | (2 if depth["em"] else 0) | (4 if depth["s"] else 0)
                text += token.content
                states.extend([mask] * len(token.content))
        assert text == case["text"] and states == case["states"], (case, text, states)
        assert all(value == 0 for value in depth.values())
    blocks = parser.parse((directory / "linked-code-table.md").read_text(encoding="utf-8"))
    inline = [child for block in blocks if block.type == "inline" for child in block.children]
    assert sum(token.type == "td_open" for token in blocks) == 2
    links = [token for token in inline if token.type == "link_open"]
    assert len(links) == 1 and links[0].attrGet("href") == "../pipe"
    assert any(token.type == "code_inline" and token.content == "a\\|b" for token in inline)
    assert all(any(token.type == kind + "_open" for token in inline) for kind in ("strong", "em", "s"))
    hard_breaks = json.loads((directory / "hard-breaks.json").read_text(encoding="utf-8"))
    assert len(hard_breaks) == 17
    for case in hard_breaks:
        blocks = parser.parse(case["source"])
        inline = [child for block in blocks if block.type == "inline" for child in block.children]
        assert sum(token.type == "hardbreak" for token in inline) == case["breaks"], case
        assert sum(token.type == "link_open" for token in inline) == case["links"], case
        text = "".join(token.content if token.type != "hardbreak" else "\n"
                       for token in inline if token.type in ("text", "code_inline", "hardbreak"))
        checkboxes = [token for token in inline if token.type == "html_inline"
                      and 'class="task-list-item-checkbox"' in token.content]
        if checkboxes:
            assert len(checkboxes) == 1 and 'checked="checked"' in checkboxes[0].content
            assert text.startswith(" ") and "- [x] " in case["source"]
            text = text[1:]
        assert text == case["text"], (case, text)
        assert "MIRRORFLY" not in case["source"]
    print("Independent CommonMark/GFM verification passed: eleven saved fixtures and 512 crossing-style saves.")
    print("Seventeen real paragraph/list/quote/link/block-opener hard-break saves independently verified.")
    rules = json.loads((directory / "thematic-breaks.json").read_text(encoding="utf-8"))
    assert len(rules) == 11
    def block_signature(source):
        signature = []
        for token in parser.parse(source):
            if token.type == "hr":
                continue
            if token.type == "inline":
                children = token.children
                caption = "".join(child.content if child.type != "softbreak" else " "
                                  for child in children if child.type in ("text", "code_inline", "softbreak"))
                styles = [(child.type, child.attrGet("href"), child.attrGet("title"))
                          for child in children if child.type not in ("text", "softbreak")]
                signature.append((token.type, caption, styles))
            else:
                signature.append((token.type, token.tag, token.attrGet("start")))
        return signature
    for case in rules:
        assert sum(token.type == "hr" for token in parser.parse(case["source"])) == 1, case
        assert block_signature(case["source"]) == block_signature(case["original"]), case
        assert "MIRRORFLY" not in case["source"]
    print("Eleven actual separator saves verified: text, headings, links and list/quote ownership preserved.")
    bodies = json.loads((directory / "body-styles.json").read_text(encoding="utf-8"))
    assert len(bodies) == 3072
    for case in bodies:
        blocks = parser.parse(case["source"])
        original = parser.parse(case["initial"])
        signature = lambda tokens: [(token.type, token.tag, token.attrGet("start"))
                                    for token in tokens if token.type != "inline"]
        assert signature(blocks) == signature(original), case
        inline = [child for block in blocks if block.type == "inline" for child in block.children]
        links = [token for token in inline if token.type == "link_open"]
        assert len(links) == (1 if "../body" in case["initial"] else 0), case
        if links:
            assert links[0].attrGet("href") == "../body", case
        depth = {"strong": 0, "em": 0, "s": 0, "link": 0}
        text, states, linked = "", [], ""
        for token in inline:
            for kind in depth:
                if token.type == kind + "_open":
                    depth[kind] += 1
                elif token.type == kind + "_close":
                    depth[kind] -= 1
            if token.type in ("text", "code_inline"):
                mask = (1 if depth["strong"] else 0) | (2 if depth["em"] else 0) | (4 if depth["s"] else 0)
                text += token.content
                states.extend([mask] * len(token.content))
                if depth["link"]:
                    linked += token.content
        assert text == case["text"] and states == case["states"], (case, text, states)
        assert linked == ("b" if links else ""), (case, linked)
        assert all(value == 0 for value in depth.values()), case
        assert "MIRRORFLY" not in case["source"]
        assert not any(0xE000 <= ord(character) <= 0xF8FF for character in case["source"])
    print("3072 actual body/heading/list/quote/link crossing-style saves independently verified.")
    def container_signature(source):
        blocks = parser.parse(source)
        structure = [(token.type, token.tag, token.attrGet("start"), token.hidden)
                     for token in blocks if token.type != "inline"]
        text, states, checkbox = "", [], []
        depth = {"strong": 0, "em": 0, "s": 0}
        for block in blocks:
            if block.type != "inline":
                continue
            for token in block.children:
                for kind in depth:
                    if token.type == kind + "_open":
                        depth[kind] += 1
                    elif token.type == kind + "_close":
                        depth[kind] -= 1
                if token.type in ("text", "code_inline", "hardbreak", "softbreak"):
                    value = token.content if token.type not in ("hardbreak", "softbreak") else "\n"
                    text += value
                    mask = (1 if depth["strong"] else 0) | (2 if depth["em"] else 0) | (4 if depth["s"] else 0)
                    states.extend([mask] * len(value))
                elif token.type == "html_inline":
                    checkbox.append(token.content)
        return structure, text, states, checkbox
    containers = json.loads((directory / "containers.json").read_text(encoding="utf-8"))
    assert len(containers) == 8
    for case in containers:
        original = container_signature(case["original"])
        saved = container_signature(case["source"])
        expected = original[2].copy()
        first = next(index for index, character in enumerate(original[1]) if not character.isspace())
        expected[first] |= 2
        assert saved[0] == original[0] and saved[1] == original[1] and saved[3] == original[3], case
        assert saved[2] == expected, (case, saved[2], expected)
        assert "MIRRORFLY" not in case["source"]
    moves = json.loads((directory / "container-moves.json").read_text(encoding="utf-8"))
    assert len(moves) == 1
    for case in moves:
        assert container_signature(case["source"]) == container_signature(case["expected"]), case
        assert container_signature(case["restored"]) == container_signature(case["original"]), case
    print("Eight continued/nested container saves and one parent indent/outdent independently verified.")
    entered = json.loads((directory / "container-enter.json").read_text(encoding="utf-8"))
    assert container_signature(entered["source"]) == container_signature(entered["expected"]), entered
    print("Native new list-item insertion independently verified as a separate item.")
    created = json.loads((directory / "container-created.json").read_text(encoding="utf-8"))
    assert container_signature(created["source"]) == container_signature(created["expected"]), created
    print("Fresh public list conversion and indentation independently verified.")

    ordered = json.loads((directory / "ordered-markers.json").read_text(encoding="utf-8"))
    assert len(ordered) == 18
    for case in ordered:
        original = container_signature(case["original"])
        saved = container_signature(case["source"])
        expected = original[2].copy()
        if case["style"]:
            first = next(index for index, character in enumerate(original[1]) if not character.isspace())
            expected[first] |= 2
        assert saved[0] == original[0] and saved[1] == original[1] and saved[3] == original[3], case
        assert saved[2] == expected, (case, saved[2], expected)
        markers = lambda source: [(token.type, token.markup) for token in parser.parse(source)
                                  if token.type in ("ordered_list_open", "bullet_list_open")]
        assert markers(case["original"]) == markers(case["source"]), case
        assert "MIRRORFLY" not in case["source"]
        assert not any(0xE000 <= ord(character) <= 0xF8FF for character in case["source"])
    print("Eighteen actual marker-variant saves independently verified, including native Enter and delimiter-aware outdent.")

    mixed = json.loads((directory / "block-containers.json").read_text(encoding="utf-8"))
    assert len(mixed) == 10
    for case in mixed:
        original = container_signature(case["original"])
        saved = container_signature(case["source"])
        expected = original[2].copy()
        expected[original[1].index(case["selected"])] |= 2
        assert saved[0] == original[0] and saved[1] == original[1] and saved[3] == original[3], case
        assert saved[2] == expected, (case, saved[2], expected)
        leaves = lambda source: [(token.type, token.tag, token.attrs,
                                  token.content if token.type in ("fence", "code_block") else "",
                                  token.info if token.type == "fence" else "")
                                 for token in parser.parse(source) if token.type != "inline"]
        assert leaves(case["source"]) == leaves(case["original"]), case
        assert "MIRRORFLY" not in case["source"]
        assert not any(0xE000 <= ord(character) <= 0xF8FF for character in case["source"])
    print("Ten actual compound code/table/rule/empty-head ownership saves independently verified, including literal code and cell styles.")
    moves = json.loads((directory / "block-moves.json").read_text(encoding="utf-8"))
    assert len(moves) == 12
    for case in moves:
        for name in ("source", "sourceEdit"):
            assert container_signature(case[name]) == container_signature(case["expected"]), (name, case)
            assert leaves(case[name]) == leaves(case["expected"]), (name, case)
        assert container_signature(case["restored"]) == container_signature(case["original"]), case
        assert leaves(case["restored"]) == leaves(case["original"]), case
    print("Twelve mixed subtree moves independently verified through actual visual and public source transactions.")
    quotes = json.loads((directory / "block-quotes.json").read_text(encoding="utf-8"))
    assert len(quotes) == 2
    for case in quotes:
        for name in ("source", "sourceEdit"):
            assert container_signature(case[name]) == container_signature(case["expected"]), (name, case)
            assert leaves(case[name]) == leaves(case["expected"]), (name, case)
    print("Two mixed parent quote transformations independently verified through visual and source transactions.")
    literal_imports = json.loads((directory / "literal-imports.json").read_text(encoding="utf-8"))
    assert len(literal_imports) == 12
    for case in literal_imports:
        # Indented code may canonically save as fences; compare the complete rendered semantics.
        assert parser.render(case["source"]) == parser.render(case["expected"]), case
    print("12 actual literal HTML/code/escaped text/pipe imports independently verified after visual edits.")
    space_breaks = json.loads((directory / "source-space-breaks.json").read_text(encoding="utf-8"))
    assert len(space_breaks) == 12
    for case in space_breaks:
        assert parser.render(case["source"]) == parser.render(case["expected"]), case
        # Existing spaces/tabs remain text before the break, not consumed Markdown delimiters.
        inline = [token for token in parser.parse(case["source"]) if token.type == "inline"]
        visible = "".join(child.content for token in inline for child in token.children or []
                          if child.type == "text")
        original = "".join(child.content for token in parser.parse(case["initial"])
                           for child in token.children or [] if child.type == "text")
        assert visible == original, (visible, original)
    print("12 source hard-break whitespace/container cases independently verified after real save-reopen.")
    empty_headings = json.loads((directory / "empty-headings.json").read_text(encoding="utf-8"))
    assert len(empty_headings) == 60
    for case in empty_headings:
        for name in ("source", "sourceEdit"):
            assert container_signature(case[name]) == container_signature(case["expected"]), (name, case)
        for name in ("plain", "sourcePlain"):
            assert container_signature(case[name]) == container_signature(case["plainExpected"]), (name, case)
    entered = json.loads((directory / "heading-enter.json").read_text(encoding="utf-8"))
    assert len(entered) == 20
    for case in entered:
        assert container_signature(case["source"]) == container_signature(case["expected"]), case
    print("60 empty heading changes/resets and 20 heading Enter saves independently verified.")
    headings = json.loads((directory / "heading-levels.json").read_text(encoding="utf-8"))
    assert len(headings) == 84
    assert {case["level"] for case in headings} == set(range(1, 7))
    def heading_signature(source):
        structure, text, states, checkbox = container_signature(source)
        # A Setext soft newline and the equivalent ATX space have the same rendered text.
        assert not any(child.type == "hardbreak" for block in parser.parse(source)
                       if block.type == "inline" for child in block.children)
        return structure, text.replace("\n", " "), states, checkbox
    for case in headings:
        for name in ("source", "sourceEdit"):
            assert heading_signature(case[name]) == heading_signature(case["expected"]), (name, case)
            assert leaves(case[name]) == leaves(case["expected"]), (name, case)
        for name in ("plain", "sourcePlain"):
            assert heading_signature(case[name]) == heading_signature(case["plainExpected"]), (name, case)
            assert leaves(case[name]) == leaves(case["plainExpected"]), (name, case)
        def link_signature(source):
            return [(token.type, token.attrs, token.content if token.type == "code_inline" else "")
                    for block in parser.parse(source) if block.type == "inline"
                    for token in block.children if token.type in ("link_open", "link_close", "code_inline")]
        for name in ("source", "sourceEdit", "plain", "sourcePlain"):
            assert link_signature(case[name]) == link_signature(case["expected"]), (name, case)
            assert "MIRRORFLY" not in case[name]
            assert not any(0xE000 <= ord(character) <= 0xF8FF for character in case[name])
    print("84 six-level ATX/Setext/container conversions independently verified along visual/source and paragraph reset paths.")
    images = json.loads((directory / "images.json").read_text(encoding="utf-8"))
    assert len(images) == 20
    def image_alt(tokens):
        return "".join(token.content if token.type in ("text", "text_special", "code_inline")
                       else image_alt(token.children or []) if token.type == "image"
                       else " " if token.type in ("softbreak", "hardbreak") else "" for token in tokens)
    def image_signature(source):
        return [(token.attrGet("src"), token.attrGet("title"), image_alt(token.children or []))
                for block in parser.parse(source) if block.type == "inline"
                for token in block.children if token.type == "image"]
    # markdown-it-py 4's renderInlineAsText omits text_special in image captions.
    # Compare decoded image tokens, then render the remaining full structure with stable empty alt.
    def image_renderer(tokens, index, options, env):
        tokens[index].attrSet("alt", "")
        return parser.renderer.renderToken(tokens, index, options, env)
    parser.renderer.rules["image"] = image_renderer
    for case in images:
        assert image_signature(case["source"]) == image_signature(case["original"]), case
        assert image_signature(case["updated"]) == image_signature(case["expected"]), case
        assert parser.render(case["source"]) == parser.render(case["original"]), case
        assert parser.render(case["updated"]) == parser.render(case["expected"]), case
        assert "MIRRORFLY" not in case["source"] + case["updated"]
    print("20 image/container/reference/linked-image saves and edits independently rendered and compared.")
    codes = json.loads((directory / "code-spans.json").read_text(encoding="utf-8"))
    assert len(codes) == 23
    code_tokens = lambda source: [token.content for block in parser.parse(source)
                                 if block.type == "inline" for token in block.children
                                 if token.type == "code_inline"]
    links_only = lambda source: [(token.type, token.attrs) for block in parser.parse(source)
                                if block.type == "inline" for token in block.children
                                if token.type in ("link_open", "link_close")]
    for case in codes:
        if "breakSource" in case:
            assert "".join(code_tokens(case["breakSource"])) == "before after", case
            assert container_signature(case["breakSource"])[1:3] == ("before after", [1] * 6 + [0] * 6), case
            assert not any(token.type == "hardbreak" for block in parser.parse(case["breakSource"])
                           if block.type == "inline" for token in block.children), case
            continue
        assert code_tokens(case["original"]) == [case["code"]], case
        for name in ("plain", "sourcePlain", "restored"):
            assert container_signature(case[name]) == container_signature(case["original"]), (name, case)
            assert links_only(case[name]) == links_only(case["original"]), (name, case)
        assert not code_tokens(case["plain"]) and not code_tokens(case["sourcePlain"]), case
        assert code_tokens(case["restored"]) == [case["code"]], case
        def char_signature(source, include_code):
            result = []
            link = None
            for block in parser.parse(source):
                if block.type != "inline":
                    continue
                for token in block.children:
                    if token.type == "link_open":
                        link = token.attrs
                    elif token.type == "link_close":
                        link = None
                    elif token.type in ("text", "code_inline"):
                        result.extend((character, link, include_code and token.type == "code_inline")
                                      for character in token.content)
            return result
        # Strong/link nesting may reorder while each character retains both styles.
        assert char_signature(case["restored"], True) == char_signature(case["original"], True), case
        for name in ("plain", "sourcePlain"):
            assert char_signature(case[name], False) == char_signature(case["original"], False), (name, case)
    print("22 multiline code/whitespace/container cases and hard-break conversion independently compared.")

    spaces = json.loads((directory / "whitespace-styles.json").read_text(encoding="utf-8"))
    assert len(spaces) == 280
    def inline_links(source):
        return [(token.type, token.attrs) for block in parser.parse(source)
                if block.type == "inline" for token in block.children
                if token.type in ("link_open", "link_close")]
    for case in spaces:
        original = container_signature(case["original"])
        saved = container_signature(case["source"])
        expected = original[2].copy()
        start = original[1].index(case["selected"])
        for position in range(start, start + len(case["selected"])):
            expected[position] |= case["mask"]
        assert saved[0] == original[0] and saved[1] == original[1] and saved[3] == original[3], case
        assert saved[2] == expected, (case, saved[2], expected)
        assert container_signature(case["plain"]) == original, case
        assert inline_links(case["source"]) == inline_links(case["original"]), case
        assert inline_links(case["plain"]) == inline_links(case["original"]), case
    print("280 whitespace style/Unicode/tab cases independently verified across body, heading, lists, quotes, links and tables.")

    source_spaces = json.loads((directory / "whitespace-source.json").read_text(encoding="utf-8"))
    assert len(source_spaces) == 60
    for case in source_spaces:
        original = container_signature(case["original"])
        saved = container_signature(case["source"])
        expected = original[2].copy()
        start = original[1].index(case["selected"])
        for position in range(start, start + len(case["selected"])):
            expected[position] |= case["mask"]
        assert saved[0] == original[0] and saved[1] == original[1] and saved[3] == original[3], case
        assert saved[2] == expected, (case, saved[2], expected)
    print("60 public source whitespace operations independently verified, including word-adjacent escaped and Unicode neighbors.")


if __name__ == "__main__":
    arguments = argparse.ArgumentParser()
    arguments.add_argument("directory", type=Path)
    arguments.add_argument("--runtime", type=Path)
    options = arguments.parse_args()
    verify(options.directory, options.runtime)
