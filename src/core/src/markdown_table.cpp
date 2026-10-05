#include "markdown_table.hpp"
#include "mirrorfly/text.hpp"

#include <utf8/checked.h>

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    bool line_break(char character)
    {
        return character == '\n' || character == '\r';
    }

    bool space(char character)
    {
        return character == ' ' || character == '\t';
    }

    std::string_view trimmed(std::string_view text)
    {
        while (!text.empty() && space(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && space(text.back()))
            text.remove_suffix(1);
        return text;
    }

    struct Line
    {
        std::size_t start;
        std::size_t end;
        std::size_t content_start;
        std::string_view text;
        int quotes = 0;
        int indentation = 0;
        int list_indent = -1;
        int list_quote_level = 0;
        std::size_t list_owner_start = 0;
        bool list_marker = false;
        bool literal = false;
    };

    std::vector<Line> source_lines(const std::string& source)
    {
        std::vector<Line> lines;
        char fence = 0;
        std::size_t fence_length = 0;
        int fence_quotes = 0;
        int list_indent = -1;
        int list_quotes = 0;
        std::size_t list_owner_start = 0;
        for (std::size_t start = 0; start < source.size();)
        {
            const auto ending = source.find_first_of("\r\n", start);
            const auto end = ending == std::string::npos ? source.size() : ending;
            Line line{start, end, start, std::string_view(source).substr(start, end - start)};
            auto content = line.text;
            bool quoted_in_list = false;
            while (!content.empty())
            {
                while (!content.empty() && space(content.front()))
                {
                    line.indentation += content.front() == '\t' ? 4 : 1;
                    content.remove_prefix(1);
                }
                if (content.empty() || content.front() != '>')
                    break;
                if (list_indent >= 0 && line.quotes == list_quotes && line.indentation >= list_indent)
                    quoted_in_list = true;
                ++line.quotes;
                content.remove_prefix(1);
                if (!content.empty() && content.front() == ' ')
                    content.remove_prefix(1);
                line.indentation = 0;
            }
            std::size_t marker_length = 0;
            if (!content.empty() &&
                (content.front() == '-' || content.front() == '+' || content.front() == '*') &&
                (content.size() == 1 || space(content[1])))
                marker_length = std::min<std::size_t>(2, content.size());
            else
            {
                while (marker_length < content.size() && marker_length < 9 && content[marker_length] >= '0' &&
                    content[marker_length] <= '9')
                    ++marker_length;
                if (marker_length == 0 || marker_length + 1 >= content.size() ||
                    (content[marker_length] != '.' && content[marker_length] != ')') ||
                    !space(content[marker_length + 1]))
                    marker_length = 0;
                else
                    marker_length += 2;
            }
            if (fence != 0 && line.quotes != fence_quotes)
                fence = 0;
            if (marker_length > 0 && fence == 0)
            {
                line.list_marker = true;
                list_indent = line.indentation + static_cast<int>(marker_length);
                if (content.size() == 1)
                    ++list_indent;
                list_quotes = line.quotes;
                list_owner_start = start;
                content.remove_prefix(marker_length);
                while (!content.empty() && space(content.front()))
                    content.remove_prefix(1);
            }
            else if (!content.empty() && !quoted_in_list &&
                (line.quotes != list_quotes || line.indentation < list_indent))
                list_indent = -1;
            const int relative_indent =
                list_indent >= 0 && !line.list_marker ? line.indentation - list_indent : line.indentation;
            line.literal = fence != 0 || relative_indent >= 4;
            if (!content.empty() && relative_indent < 4 &&
                (content.front() == '\x60' || content.front() == '~'))
            {
                const char marker = content.front();
                std::size_t length = 0;
                while (length < content.size() && content[length] == marker)
                    ++length;
                const auto suffix = trimmed(content.substr(length));
                if (fence != 0 && marker == fence && length >= fence_length && suffix.empty())
                {
                    fence = 0;
                    line.literal = true;
                }
                else if (fence == 0 && length >= 3 &&
                    (marker != '\x60' || suffix.find('\x60') == std::string_view::npos))
                {
                    fence = marker;
                    fence_length = length;
                    fence_quotes = line.quotes;
                    line.literal = true;
                }
            }
            line.list_indent = list_indent;
            line.list_quote_level = list_quotes;
            line.list_owner_start = list_owner_start;
            line.content_start = start + line.text.size() - content.size();
            line.text = content;
            lines.push_back(line);
            start = end;
            if (start < source.size() && source[start++] == '\r' && start < source.size() &&
                source[start] == '\n')
                ++start;
        }
        return lines;
    }

    struct Cell
    {
        std::size_t start;
        std::size_t end;
        std::string_view text;
    };

    std::vector<Cell> cells(const Line& line)
    {
        std::vector<Cell> result;
        std::size_t start = 0;
        for (std::size_t position = 0; position <= line.text.size(); ++position)
        {
            if (position != line.text.size() &&
                (line.text[position] != '|' || (position > 0 && line.text[position - 1] == '\\')))
                continue;
            auto text = line.text.substr(start, position - start);
            const auto leading = text.size();
            while (!text.empty() && space(text.front()))
                text.remove_prefix(1);
            const auto left = start + leading - text.size();
            while (!text.empty() && space(text.back()))
                text.remove_suffix(1);
            result.push_back({line.content_start + left, line.content_start + left + text.size(), text});
            start = position + 1;
        }
        if (!result.empty() && result.front().text.empty() && !trimmed(line.text).empty() &&
            trimmed(line.text).front() == '|')
            result.erase(result.begin());
        if (!result.empty() && result.back().text.empty() && !trimmed(line.text).empty() &&
            trimmed(line.text).back() == '|' &&
            (trimmed(line.text).size() == 1 || trimmed(line.text)[trimmed(line.text).size() - 2] != '\\'))
            result.pop_back();
        return result;
    }

    bool delimiter(const Cell& cell)
    {
        auto text = cell.text;
        if (!text.empty() && text.front() == ':')
            text.remove_prefix(1);
        if (!text.empty() && text.back() == ':')
            text.remove_suffix(1);
        return !text.empty() &&
            std::all_of(text.begin(), text.end(), [](char character)
        {
            return character == '-';
        });
    }

    bool block_start(const Line& line)
    {
        const auto text = trimmed(line.text);
        if (line.literal || line.list_marker || text.empty())
            return true;
        if (text.front() == '#')
        {
            const auto last = text.find_first_not_of('#');
            if (last != std::string_view::npos && last <= 6 && space(text[last]))
                return true;
        }
        if (text.front() == '>')
            return true;
        if (text.front() == '<' && text.find('@') == std::string_view::npos &&
            text.find("://") == std::string_view::npos && text.substr(0, 8) != "<mailto:")
            return true;
        if (text.front() == '-' || text.front() == '*' || text.front() == '_')
        {
            const char marker = text.front();
            const auto count = std::count(text.begin(), text.end(), marker);
            if (count >= 3 &&
                std::all_of(text.begin(), text.end(), [marker](char character)
            {
                return character == marker || space(character);
            }))
                return true;
        }
        return false;
    }

}

