#include "markdown_table.hpp"
#include "markdown_container.hpp"
#include "markdown_table_format.hpp"

#include <mirrorfly/markdown.hpp>

#include <QColor>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace
{
    using mirrorfly::detail::quote_prefix;
    constexpr int column_alignment_property = QTextFormat::UserProperty + 62;
    constexpr int table_quote_property = QTextFormat::UserProperty + 63;
    constexpr int owner_token_property = QTextFormat::UserProperty + 64;
    constexpr int table_list_indent_property = QTextFormat::UserProperty + 65;

    QList<QTextTable*> tables_in(QTextFrame* frame)
    {
        QList<QTextTable*> tables;
        for (auto* child : frame->childFrames())
        {
            if (auto* table = qobject_cast<QTextTable*>(child))
                tables.append(table);
            tables.append(tables_in(child));
        }
        return tables;
    }

    bool valid_alignment(const QString& value)
    {
        return value == "default" || value == "left" || value == "center" || value == "right";
    }

    QTextBlock table_owner(QTextTable* table)
    {
        const auto token = table->format().stringProperty(owner_token_property);
        if (token.isEmpty())
            return {};
        for (auto block = table->document()->begin(); block.isValid(); block = block.next())
            if (block.blockFormat().stringProperty(owner_token_property) == token)
                return block;
        return {};
    }

    int owner_indent(const QTextBlock& owner)
    {
        auto* list = owner.textList();
        if (!list)
            return 0;
        const auto format = list->format();
        if (format.style() != QTextListFormat::ListDecimal)
            return format.indent() * 2;
        const int ordinal = format.start() + list->itemNumber(owner);
        const int marker = QString::number(ordinal).size() +
            (format.numberSuffix().isEmpty() ? 1 : format.numberSuffix().size()) + 1;
        return (format.indent() - 1) * 4 + std::max(4, marker);
    }

    QString table_prefix(QTextTable* table)
    {
        const int quotes = table->format().intProperty(table_quote_property);
        const auto owner = table_owner(table);
        if (!owner.isValid())
            return quote_prefix(quotes) +
                QString(table->format().intProperty(table_list_indent_property), u' ');
        const int inherited = std::min(quotes, owner.blockFormat().intProperty(QTextFormat::BlockQuoteLevel));
        return quote_prefix(inherited) + QString(owner_indent(owner), u' ') +
            quote_prefix(quotes - inherited);
    }

    void set_column_alignment(QTextTable* table, int column, const QString& alignment)
    {
        for (int row = 0; row < table->rows(); ++row)
        {
            auto cursor = table->cellAt(row, column).firstCursorPosition();
            auto format = cursor.blockFormat();
            format.setProperty(column_alignment_property, alignment);
            auto flags = Qt::AlignLeft;
            if (alignment == "center")
                flags = Qt::AlignHCenter;
            else if (alignment == "right")
                flags = Qt::AlignRight;
            format.setAlignment(flags);
            cursor.setBlockFormat(format);
        }
    }

}

namespace mirrorfly
{
    PreparedMarkdownTables prepare_markdown_table_containers(const QString& source)
    {
        PreparedMarkdownTables prepared{source, {}};
        auto bytes = source.toUtf8().toStdString();
        const auto tables = markdown_tables(bytes);
        std::vector<std::size_t> owners;
        static const QRegularExpression empty_item(
            QStringLiteral("^[ \\t>]*(?:[-+*]|[0-9]{1,9}[.)])[ \\t]*$"));
        for (auto item = tables.rbegin(); item != tables.rend(); ++item)
        {
            if (item->list_indent == 0 ||
                std::find(owners.begin(), owners.end(), item->list_owner_start) != owners.end())
                continue;
            const auto start = item->list_owner_start;
            const auto ending = bytes.find_first_of("\r\n", start);
            const auto end = ending == std::string::npos ? bytes.size() : ending;
            const auto line = QString::fromStdString(bytes.substr(start, end - start));
            const bool inline_header = start == item->start;
            if (!inline_header && !empty_item.match(line).hasMatch())
                continue;
            owners.push_back(start);
            const auto token =
                QStringLiteral("MIRRORFLYTABLEOWNER") + QUuid::createUuid().toString(QUuid::Id128);
            prepared.owner_tokens.append(token);
            QString replacement;
            if (inline_header)
            {
                const auto pipe = line.indexOf(u'|');
                if (pipe < 0)
                    continue;
                const auto outer = quote_prefix(item->list_quote_level);
                replacement = line.left(pipe) + token + u'\n' + outer + u'\n' + outer +
                    QString(item->list_indent, u' ') +
                    quote_prefix(item->quote_level - item->list_quote_level) + line.mid(pipe);
            }
            else
            {
                replacement = line + u' ' + token;
                auto next = end;
                if (next < bytes.size() && bytes[next] == '\r')
                    ++next;
                if (next < bytes.size() && bytes[next] == '\n')
                    ++next;
                if (next == item->start)
                    replacement += '\n' + quote_prefix(item->list_quote_level);
            }
            bytes.replace(start, end - start, replacement.toUtf8().toStdString());
        }
        prepared.source = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
        return prepared;
    }

