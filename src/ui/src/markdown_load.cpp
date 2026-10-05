#include "markdown_break.hpp"
#include "markdown_code.hpp"
#include "markdown_document.hpp"
#include "markdown_image.hpp"
#include "markdown_inline.hpp"
#include "markdown_input.hpp"
#include "markdown_link.hpp"
#include "markdown_paragraph.hpp"
#include "markdown_table.hpp"

#include <QFont>
#include <QImage>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

namespace
{
    class EditorDocument final : public QTextDocument
    {
    protected:
        QVariant loadResource(int type, const QUrl&) override
        {
            if (type == ImageResource)
            {
                QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                return image;
            }
            return QByteArray{};
        }
    };

    void style_frames(QTextFrame* frame, const QVariantMap& theme)
    {
        for (auto* child : frame->childFrames())
        {
            if (auto* table = qobject_cast<QTextTable*>(child))
            {
                mirrorfly::style_markdown_table(table, theme);
            }
            style_frames(child, theme);
        }
    }

}

namespace mirrorfly
{
    std::unique_ptr<QTextDocument> create_editor_document()
    {
        return std::make_unique<EditorDocument>();
    }

    QString load_markdown_document(QTextDocument& document, const QString& source, const QVariantMap& theme)
    {
        QFont font(theme.value(QStringLiteral("fontFamily")).toString());
        font.setPixelSize(theme.value(QStringLiteral("editorFontSize"), 16).toInt());
        document.setDefaultFont(font);
        document.setDocumentMargin(4);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document.setDefaultTextOption(option);
        const auto images = prepare_markdown_images(source);
        const auto paragraphs = prepare_markdown_paragraphs(images.source);
        const auto links = prepare_markdown_links(paragraphs.source);
        const auto breaks = prepare_markdown_hard_breaks(links.source);
        const auto containers = prepare_markdown_table_containers(breaks.source);
        const auto prepared = separate_fences(containers.source);
        document.setMarkdown(prepared.source,
            QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) |
                QTextDocument::MarkdownNoHTML);
        restore_literal_fences(document, prepared);
        if (!restore_markdown_link_import(document, links, theme))
            return QStringLiteral("链接文本暂时无法完整对应到编辑区；原始内容已保留。");
        if (!restore_markdown_paragraphs(document, paragraphs, theme))
            return QStringLiteral("段落文字暂时无法完整对应到编辑区；原始内容已保留。");
        if (!restore_markdown_hard_break_import(document, breaks))
            return QStringLiteral("换行文本暂时无法完整对应到编辑区；原始内容已保留。");
        for (const auto& token : containers.owner_tokens)
        {
            auto cursor = document.find(token);
            if (!cursor.isNull())
                cursor.removeSelectedText();
        }
        const auto ranges = code_ranges(document);
        for (auto iterator = ranges.rbegin(); iterator != ranges.rend(); ++iterator)
        {
            QTextCursor cursor(&document);
            const auto following = document.findBlock(iterator->end).next();
            if (following.isValid() && QTextCursor(following).currentTable())
            {
                // Keep insertFrame's closing boundary outside the following table's first cell.
                cursor.setPosition(iterator->end);
                cursor.insertBlock(QTextBlockFormat{}, QTextCharFormat{});
            }
            cursor.setPosition(iterator->start);
            cursor.setPosition(iterator->end, QTextCursor::KeepAnchor);
            auto* frame = cursor.insertFrame(frame_style(theme));
            const auto language = prepared.languages.value(iterator->language, iterator->language);
            style_code_frame(frame, language, theme, iterator->quote_level);
        }
        load_markdown_table_alignments(document, source);
        if (!load_markdown_block_containers(document, images.source, theme))
            return QStringLiteral("块结构暂时无法完整对应到编辑区；原始内容已保留。");
        style_frames(document.rootFrame(), theme);
        style_markdown_inline_code(document, theme);
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            QTextCursor cursor(block);
            if (code_block(block) && code_frame(cursor) == nullptr && block.text().isEmpty())
            {
                normal_markdown_block(cursor, theme);
            }
            else if (!cursor.currentTable() && !code_frame(cursor))
            {
                auto format = block.blockFormat();
                format.setLeftMargin(format.intProperty(QTextFormat::BlockQuoteLevel) *
                    theme.value("markdownQuoteIndent", 18).toInt());
                cursor.setBlockFormat(format);
            }
        }
        if (!restore_markdown_images(document, images, theme))
            return QStringLiteral("图片暂时无法完整对应到编辑区；原始内容已保留。");
        document.clearUndoRedoStacks();
        document.setModified(false);
        return {};
    }

}