namespace mirrorfly::detail
{
    MarkdownEdit align_markdown_source_table(
        const std::string& source, std::size_t start, std::size_t end, const MarkdownOptions& options)
    {
        if (options.table_column < 0 || options.table_column >= 32 ||
            (options.table_alignment != "left" && options.table_alignment != "center" &&
                options.table_alignment != "right" && options.table_alignment != "default"))
            return {};
        for (const auto& table : markdown_tables(source))
        {
            if (start < table.start || start > table.end || end > table.end)
                continue;
            if (static_cast<std::size_t>(options.table_column) >= table.columns.size())
                return {};
            const auto& column = table.columns[static_cast<std::size_t>(options.table_column)];
            auto hyphens = std::string_view(source).substr(
                column.delimiter_start, column.delimiter_end - column.delimiter_start);
            if (hyphens.front() == ':')
                hyphens.remove_prefix(1);
            if (hyphens.back() == ':')
                hyphens.remove_suffix(1);
            MarkdownEdit edit;
            edit.start = column.delimiter_start;
            edit.end = column.delimiter_end;
            edit.replacement =
                (options.table_alignment == "left" || options.table_alignment == "center" ? ":" : "") +
                std::string(hyphens) +
                (options.table_alignment == "right" || options.table_alignment == "center" ? ":" : "");
            edit.selection_end = edit.replacement.size();
            edit.valid = true;
            return edit;
        }
        return {};
    }

