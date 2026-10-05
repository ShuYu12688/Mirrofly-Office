#include "markdown_code.hpp"

#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

namespace mirrorfly
{
    PreparedMarkdown separate_fences(const QString& source)
    {
        PreparedMarkdown prepared;
        auto lines = source.split(u'\n');
        const QString prefix = QStringLiteral("mirrorflyFence") + QUuid::createUuid().toString(QUuid::Id128);
        static const QRegularExpression fence_line(
            QStringLiteral("^((?:[ \\t>]|(?:[-+*]|[0-9]{1,9}[.)])[ \\t]+)*)(`{3,}|~{3,})(.*)$"));
        QChar active_character;
        int active_length = 0;
        int active_quotes = 0;
        int index = 0;
        for (auto& line : lines)
        {
            const auto match = fence_line.match(line);
            if (!match.hasMatch())
            {
                continue;
            }
            const auto indentation = match.captured(1);
            const auto fence = match.captured(2);
            const auto suffix = match.captured(3);
            const int quotes = static_cast<int>(indentation.count(u'>'));
            if (active_length > 0)
            {
                if (fence.front() == active_character && fence.size() >= active_length &&
                    suffix.trimmed().isEmpty() && quotes == active_quotes)
                {
                    active_length = 0;
                }
                continue;
            }
            if (fence.front() == u'`' && suffix.contains(u'`'))
            {
                continue;
            }
            active_character = fence.front();
            active_length = static_cast<int>(fence.size());
            active_quotes = quotes;
            const QString identifier = prefix + QString::number(index++) + QStringLiteral("End");
            prepared.languages.insert(identifier, language_name(suffix.trimmed()));
            prepared.original_suffixes.insert(identifier, suffix);
            line = indentation + fence + identifier;
        }
        prepared.source = lines.join(u'\n');
        return prepared;
    }

    void restore_literal_fences(QTextDocument& document, const PreparedMarkdown& prepared)
    {
        for (auto iterator = prepared.original_suffixes.cbegin();
            iterator != prepared.original_suffixes.cend(); ++iterator)
        {
            QTextCursor cursor(&document);
            while (!(cursor = document.find(iterator.key(), cursor)).isNull())
            {
                cursor.insertText(iterator.value());
            }
        }
    }

}
