#include "markdown_container.hpp"

#include <QSet>
#include <algorithm>

namespace
{
    QString prefix(const QVariantList& path, QSet<qulonglong>* opened = nullptr)
    {
        QString result;
        for (const auto& value : path)
        {
            const auto node = value.toMap();
            const auto identity = node.value("identity").toULongLong();
            const bool first = opened && !opened->contains(identity);
            result += node.value(first ? "opening" : "continuation").toString();
            if (opened)
                opened->insert(identity);
        }
        return result;
    }

    QVariantMap last_item(const QVariantList& path)
    {
        for (auto iterator = path.crbegin(); iterator != path.crend(); ++iterator)
            if (iterator->toMap().value("kind") == "listItem")
                return iterator->toMap();
        return {};
    }

    bool needs_blank(const QVariantList& previous, const QVariantList& current)
    {
        const auto a = last_item(previous);
        const auto b = last_item(current);
        if (a.isEmpty() || b.isEmpty())
            return false;
        if (a.value("listIdentity").toULongLong() != 0 && a.value("listIdentity") == b.value("listIdentity"))
            return !a.value("tight", true).toBool();
        // A non-one ordered marker cannot interrupt its parent's paragraph without a blank line.
        if (current.size() > previous.size() && !previous.isEmpty() &&
            current.mid(0, previous.size()) == previous)
            return !a.value("tight", true).toBool() ||
                (b.value("ordered").toBool() && b.value("ordinal").toUInt() != 1);
        return false;
    }

}

namespace mirrorfly
{
    bool restore_markdown_containers(QString& source, const std::vector<MarkdownContainerToken>& tokens)
    {
        QSet<qulonglong> opened;
        QVariantList previous;
        int previous_end = 0;
        bool previous_empty = false;
        for (std::size_t token_index = 0; token_index < tokens.size(); ++token_index)
        {
            const auto& token = tokens[token_index];
            const int position = static_cast<int>(source.indexOf(token.token, previous_end));
            if (position < 0)
                return false;
            const int line = position > 0 ? static_cast<int>(source.lastIndexOf('\n', position - 1) + 1) : 0;
            QVariantList common;
            for (qsizetype index = 0; index < std::min(previous.size(), token.path.size()); ++index)
            {
                if (previous[index].toMap().value("identity") != token.path[index].toMap().value("identity"))
                    break;
                common.append(token.path[index]);
            }
            auto before = source.mid(previous_end, line - previous_end);
            auto only_markers = before;
            only_markers.remove('>');
            if (only_markers.trimmed().isEmpty())
            {
                QString replacement;
                const auto blank = prefix(common);
                const auto previous_item = last_item(previous);
                const auto current_item = last_item(token.path);
                const bool tight_list = previous_item.value("listIdentity").toULongLong() != 0 &&
                    previous_item.value("listIdentity") == current_item.value("listIdentity") &&
                    previous_item.value("tight", true).toBool() && current_item.value("tight", true).toBool();
                const int preserved_lines =
                    std::max(static_cast<int>(before.count('\n')), needs_blank(previous, token.path) ? 1 : 0);
                const bool code_head = token.empty_head && token_index + 1 < tokens.size() &&
                    tokens[token_index + 1].token.startsWith("MIRRORFLYCODE") &&
                    tokens[token_index + 1].path == token.path;
                const bool empty_child = token.empty_head && !code_head &&
                    token.path.size() > previous.size() && !previous.isEmpty() &&
                    common.size() == previous.size();
                int lines = preserved_lines;
                if (empty_child)
                    lines = std::max(1, preserved_lines);
                else if ((previous_empty && !common.isEmpty()) || tight_list)
                    lines = 0;
                for (int count = lines; count > 0; --count)
                    replacement += blank + '\n';
                if (before != replacement)
                    source.replace(previous_end, line - previous_end, replacement);
            }
            const int updated = static_cast<int>(source.indexOf(token.token, previous_end));
            const int start = updated > 0 ? static_cast<int>(source.lastIndexOf('\n', updated - 1) + 1) : 0;
            auto opening = prefix(token.path, &opened);
            if (token.heading > 0)
                opening += QString(token.heading, '#') + ' ';
            if (source.mid(start, updated - start) != opening)
                source.replace(start, updated - start, opening);
            previous_end =
                static_cast<int>(source.indexOf('\n', start + opening.size() + token.token.size()));
            if (previous_end < 0)
                previous_end = static_cast<int>(source.size());
            else
                ++previous_end;
            previous = token.path;
            previous_empty = token.empty_head;
        }
        return true;
    }
}
