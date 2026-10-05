#include "markdown_table.hpp"
#include "markdown_table_format.hpp"

#include <algorithm>
#include <mirrorfly/markdown.hpp>

namespace mirrorfly
{
    using detail::delimiter_text;
    using detail::quote_prefix;
    bool restore_markdown_table_alignments(QString& source, const std::vector<MarkdownTableToken>& tokens,
        std::vector<MarkdownInlineToken>* bodies)
    {
        if (tokens.empty())
            return true;
        // Qt 6.8.3 writes neither alignment colons nor the delimiter for a header-only table.
        for (const auto& item : tokens)
        {
            if (item.rows != 1)
                continue;
            const auto position = source.indexOf(item.token);
            if (position < 0)
                return false;
            const auto end = source.indexOf(u'\n', position);
            if (end < 0)
                return false;
            QString delimiter = QStringLiteral("\n|");
            for (const auto& alignment : item.alignments)
                delimiter += delimiter_text(alignment) + u'|';
            source.insert(end, delimiter);
        }
        struct Replacement
        {
            std::size_t start;
            std::size_t end;
            std::string text;
        };
        auto bytes = source.toUtf8().toStdString();
        std::vector<Replacement> edits;
        for (const auto& item : tokens)
        {
            const auto position = bytes.find(item.token.toStdString());
            if (position == std::string::npos)
                return false;
            const auto previous = bytes.rfind('\n', position);
            const auto begin = previous == std::string::npos ? 0 : previous + 1;
            const auto first_end = bytes.find('\n', position);
            if (first_end == std::string::npos)
                return false;
            const auto second_end = bytes.find('\n', first_end + 1);
            const auto end = second_end == std::string::npos ? bytes.size() : second_end;
            // Inspect only the two structural lines, not transient UUIDs throughout the whole document.
            const auto header_pipe = bytes.find('|', begin);
            const auto delimiter_pipe = bytes.find('|', first_end + 1);
            if (header_pipe == std::string::npos || header_pipe >= first_end ||
                delimiter_pipe == std::string::npos || delimiter_pipe >= end)
                return false;
            const auto header = bytes.substr(header_pipe, first_end - header_pipe);
            const auto tables =
                markdown_tables(header + "\n" + bytes.substr(delimiter_pipe, end - delimiter_pipe));
            if (tables.size() != 1 ||
                tables.front().columns.size() != static_cast<std::size_t>(item.alignments.size()))
                return false;
            for (qsizetype column = 0; column < item.alignments.size(); ++column)
            {
                const auto& entry = tables.front().columns[static_cast<std::size_t>(column)];
                const auto start = delimiter_pipe + entry.delimiter_start - header.size() - 1;
                const auto finish = delimiter_pipe + entry.delimiter_end - header.size() - 1;
                const auto value = bytes.substr(start, finish - start);
                const auto hyphens = QString::fromStdString(value).remove(u':');
                edits.push_back(
                    {start, finish, delimiter_text(item.alignments[column], hyphens).toStdString()});
            }
        }
        std::sort(edits.begin(), edits.end(), [](const auto& first, const auto& second)
        {
            return first.start > second.start;
        });
        for (const auto& edit : edits)
            bytes.replace(edit.start, edit.end - edit.start, edit.text);
        source = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
        for (const auto& item : tokens)
        {
            const auto position = source.indexOf(item.token);
            if (position < 0)
                return false;
            const auto begin = source.lastIndexOf(u'\n', position - 1) + 1;
            auto end = begin;
            QString replacement;
            for (int row = 0; row < item.rows + 1; ++row)
            {
                const auto newline = source.indexOf(u'\n', end);
                if (newline < 0)
                    return false;
                const auto line = source.mid(end, newline - end);
                const auto pipe = line.indexOf(u'|');
                if (pipe < 0)
                    return false;
                replacement += (bodies ? QString{} : item.prefix) + line.mid(pipe) + u'\n';
                end = newline + 1;
            }
            if (bodies)
            {
                replacement.replace("**" + item.token + "**", QString{});
                replacement.replace(item.token, QString{});
                replacement.chop(1);
                bodies->push_back({item.token, replacement});
                source.replace(begin, end - begin, item.token + '\n');
                if (!item.owner_token.isEmpty())
                    bodies->push_back({item.owner_token, {}});
                continue;
            }
            source.replace(begin, end - begin, replacement);
            // Native table output drops quote prefixes, including the blank lines adjoining the table.
            if (item.outer_quotes > 0)
            {
                const auto blank_prefix = quote_prefix(item.outer_quotes);
                auto scan = begin;
                auto following = begin + replacement.size();
                while (scan > 0)
                {
                    const auto previous = scan >= 2 ? source.lastIndexOf(u'\n', scan - 2) + 1 : 0;
                    const auto text = source.mid(previous, scan - previous - 1).trimmed();
                    if (!text.isEmpty())
                        break;
                    if (previous == 0)
                        break;
                    source.insert(previous, blank_prefix);
                    following += blank_prefix.size();
                    scan = previous;
                }
                const auto first_blank = following;
                while (following < source.size())
                {
                    const auto newline = source.indexOf(u'\n', following);
                    if (newline < 0 || !source.mid(following, newline - following).trimmed().isEmpty())
                        break;
                    following = newline + 1;
                }
                if (source.mid(following).startsWith(blank_prefix.trimmed()))
                {
                    for (auto line = following; line > first_blank;)
                    {
                        const auto previous = line >= 2 ? source.lastIndexOf(u'\n', line - 2) + 1 : 0;
                        source.insert(previous, blank_prefix);
                        line = previous;
                    }
                }
            }
            source.replace("**" + item.token + "**", QString{});
            source.replace(item.token, QString{});
            if (!item.owner_token.isEmpty())
                source.replace(item.owner_token, QString{});
        }
        return true;
    }

}
