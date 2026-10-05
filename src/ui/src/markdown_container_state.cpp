#include "markdown_container.hpp"

#include <QTextCursor>
#include <QTextList>
#include <algorithm>
#include <limits>

namespace
{
    int count(const QVariantList& path, const QString& kind)
    {
        return static_cast<int>(std::count_if(path.begin(), path.end(), [&](const QVariant& value)
        {
            return value.toMap().value("kind") == kind;
        }));
    }

    int item_index(const QVariantList& path)
    {
        for (int index = static_cast<int>(path.size()) - 1; index >= 0; --index)
            if (path[index].toMap().value("kind") == "listItem")
                return index;
        return -1;
    }
}

namespace mirrorfly
{
    QVariantList markdown_container_values(const std::vector<MarkdownContainerInfo>& containers)
    {
        QVariantList path;
        for (const auto& container : containers)
            path.append(QVariantMap{{"identity", QVariant::fromValue<qulonglong>(container.identity)},
                {"kind", QString::fromStdString(container.kind)},
                {"opening", QString::fromStdString(container.opening)},
                {"continuation", QString::fromStdString(container.continuation)},
                {"ordered", container.ordered}, {"ordinal", container.ordinal},
                {"delimiter", QString(QChar::fromLatin1(container.delimiter))},
                {"listIdentity", QVariant::fromValue<qulonglong>(container.list_identity)},
                {"tight", container.tight}});
        return path;
    }

    void set_markdown_containers(const QTextBlock& block, const MarkdownParagraphInfo& paragraph)
    {
        QTextCursor cursor(block);
        auto format = block.blockFormat();
        format.setProperty(QTextFormat::BlockQuoteLevel, paragraph.quote_level);
        format.setProperty(markdown_container_property, markdown_container_values(paragraph.containers));
        format.setProperty(markdown_container_head_property, block.textList() != nullptr);
        if (!block.textList())
            format.setIndent(paragraph.list_depth);
        cursor.setBlockFormat(format);
        if (auto* list = block.textList(); list && !paragraph.containers.empty())
        {
            if (list->format().indent() != paragraph.list_depth)
            {
                auto corrected = list->format();
                corrected.setIndent(paragraph.list_depth);
                list->remove(block);
                cursor.createList(corrected);
                list = block.textList();
            }
            const auto& item = paragraph.containers.back();
            if (item.kind == "listItem" && item.ordered)
            {
                auto list_format = list->format();
                list_format.setNumberSuffix(QString(QChar::fromLatin1(item.delimiter)));
                list->setFormat(list_format);
            }
        }
    }

    QVariantList MarkdownContainerState::path(const QTextBlock& block)
    {
        return path(block, block.blockFormat().property(markdown_container_property).toList(),
            block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel));
    }

    QVariantList MarkdownContainerState::path(const QTextBlock& block, QVariantList result, int quote)
    {
        const int original_depth = count(result, "listItem");
        for (int index = static_cast<int>(result.size()) - 1; index >= 0; --index)
        {
            const auto id = result[index].toMap().value("identity").toULongLong();
            if (result[index].toMap().value("kind") == "listItem" && owners_.contains(id))
            {
                const auto tail = result.mid(index + 1);
                result = owners_.value(id);
                result.append(tail);
                break;
            }
        }
        const int original_quote = count(result, "quote");
        for (int index = original_quote; index < quote; ++index)
        {
            const QVariantMap node{{"kind", "quote"}, {"opening", "> "}, {"continuation", "> "},
                {"identity",
                    QVariant::fromValue<qulonglong>(std::numeric_limits<qulonglong>::max() - index)}};
            if (!block.textList() && count(result, "listItem") > 0)
                result.append(node);
            else
            {
                const int own = item_index(result);
                result.insert(own >= 0 ? own : result.size(), node);
            }
        }
        for (int remaining = original_quote - quote, index = 0; remaining > 0 && index < result.size();)
            if (result[index].toMap().value("kind") == "quote")
            {
                result.removeAt(index);
                --remaining;
            }
            else
                ++index;
        auto* list = block.textList();
        if (!list)
        {
            if (block.blockFormat().boolProperty(markdown_container_head_property) &&
                block.blockFormat().indent() == 0)
            {
                const int own_index = item_index(result);
                const auto identity =
                    own_index >= 0 ? result[own_index].toMap().value("identity").toULongLong() : 0;
                for (qsizetype index = result.size(); index > 0; --index)
                    if (result[index - 1].toMap().value("kind") == "listItem")
                        result.removeAt(index - 1);
                if (identity != 0)
                    owners_.insert(identity, result);
            }
            return result;
        }
        auto own = item_index(result) >= 0 ? result[item_index(result)].toMap() : QVariantMap{};
        auto identity = own.value("identity").toULongLong();
        const auto inherited = identity;
        const bool duplicate = identity != 0 && item_heads_.contains(identity);
        if (identity == 0 || duplicate)
            identity = (qulonglong{1} << 63) + static_cast<qulonglong>(block.position());
        item_heads_.insert(identity);
        const int depth = list->format().indent();
        const bool fresh = own.isEmpty();
        if (fresh)
            own = {{"kind", "listItem"}, {"identity", QVariant::fromValue(identity)}};
        if ((depth != original_depth && depth != count(result, "listItem")) || fresh)
        {
            QVariantList parent;
            if (depth > 1)
                for (auto iterator = heads_.rbegin(); iterator != heads_.rend(); ++iterator)
                    if (count(*iterator, "listItem") == depth - 1 && count(*iterator, "quote") == quote)
                    {
                        parent = *iterator;
                        break;
                    }
            if (parent.isEmpty())
                for (const auto& node : result)
                    if (node.toMap().value("kind") == "quote")
                        parent.append(node);
            result = parent;
            if (!own.isEmpty())
                result.append(own);
        }
        const bool ordered = list->format().style() == QTextListFormat::ListDecimal;
        const int number = list->format().start() + list->itemNumber(block);
        const auto suffix = list->format().numberSuffix() == ")" ? QStringLiteral(")") : QStringLiteral(".");
        const auto marker = !own.value("ordered").toBool() &&
                QStringLiteral("-+*").contains(own.value("delimiter").toString()) &&
                own.value("delimiter").toString().size() == 1
            ? own.value("delimiter").toString()
            : QStringLiteral("-");
        // CommonMark accepts at most nine source digits, even when the semantic counter grows larger.
        auto opening = ordered ? QString::number(std::min(number, 999999999)) + suffix + " " : marker + " ";
        const auto continuation = QString(opening.size(), ' ');
        if (block.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked)
            opening += "[x] ";
        else if (block.blockFormat().marker() == QTextBlockFormat::MarkerType::Unchecked)
            opening += "[ ] ";
        own = {{"kind", "listItem"}, {"identity", QVariant::fromValue(identity)}, {"opening", opening},
            {"continuation", continuation}, {"ordered", ordered}, {"ordinal", number},
            {"delimiter", ordered ? suffix : marker}, {"listIdentity", own.value("listIdentity")},
            {"tight", own.value("tight", true)}};
        const int index = item_index(result);
        if (index >= 0)
            result[index] = own;
        else
            result.append(own);
        owners_.insert(identity, result);
        if (duplicate)
            owners_.insert(inherited, result);
        heads_.push_back(result);
        return result;
    }

}