    std::string markdown_line_ending(const std::string& source)
    {
        const auto position = source.find_first_of("\r\n");

        if (position != std::string::npos && source[position] == '\r')
        {
            return position + 1 < source.size() && source[position + 1] == '\n' ? "\r\n" : "\r";
        }

        return "\n";
    }

    MarkdownEdit insert_markdown_source_table(const std::string& source, std::size_t start, std::size_t end,
        const mirrorfly::MarkdownOptions& options)
    {
        mirrorfly::MarkdownEdit edit;
        edit.start = start;
        edit.end = end;
        const int columns = std::clamp(options.table_columns, 1, 12);
        const int rows = std::clamp(options.table_rows, 1, 20);
        const auto line_ending = markdown_line_ending(source);
        edit.replacement = source.substr(start, end - start);

        if ((!edit.replacement.empty() && !line_break(edit.replacement.back())) ||
            (edit.replacement.empty() && start > 0 && !line_break(source[start - 1])))
        {
            edit.replacement += line_ending;
        }

        if (start > 0 || end > start)
        {
            edit.replacement += line_ending;
        }

        for (int column = 0; column < columns; ++column)
        {
            edit.replacement += "| ";

            if (column == 0)
            {
                edit.selection_start = edit.replacement.size();
            }

            edit.replacement += u8"列" + std::to_string(column + 1);

            if (column == 0)
            {
                edit.selection_end = edit.replacement.size();
            }

            edit.replacement += ' ';
        }

        edit.replacement += '|' + line_ending;

        for (int column = 0; column < columns; ++column)
        {
            edit.replacement += "| --- ";
        }

        edit.replacement += '|' + line_ending;

        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                edit.replacement += row == 0 && column == 0 ? u8"| 内容 " : "|  ";
            }

            edit.replacement += '|' + line_ending;
        }

        if (end < source.size() && !line_break(source[end]))
        {
            edit.replacement += line_ending;
        }

        edit.valid = true;
        return edit;
    }
}

namespace mirrorfly
{
    std::vector<MarkdownTableInfo> markdown_tables(const std::string& source)
    {
        if (source.size() > maximum_text_bytes || source.find(char(0)) != std::string::npos ||
            !utf8::is_valid(source.begin(), source.end()))
            return {};
        std::vector<MarkdownTableInfo> result;
        const auto lines = source_lines(source);
        for (std::size_t index = 1; index < lines.size(); ++index)
        {
            const auto& header = lines[index - 1];
            const auto& row = lines[index];
            if (header.literal || row.literal || header.text.empty() ||
                (block_start(header) && !header.list_marker) || row.quotes != header.quotes ||
                row.list_indent != header.list_indent || header.text.find('|') == std::string_view::npos)
                continue;
            const auto columns = cells(row);
            if (columns.empty() || columns.size() > 32 || cells(header).size() != columns.size() ||
                !std::all_of(columns.begin(), columns.end(), delimiter))
                continue;
            std::size_t next = index + 1;
            for (; next < lines.size() && lines[next].quotes == header.quotes &&
                lines[next].list_indent == header.list_indent && !block_start(lines[next]);
                ++next)
            {
            }
            MarkdownTableInfo table;
            table.start = header.start;
            table.end = lines[next - 1].end;
            table.rows = static_cast<int>(next - index);
            table.quote_level = header.quotes;
            table.list_indent = std::max(0, header.list_indent);
            table.list_quote_level = header.list_quote_level;
            table.list_owner_start = header.list_owner_start;
            if (table.rows > 128)
                continue;
            for (const auto& column : columns)
            {
                const bool left = column.text.front() == ':';
                const bool right = column.text.back() == ':';
                std::string alignment = "default";
                if (left && right)
                    alignment = "center";
                else if (left)
                    alignment = "left";
                else if (right)
                    alignment = "right";
                table.columns.push_back({column.start, column.end, alignment});
            }
            result.push_back(std::move(table));
            index = next - 1;
        }
        return result;
    }
}