    void preserve_markdown_empty_blocks(QTextDocument& copy, const QTextDocument& original)
    {
        // QTextDocument::clone drops custom properties/list membership on empty owner paragraphs.
        for (auto block = original.begin(); block.isValid(); block = block.next())
        {
            if (!block.text().isEmpty() ||
                (block.blockFormat().headingLevel() == 0 &&
                    block.blockFormat().property(markdown_container_property).toList().isEmpty() &&
                    (!block.textList() ||
                        (block.blockFormat().stringProperty(owner_token_property).isEmpty() &&
                            !block.blockFormat().boolProperty(markdown_container_head_property)))))
                continue;
            QTextCursor cursor(copy.findBlock(block.position()));
            cursor.setBlockFormat(block.blockFormat());
            if (auto* inherited = cursor.block().textList())
                inherited->remove(cursor.block());
            if (!block.textList())
                continue;
            auto format = block.textList()->format();
            format.setStart(format.start() + block.textList()->itemNumber(block));
            cursor.createList(format);
        }
    }

    QString markdown_table_alignment(QTextTable* table, int column)
    {
        if (!table || column < 0 || column >= table->columns())
            return {};
        const auto value = table->cellAt(0, column).firstCursorPosition().blockFormat().stringProperty(
            column_alignment_property);
        return valid_alignment(value) ? value : QStringLiteral("default");
    }

