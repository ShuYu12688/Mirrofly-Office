#pragma once

#include "mirrorfly/markdown.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace mirrorfly::detail
{
    struct SourceLine
    {
        std::size_t start = 0;
        std::size_t end = 0;
        std::size_t next = 0;
        std::size_t prefix_end = 0;
        std::size_t marker_start = 0;
        int indent = 0;
        int quotes = 0;
        int owner = -1;
        bool literal = false;
        std::string_view text;
    };

    struct Marker
    {
        bool valid = false;
        std::size_t body = 0;
        std::size_t content = 0;
        std::size_t checkbox = std::string::npos;
    };

    struct ListItem
    {
        std::size_t line;
        std::size_t end;
        int parent;
        int level;
        Marker marker;
    };

    struct SourceLists
    {
        std::vector<SourceLine> lines;
        std::vector<ListItem> items;
        std::vector<std::pair<std::size_t, std::size_t>> fences;
    };

    SourceLine line_info(std::string_view text, int maximum_quotes = 100000);
    Marker list_marker(const SourceLine& line);
    SourceLists source_lists(const std::string& source);
}
