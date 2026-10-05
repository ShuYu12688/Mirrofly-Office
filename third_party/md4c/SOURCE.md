# MD4C source and license provenance

Upstream: https://github.com/mity/md4c/tree/release-0.5.2
Version: 0.5.2, MIT; retain LICENSE.txt and each source header.
Source archive: https://codeload.github.com/mity/md4c/zip/refs/tags/release-0.5.2
SHA-256: bc0bd850ef8ae9bb44dd7df048ff9d8147370f7becafd49f9245e95471da15cc

The parser and entity table are compiled as a private static core dependency.
No Qt or platform types enter the core public interface. The md_parse symbol is
renamed to mirrorfly_md_parse by a compiler definition, avoiding another
MD4C copy in the Qt runtime. The entity lookup symbol is private in the same way.

Local changes are recorded verbatim in mirrorfly_source_ranges.patch: an optional
source_span callback is appended to MD_PARSER and called before link/image/code-span entry,
source_break reports parsed hard breaks and the following container prefix,
and source_block reports all leaf ranges, including tight-list paragraphs,
opening/closing code delimiters, tables and rules. A private leaf end offset records
the already-recognized closing fence without changing code content or parsing. Non-paragraph notifications reset paragraph
state so literal text cannot leak into preceding prose. All callbacks use the parser's existing original-source
delimiter offsets. No parsing rules,
Unicode tables or entity tables are changed. The custom header is internal only;
this parser ABI must not be used with the Qt-bundled MD4C. entity.c and entity.h
are unchanged upstream source. Source files retain upstream formatting.
