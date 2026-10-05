"""Independently verify local DOCX text edits and paragraph splits without rewriting input files."""

import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile

from lxml import etree


WORD = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
MATH = "http://schemas.openxmlformats.org/officeDocument/2006/math"
PROTECTED = {
    f"{{{WORD}}}{name}"
    for name in (
        "tblPr", "tblGrid", "tcPr", "trPr", "sectPr", "drawing", "pict", "object",
        "footnoteReference", "fldChar", "instrText", "bookmarkStart", "bookmarkEnd",
    )
} | {f"{{{MATH}}}oMath", f"{{{MATH}}}oMathPara"}


def signature(node):
    children = [item for item in node if isinstance(item.tag, str)]
    text = node.text if not children or (node.text and node.text.strip()) else None
    return node.tag, tuple(sorted(node.attrib.items())), text, tuple(map(signature, children))


def protected_content(root):
    return [signature(node) for node in root.iter() if node.tag in PROTECTED]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("edited", type=Path)
    parser.add_argument("--split", action="store_true", help="Require one extra paragraph with unchanged text")
    arguments = parser.parse_args()
    with arguments.original.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    with ZipFile(arguments.original) as original, ZipFile(arguments.edited) as edited:
        names = {name for name in original.namelist() if not name.endswith("/")}
        actual = {name for name in edited.namelist() if not name.endswith("/")}
        assert names == actual, "package part set changed"
        changed = sorted(name for name in names if original.read(name) != edited.read(name))
        assert changed == ["word/document.xml"], f"unexpected changed parts: {changed}"
        before = etree.fromstring(original.read("word/document.xml"))
        after = etree.fromstring(edited.read("word/document.xml"))
        retained = protected_content(before)
        assert retained == protected_content(after), "protected XML structure or text changed"
        for query in ("//comment()", "//processing-instruction()"):
            assert [etree.tostring(item, with_tail=False) for item in before.getroottree().xpath(query)] == [
                etree.tostring(item, with_tail=False) for item in after.getroottree().xpath(query)
            ], "XML annotations changed"
        if arguments.split:
            assert len(list(after.iter(f"{{{WORD}}}p"))) == len(list(before.iter(f"{{{WORD}}}p"))) + 1
            assert "".join(node.text or "" for node in before.iter(f"{{{WORD}}}t")) == "".join(
                node.text or "" for node in after.iter(f"{{{WORD}}}t")
            ), "paragraph split changed the document text"
    print(json.dumps({"success": True, "edited": str(arguments.edited), "sourceSHA256": digest,
                      "changedParts": changed, "protectedSubtrees": len(retained)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
