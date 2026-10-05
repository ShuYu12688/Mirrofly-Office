#include "markdown_document.hpp"
#include "markdown_blocks.hpp"
#include "markdown_break.hpp"
#include "markdown_code.hpp"
#include "markdown_image.hpp"
#include "markdown_inline.hpp"
#include "markdown_input.hpp"
#include "markdown_link.hpp"
#include "markdown_rule.hpp"
#include "markdown_table.hpp"

#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <algorithm>

namespace
{
    QVariantMap result_for(const QTextCursor& cursor, bool handled = true)
    {
        return {{QStringLiteral("valid"), true}, {QStringLiteral("selectionStart"), cursor.selectionStart()},
            {QStringLiteral("selectionEnd"), cursor.selectionEnd()}, {QStringLiteral("handled"), handled}};
    }

}

namespace mirrorfly
{
    QVariantMap edit_markdown_document(QTextDocument& document, int start, int end, const QString& action,
        const QVariantMap& options, const QVariantMap& theme)
    {
        const auto boundary = [&document](int position)
        {
            const int end = document.characterCount() - 1;
            return position >= 0 && position <= end &&
                (position == 0 || position == end || !document.characterAt(position - 1).isHighSurrogate() ||
                    !document.characterAt(position).isLowSurrogate());
        };
        if (!boundary(start) || !boundary(end))
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("请重新选择编辑位置。")}};
        }
        QTextCursor cursor(&document);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        if (cursor.hasComplexSelection())
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("请先选中同一个表格单元格内的内容。")}};
        }
        auto* frame = code_frame(cursor);
        auto* table = cursor.currentTable();
        if (action == QStringLiteral("enter") && !table)
        {
            if (frame || cursor.hasSelection())
            {
                return result_for(cursor, false);
            }
            cursor.beginEditBlock();
            const bool handled =
                continue_markdown_heading(cursor, theme) || convert_explicit_markdown_block(cursor, theme);
            cursor.endEditBlock();
            return result_for(cursor, handled);
        }
        if (table &&
            (action == QStringLiteral("heading") || action == QStringLiteral("paragraph") ||
                action == QStringLiteral("bullet") || action == QStringLiteral("ordered") ||
                action == QStringLiteral("task") || action == QStringLiteral("listIndent") ||
                action == QStringLiteral("listOutdent") || action == QStringLiteral("taskSet") ||
                action == QStringLiteral("code") || action == QStringLiteral("table")))
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"),
                    QStringLiteral(
                        "Markdown "
                        "单元格支持文字、加粗、斜体、删除线、行内代码和链接；请先退出表格再插入其他结构。")}};
        }
        const auto region_error = markdown_selection_region_error(cursor);
        if (!region_error.isEmpty())
            return {{QStringLiteral("valid"), false}, {QStringLiteral("error"), region_error}};
        if (frame && action != QStringLiteral("codeLanguage") && action != QStringLiteral("exitCode") &&
            action != QStringLiteral("quoteSet") && action != QStringLiteral("quote"))
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"),
                    QStringLiteral("代码内容可直接输入；先退出代码块再插入其他结构。")}};
        }
        if (action == "inlineCode" && cursor.selectedText().contains(QChar::ObjectReplacementCharacter))
            return {{"valid", false}, {"error", QStringLiteral("请先选择图片之外的文字设置行内代码。")}};
        if ((action == "inlineCode" || action == "removeInlineCode") &&
            document.findBlock(cursor.selectionStart()) != document.findBlock(cursor.selectionEnd()))
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("请在同一段落中选择行内代码。")}};
        if (action == "hardBreak" && !can_insert_markdown_hard_break(cursor))
            return {
                {"valid", false}, {"error", QStringLiteral("请在正文、引用或列表的非代码文字内插入换行。")}};
        if (action == "thematicBreak" && !can_insert_markdown_rule(cursor))
            return {{"valid", false}, {"error", QStringLiteral("请在非代码、非表格段落中插入分隔线。")}};
        if (action == "heading" || action == "paragraph")
            for (const auto& block : selected_markdown_blocks(cursor))
                if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
                    return {{"valid", false}, {"error", QStringLiteral("请先在分隔线外选择文字段落。")}};
        if (action == "heading")
        {
            const auto value = options.value("headingLevel", 2);
            bool valid = false;
            const int level = value.toInt(&valid);
            if (!valid || value.metaType().id() == QMetaType::Bool ||
                value.metaType().id() == QMetaType::QString || value.toDouble() != level || level < 1 ||
                level > 6)
                return {{"valid", false}, {"error", QStringLiteral("请选择一到六级标题。")}};
            for (const auto& block : selected_markdown_blocks(cursor))
            {
                if (block.textList() &&
                    block.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker)
                    return {{"valid", false},
                        {"error", QStringLiteral("待办首段保留复选框，请在独立续段中设置标题。")}};
                if (block.text().contains(QChar::LineSeparator))
                    return {{"valid", false},
                        {"error", QStringLiteral("请保留段内换行，或先拆成独立段落再设标题。")}};
            }
        }
        cursor.beginEditBlock();
        if (action == "image" || action == "removeImage")
        {
            if (!edit_markdown_image(cursor, action, options, theme))
            {
                cursor.endEditBlock();
                return {{"valid", false},
                    {"error", QStringLiteral("请在同一段落内选择图片或插入位置，并填写单行图片信息。")}};
            }
        }
        else if (action == "thematicBreak")
        {
            insert_markdown_rule(cursor, theme);
        }
        else if (action == "hardBreak")
        {
            auto original = cursor.charFormat();
            original.setFontFixedPitch(false);
            original.setFontFamilies({theme.value("fontFamily").toString()});
            original.setBackground(QBrush(Qt::NoBrush));
            auto format = original;
            format.setProperty(markdown_hard_break_property, true);
            cursor.insertText(QString(QChar::LineSeparator), format);
            cursor.setCharFormat(original);
        }
        else if (action == QStringLiteral("link") || action == QStringLiteral("unlink"))
        {
            if (!edit_markdown_link(cursor, action, options, theme))
            {
                cursor.endEditBlock();
                return {{QStringLiteral("valid"), false},
                    {QStringLiteral("error"),
                        QStringLiteral("请填写有效的单行链接地址，并在同一个链接或段落内编辑。")}};
            }
        }
        else if (action == QStringLiteral("bold") || action == QStringLiteral("italic") ||
            action == QStringLiteral("strike"))
        {
            apply_markdown_inline_style(cursor, action);
        }
        else if (action == "inlineCode" || action == "removeInlineCode")
        {
            if (!edit_markdown_inline_code(cursor, action, theme))
            {
                cursor.endEditBlock();
                return {{"valid", false}, {"error", QStringLiteral("请先在行内代码内定位或选择代码文字。")}};
            }
        }
        else if (action == QStringLiteral("quote") || action == QStringLiteral("quoteSet"))
        {
            const auto value = options.value("quoteLevel");
            bool valid_level = action == QStringLiteral("quote");
            const int level = valid_level ? 1 : value.toInt(&valid_level);
            const bool typed = action == QStringLiteral("quote") ||
                (value.metaType().id() != QMetaType::Bool && value.metaType().id() != QMetaType::QString &&
                    value.toDouble() == level);
            if (!valid_level || !typed || !set_markdown_quote(cursor, level, theme))
            {
                cursor.endEditBlock();
                return {{QStringLiteral("valid"), false},
                    {QStringLiteral("error"),
                        QStringLiteral("请选择有效的引用层级；表格不能低于父引用层级。")}};
            }
        }
        else if (action == QStringLiteral("heading") || action == QStringLiteral("paragraph"))
        {
            apply_markdown_block(cursor, action, options, theme);
        }
        else if (action == QStringLiteral("bullet") || action == QStringLiteral("ordered") ||
            action == QStringLiteral("task"))
        {
            apply_markdown_list(cursor, action);
            refresh_markdown_table_containers(document, theme);
        }
        else if (action == QStringLiteral("listIndent") || action == QStringLiteral("listOutdent") ||
            action == QStringLiteral("taskSet"))
        {
            if (!edit_markdown_list(cursor, action, options))
            {
                cursor.endEditBlock();
                return {{QStringLiteral("valid"), false},
                    {QStringLiteral("error"), QStringLiteral("请选择可编辑的列表层级或已有待办状态。")}};
            }
            refresh_markdown_table_containers(document, theme);
        }
        else if (action == QStringLiteral("code"))
        {
            insert_markdown_code_frame(cursor, options, theme);
        }
        else if (action == QStringLiteral("codeLanguage") && frame)
        {
            style_code_frame(frame, options.value(QStringLiteral("language")).toString(), theme);
        }
        else if (action == QStringLiteral("exitCode") && frame)
        {
            leave_markdown_frame(cursor, frame, theme);
        }
        else if (action == QStringLiteral("table") && !table)
        {
            insert_markdown_table(cursor, options, theme);
        }
        else if (table &&
            (action == QStringLiteral("rowAdd") || action == QStringLiteral("rowRemove") ||
                action == QStringLiteral("columnAdd") || action == QStringLiteral("columnRemove")))
        {
            resize_markdown_table(cursor, action, theme);
        }
        else if (action == QStringLiteral("tableAlign") && table)
        {
            if (!align_markdown_table(cursor, options))
            {
                cursor.endEditBlock();
                return {{QStringLiteral("valid"), false},
                    {QStringLiteral("error"), QStringLiteral("请选择有效的表格列和对齐方式。")}};
            }
        }
        else if (action == QStringLiteral("exitTable") && table)
        {
            leave_markdown_frame(cursor, table, theme);
        }
        else if (table &&
            (action == QStringLiteral("tableNextCell") || action == QStringLiteral("tablePreviousCell") ||
                action == QStringLiteral("tableNextRow") || action == QStringLiteral("enter")))
        {
            if (!navigate_markdown_table(cursor, action, theme))
            {
                cursor.endEditBlock();
                return {{QStringLiteral("valid"), false},
                    {QStringLiteral("error"),
                        QStringLiteral("表格最多支持 128 行；可使用退出表格继续写作。")}};
            }
        }
        else
        {
            cursor.endEditBlock();
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("当前光标位置不能执行这项操作。")}};
        }
        cursor.endEditBlock();
        return result_for(cursor);
    }
}
