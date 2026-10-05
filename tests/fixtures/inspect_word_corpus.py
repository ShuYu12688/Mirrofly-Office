"""Read-only inventory of DOCX features used to scope rendering and preservation tests."""

import argparse
import collections
import hashlib
import json
import pathlib
import zipfile

from lxml import etree


def inspect(path):
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    with zipfile.ZipFile(path) as archive:
        info = archive.infolist()
        xml = archive.read("word/document.xml")
        root = etree.fromstring(xml, etree.XMLParser(resolve_entities=False, no_network=True))
        ns = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
              "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
              "wp": "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"}
        counts = collections.Counter(etree.QName(node).localname for node in root.iter()
                                     if isinstance(node.tag, str))
        text = root.xpath("//w:t/text()", namespaces=ns)
        media = [item for item in info if item.filename.startswith("word/media/")]
        return {
            "file": str(path), "archive_bytes": path.stat().st_size,
            "sha256": digest,
            "expanded_bytes": sum(item.file_size for item in info), "parts": len(info),
            "document_xml_bytes": len(xml), "text_utf8_bytes": len("".join(text).encode()),
            "paragraphs": counts["p"], "tables": counts["tbl"], "rows": counts["tr"],
            "cells": counts["tc"], "images": counts["blip"], "inline": counts["inline"],
            "anchors": counts["anchor"], "sections": counts["sectPr"],
            "equations": counts["oMath"], "textboxes": counts["txbxContent"],
            "media_bytes": sum(item.file_size for item in media),
            "media_types": dict(collections.Counter(pathlib.Path(item.filename).suffix for item in media)),
            "largest_media": max((item.file_size for item in media), default=0),
            "node_counts": dict(counts),
            "page_sizes": root.xpath("//w:pgSz/@w:w | //w:pgSz/@w:h", namespaces=ns),
            "image_extents": [dict(node.attrib) for node in root.xpath("//wp:extent", namespaces=ns)][:12],
        }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("documents", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps([inspect(path) for path in args.documents], ensure_ascii=False, indent=2))
