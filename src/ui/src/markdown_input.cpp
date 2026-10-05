#include "markdown_input.hpp"
#include "markdown_blocks.hpp"

#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

#include <algorithm>

namespace
{
    QColor color(const QVariantMap& theme, const char* key)
    {
        return QColor(theme.value(QString::fromLatin1(key)).toString());
    }
}

namespace mirrorfly
{
    bool continue_markdown_heading(QTextCursor& cursor, const QVariantMap& theme)
    {
        if (cursor.hasSelection() || cursor.blockFormat().headingLevel() == 0 || !cursor.atBlockEnd())
            return false;
        if (!cursor.block().text().isEmpty())
            cursor.insertBlock();
        apply_markdown_block(cursor, "paragraph", {}, theme);
        QTextCharFormat format;
        format.setFontFamilies({theme.value("fontFamily").toString()});
        format.setProperty(QTextFormat::FontPixelSize, theme.value("editorFontSize", 16).toInt());
        format.setForeground(QColor(theme.value("textPrimary").toString()));
        cursor.setBlockCharFormat(format);
        cursor.setCharFormat(format);
        return true;
    }

    void normal_markdown_block(QTextCursor& cursor, const QVariantMap& theme)
    {
        QTextBlockFormat block;
        block.setBottomMargin(10);
        block.setLineHeight(145, QTextBlockFormat::ProportionalHeight);
        cursor.setBlockFormat(block);
        QTextCharFormat format;
        format.setFontFamilies({theme.value(QStringLiteral("fontFamily")).toString()});
        format.setProperty(QTextFormat::FontPixelSize, theme.value(QStringLiteral("editorFontSize"), 16));
        format.setForeground(color(theme, "textPrimary"));
        cursor.setCharFormat(format);
    }

    void leave_markdown_frame(QTextCursor& cursor, QTextFrame* frame, const QVariantMap& theme)
    {
        auto* document = cursor.document();
        cursor.setPosition(std::min(document->characterCount() - 1, frame->lastPosition() + 1));
        if (cursor.currentFrame() == frame || !cursor.block().text().isEmpty())
        {
            cursor.insertBlock();
            cursor.movePosition(QTextCursor::PreviousBlock);
        }
        normal_markdown_block(cursor, theme);
    }

    QString markdown_selection_region_error(QTextCursor cursor)
    {
        auto& document = *cursor.document();
        auto* table = cursor.currentTable();
        if (cursor.hasSelection())
        {
            for (auto block = document.findBlock(cursor.selectionStart());
                block.isValid() && block.position() < cursor.selectionEnd(); block = block.next())
            {
                if (QTextCursor(block).currentFrame() != cursor.currentFrame())
                {
                    return QStringLiteral("请在同一个段落区域、代码块或表格单元格内操作。");
                }
                if (table)
                {
                    const auto first = table->cellAt(cursor.selectionStart());
                    const auto last = table->cellAt(cursor.selectionEnd() - 1);
                    if (first.row() != last.row() || first.column() != last.column())
                    {
                        return QStringLiteral("请先选中同一个表格单元格内的内容。");
                    }
                }
            }
        }
        return {};
    }
}