    void load_markdown_table_alignments(QTextDocument& document, const QString& source)
    {
        const auto metadata = markdown_tables(source.toUtf8().toStdString());
        const auto tables = tables_in(document.rootFrame());
        for (qsizetype index = 0; index < tables.size(); ++index)
        {
            auto* table = tables[index];
            if (static_cast<std::size_t>(index) < metadata.size())
            {
                const auto& item = metadata[static_cast<std::size_t>(index)];
                auto format = table->format();
                format.setProperty(table_quote_property, item.quote_level);
                format.setProperty(table_list_indent_property, item.list_indent);
                if (item.list_indent > 0)
                {
                    auto owner = document.findBlock(table->firstPosition()).previous();
                    while (owner.isValid() &&
                        (!owner.textList() ||
                            owner.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) !=
                                item.list_quote_level))
                        owner = owner.previous();
                    if (owner.isValid())
                    {
                        QTextCursor cursor(owner);
                        auto block_format = owner.blockFormat();
                        auto token = block_format.stringProperty(owner_token_property);
                        if (token.isEmpty())
                            token = QUuid::createUuid().toString(QUuid::Id128);
                        block_format.setProperty(owner_token_property, token);
                        cursor.setBlockFormat(block_format);
                        format.setProperty(owner_token_property, token);
                    }
                }
                table->setFormat(format);
            }
            for (int column = 0; column < table->columns(); ++column)
            {
                QString alignment = QStringLiteral("default");
                if (static_cast<std::size_t>(index) < metadata.size() &&
                    metadata[static_cast<std::size_t>(index)].columns.size() ==
                        static_cast<std::size_t>(table->columns()))
                    alignment = QString::fromStdString(metadata[static_cast<std::size_t>(index)]
                            .columns[static_cast<std::size_t>(column)]
                            .alignment);
                else if (table->rows() > 1)
                {
                    const auto flags =
                        table->cellAt(1, column).firstCursorPosition().blockFormat().alignment();
                    if (flags.testFlag(Qt::AlignHCenter))
                        alignment = "center";
                    else if (flags.testFlag(Qt::AlignRight))
                        alignment = "right";
                }
                set_column_alignment(table, column, alignment);
            }
        }
    }

    bool align_markdown_table(QTextCursor& cursor, const QVariantMap& options)
    {
        auto* table = cursor.currentTable();
        const auto alignment = options.value("alignment").toString();
        if (!table || !valid_alignment(alignment))
            return false;
        const auto current = table->cellAt(cursor);
        bool valid_column = true;
        const int column =
            options.contains("column") ? options.value("column").toInt(&valid_column) : current.column();
        if (options.contains("column"))
        {
            const auto value = options.value("column");
            valid_column = valid_column && value.metaType().id() != QMetaType::Bool &&
                value.metaType().id() != QMetaType::QString && value.toDouble() == column;
        }
        if (!valid_column || column < 0 || column >= table->columns())
            return false;
        set_column_alignment(table, column, alignment);
        return true;
    }

    int markdown_table_quote_level(QTextTable* table)
    {
        return table ? table->format().intProperty(table_quote_property) : 0;
    }

    int markdown_table_quote_minimum(QTextTable* table)
    {
        if (!table)
            return 0;
        const auto owner = table_owner(table);
        return owner.textList() && !owner.text().isEmpty()
            ? owner.blockFormat().intProperty(QTextFormat::BlockQuoteLevel)
            : 0;
    }

    bool set_markdown_table_quote(QTextTable* table, int level, const QVariantMap& theme)
    {
        if (!table || level < markdown_table_quote_minimum(table) || level > 8)
            return false;
        const auto owner = table_owner(table);
        if (owner.textList() && owner.text().isEmpty())
        {
            QTextCursor cursor(owner);
            auto block_format = owner.blockFormat();
            block_format.setProperty(QTextFormat::BlockQuoteLevel, level);
            cursor.setBlockFormat(block_format);
        }
        auto format = table->format();
        format.setProperty(table_quote_property, level);
        table->setFormat(format);
        style_markdown_table(table, theme);
        return true;
    }

    bool markdown_table_owned_by(QTextTable* table, const QTextBlock& owner)
    {
        const auto token = owner.blockFormat().stringProperty(owner_token_property);
        return !token.isEmpty() && table->format().stringProperty(owner_token_property) == token;
    }

    bool can_change_markdown_owner_quote(const QTextBlock& owner, int previous, int level)
    {
        const auto token = owner.blockFormat().stringProperty(owner_token_property);
        if (token.isEmpty())
            return true;
        for (auto* table : tables_in(owner.document()->rootFrame()))
            if (table->format().stringProperty(owner_token_property) == token)
            {
                const int changed = markdown_table_quote_level(table) + level - previous;
                if (changed < 0 || changed > 8)
                    return false;
            }
        return true;
    }

    void change_markdown_owner_quote(
        const QTextBlock& owner, int previous, int level, const QVariantMap& theme)
    {
        const auto token = owner.blockFormat().stringProperty(owner_token_property);
        if (token.isEmpty())
            return;
        for (auto* table : tables_in(owner.document()->rootFrame()))
            if (table->format().stringProperty(owner_token_property) == token)
                set_markdown_table_quote(table, markdown_table_quote_level(table) + level - previous, theme);
    }

    void refresh_markdown_table_containers(QTextDocument& document, const QVariantMap& theme)
    {
        for (auto* table : tables_in(document.rootFrame()))
            style_markdown_table(table, theme);
    }

    void style_markdown_table(QTextTable* table, const QVariantMap& theme)
    {
        auto format = table->format();
        format.setHeaderRowCount(1);
        format.setBorder(1);
        format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        format.setBorderBrush(QColor(theme.value("borderColor").toString()));
        format.setCellPadding(9);
        format.setCellSpacing(0);
        format.setTopMargin(12);
        format.setBottomMargin(12);
        const auto owner = table_owner(table);
        const int list_level = owner.textList() ? owner.textList()->format().indent() : 0;
        format.setLeftMargin(
            markdown_table_quote_level(table) * theme.value("markdownQuoteIndent", 18).toInt() +
            list_level * table->document()->indentWidth());
        format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        QList<QTextLength> widths;
        for (int column = 0; column < table->columns(); ++column)
        {
            widths.append(QTextLength(QTextLength::PercentageLength, 100.0 / table->columns()));
            auto cell = table->cellAt(0, column);
            auto cell_format = cell.format();
            cell_format.setBackground(QColor(theme.value("accentSoft").toString()));
            cell.setFormat(cell_format);
            set_column_alignment(table, column, markdown_table_alignment(table, column));
        }
        format.setColumnWidthConstraints(widths);
        table->setFormat(format);
    }

    std::vector<MarkdownTableToken> protect_markdown_table_alignments(QTextDocument& document)
    {
        std::vector<MarkdownTableToken> result;
        const auto tables = tables_in(document.rootFrame());
        for (auto* table : tables)
        {
            MarkdownTableToken item;
            item.token = QStringLiteral("MIRRORFLYTABLE") + QUuid::createUuid().toString(QUuid::Id128);
            item.rows = table->rows();
            item.prefix = table_prefix(table);
            const auto owner = table_owner(table);
            if (owner.textList() && owner.text().isEmpty())
            {
                // Qt omits an empty list paragraph; keep its marker until table prefixes are restored.
                item.owner_token =
                    QStringLiteral("MIRRORFLYEMPTYOWNER") + QUuid::createUuid().toString(QUuid::Id128);
                QTextCursor(owner).insertText(item.owner_token, QTextCharFormat{});
            }
            item.outer_quotes = owner.isValid()
                ? owner.blockFormat().intProperty(QTextFormat::BlockQuoteLevel)
                : markdown_table_quote_level(table);
            for (int column = 0; column < table->columns(); ++column)
                item.alignments.append(markdown_table_alignment(table, column));
            table->cellAt(0, 0).firstCursorPosition().insertText(item.token, QTextCharFormat{});
            result.push_back(std::move(item));
        }
        return result;
    }

    QString markdown_table_structure_error(QTextFrame* frame)
    {
        for (auto* child : frame->childFrames())
        {
            if (auto* table = qobject_cast<QTextTable*>(child))
            {
                if (table->rows() > 128 || table->columns() > 32)
                {
                    return QStringLiteral("表格超出可视编辑范围；请撤销最近的操作后再保存。");
                }
                for (int row = 0; row < table->rows(); ++row)
                {
                    for (int column = 0; column < table->columns(); ++column)
                    {
                        const auto cell = table->cellAt(row, column);
                        if (cell.firstCursorPosition().block() != cell.lastCursorPosition().block() ||
                            cell.rowSpan() != 1 || cell.columnSpan() != 1)
                        {
                            return QStringLiteral(
                                "Markdown 表格只支持单段、未合并的单元格；请撤销最近的操作后再保存。");
                        }
                    }
                }
            }
            const auto error = markdown_table_structure_error(child);
            if (!error.isEmpty())
            {
                return error;
            }
        }
        return {};
    }

    void insert_markdown_table(QTextCursor& cursor, const QVariantMap& options, const QVariantMap& theme)
    {
        cursor.clearSelection();
        const int rows = std::clamp(options.value(QStringLiteral("rows"), 3).toInt(), 1, 20) + 1;
        const int columns = std::clamp(options.value(QStringLiteral("columns"), 3).toInt(), 1, 12);
        auto* inserted = cursor.insertTable(rows, columns);
        style_markdown_table(inserted, theme);
        for (int column = 0; column < columns; ++column)
        {
            inserted->cellAt(0, column).firstCursorPosition().insertText(
                QStringLiteral("列%1").arg(column + 1));
        }
        cursor = inserted->cellAt(0, 0).firstCursorPosition();
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }

    void resize_markdown_table(QTextCursor& cursor, const QString& action, const QVariantMap& theme)
    {
        auto* table = cursor.currentTable();
        const auto cell = table->cellAt(cursor);
        const int row = cell.row();
        const int column = cell.column();
        if (action == QStringLiteral("rowAdd") && table->rows() < 128)
        {
            table->insertRows(row + 1, 1);
        }
        else if (action == QStringLiteral("rowRemove") && table->rows() > 2)
        {
            table->removeRows(row, 1);
        }
        else if (action == QStringLiteral("columnAdd") && table->columns() < 32)
        {
            table->insertColumns(column + 1, 1);
        }
        else if (action == QStringLiteral("columnRemove") && table->columns() > 1)
        {
            table->removeColumns(column, 1);
        }
        style_markdown_table(table, theme);
        const auto destination =
            table->cellAt(std::min(row, table->rows() - 1), std::min(column, table->columns() - 1));
        cursor = destination.firstCursorPosition();
    }
    void pad_markdown_table_cells(QTextFrame* frame, QChar marker)
    {
        for (auto* child : frame->childFrames())
        {
            if (auto* table = qobject_cast<QTextTable*>(child))
            {
                for (int row = 0; row < table->rows(); ++row)
                {
                    for (int column = 0; column < table->columns(); ++column)
                    {
                        auto cursor = table->cellAt(row, column).firstCursorPosition();
                        if (cursor.block().text().isEmpty())
                        {
                            cursor.insertText(QString(3, marker), QTextCharFormat{});
                        }
                    }
                }
            }
            pad_markdown_table_cells(child, marker);
        }
    }

    bool navigate_markdown_table(QTextCursor& cursor, const QString& action, const QVariantMap& theme)
    {
        auto* table = cursor.currentTable();
        const auto cell = table->cellAt(cursor);
        int target = cell.row() * table->columns() + cell.column();
        if (action == QStringLiteral("tablePreviousCell"))
        {
            target = std::max(0, target - 1);
        }
        else if (action == QStringLiteral("tableNextRow") || action == QStringLiteral("enter"))
        {
            target += table->columns();
        }
        else
        {
            ++target;
        }
        if (target >= table->rows() * table->columns())
        {
            if (table->rows() >= 128)
            {
                return false;
            }
            table->appendRows(1);
            style_markdown_table(table, theme);
        }
        cursor = table->cellAt(target / table->columns(), target % table->columns()).firstCursorPosition();
        return true;
    }
}
