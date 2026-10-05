#include "markdown_code.hpp"
#include "markdown_code_serialization.hpp"
#include "markdown_document.hpp"
#include "markdown_image.hpp"
#include "markdown_inline_serialization.hpp"
#include "markdown_link_serialization.hpp"
#include "markdown_paragraph.hpp"
#include "markdown_rule.hpp"
#include "markdown_table.hpp"

#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <memory>
#include <vector>

namespace mirrorfly
{
    QString markdown_serialization_markers(const QTextDocument& document)
    {
        const auto text = document.toRawText();
        QString markers;
        for (ushort point = 0xE000; point <= 0xF8FF && markers.size() < 5; ++point)
        {
            const QChar marker(point);
            if (!text.contains(marker))
            {
                markers += marker;
            }
        }
        return markers;
    }

    MarkdownSerialization serialize_markdown_document(const QTextDocument& document)
    {
        const auto error = markdown_table_structure_error(document.rootFrame());
        if (!error.isEmpty())
        {
            return {false, {}, error};
        }
        std::unique_ptr<QTextDocument> copy(document.clone());
        std::size_t expected_breaks = 0;
        const auto original_raw = document.toRawText();
        for (auto position = original_raw.indexOf(QChar::LineSeparator); position >= 0;
            position = original_raw.indexOf(QChar::LineSeparator, position + 1))
        {
            QTextCursor cursor(copy.get());
            cursor.setPosition(position);
            cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
            if (cursor.charFormat().boolProperty(markdown_hard_break_property))
                ++expected_breaks;
        }
        preserve_markdown_empty_blocks(*copy, document);
        const auto markers = markdown_serialization_markers(document);
        if (markers.size() != 5)
        {
            return {false, {}, QStringLiteral("这份文档包含较多特殊字符；请撤销最近的操作后再保存。")};
        }
        const auto images = protect_markdown_images(*copy);
        const auto cells = protect_markdown_paragraphs(*copy, markers, true);
        const auto paragraphs = protect_markdown_paragraphs(*copy, markers);
        if (!cells.valid || !paragraphs.valid)
            return {false, {}, QStringLiteral("段落样式暂时无法完整保存；请撤销最近的操作后重试。")};
        const auto empty_heads = protect_markdown_empty_heads(*copy);
        const auto tables = protect_markdown_table_alignments(*copy);
        const auto code = protect_markdown_code_blocks(*copy);
        const auto rules = protect_markdown_rules(*copy);
        const auto links = protect_markdown_links(*copy);
        if (!links.valid)
            return {false, {}, QStringLiteral("链接文本暂时无法完整保存；请撤销最近的操作后重试。")};
        const auto inline_codes = protect_markdown_code_spans(*copy);
        const auto breaks = protect_markdown_hard_breaks(*copy, markers);
        QStringList identifiers;
        for (const auto& head : empty_heads)
            identifiers.append(head.token);
        for (const auto& token : code.tokens)
            identifiers.append(token);
        for (const auto& paragraph : paragraphs.tokens)
            identifiers.append(paragraph.token);
        for (const auto& rule : rules)
            identifiers.append(rule.token);
        for (const auto& table : tables)
        {
            identifiers.append(table.token);
            if (!table.owner_token.isEmpty())
                identifiers.append(table.owner_token);
        }
        const auto containers = collect_markdown_containers(*copy, identifiers);
        if (containers.size() != static_cast<std::size_t>(identifiers.size()))
            return {false, {}, QStringLiteral("容器结构暂时无法完整保存；请撤销最近的操作后重试。")};
        // Keep blank table columns valid when the native writer sizes their delimiter runs.
        pad_markdown_table_cells(copy->rootFrame(), markers[4]);
        // Preserve semantic emphasis even when the platform font has no bold or italic face.
        prepare_markdown_inline_formats(*copy, markers);
        QString markdown = copy->toMarkdown(QTextDocument::MarkdownDialectGitHub);
        auto block_bodies = empty_heads;
        const auto code_bodies = markdown_code_bodies(code);
        block_bodies.insert(block_bodies.end(), code_bodies.begin(), code_bodies.end());
        if (!restore_markdown_table_alignments(markdown, tables, &block_bodies))
            return {false, {}, QStringLiteral("表格列对齐暂时无法保存；请撤销最近的操作后重试。")};

        if (!restore_markdown_containers(markdown, containers) ||
            !restore_markdown_block_tokens(markdown, block_bodies, containers))
            return {false, {}, QStringLiteral("块结构暂时无法完整保存；请撤销最近的操作后重试。")};
        markdown.replace(markers[0], QStringLiteral("**"));
        markdown.replace(markers[1], QStringLiteral("*"));
        markdown.replace(markers[2], QStringLiteral("\\|"));
        markdown.replace(markers[3], QChar(0x00A0));
        markdown.replace(markers[4], QString{});
        if (!restore_markdown_links(markdown, links.tokens) ||
            !restore_markdown_inline_tokens(markdown, breaks) ||
            !restore_markdown_inline_tokens(markdown, rules) ||
            !restore_markdown_inline_tokens(markdown, paragraphs.tokens) ||
            !restore_markdown_inline_tokens(markdown, cells.tokens))
            return {false, {}, QStringLiteral("换行文本暂时无法完整保存；请撤销最近的操作后重试。")};
        for (const auto& span : inline_codes)
            markdown.replace(span.token, span.source);
        if (!restore_markdown_image_tokens(markdown, images))
            return {false, {}, QStringLiteral("图片信息暂时无法完整保存；请撤销最近的操作后重试。")};
        if (!markdown.isValidUtf16() || markdown.contains(QChar::Null))
        {
            return {false, {}, QStringLiteral("内容包含无效字符；请撤销最近的操作后再保存。")};
        }
        if (markdown.toUtf8().size() > maximum_markdown_bytes)
        {
            return {false, {}, QStringLiteral("可视编辑最多支持 128 KiB；请撤销最近增加的内容后再保存。")};
        }
        if (markdown_hard_breaks(markdown.toUtf8().toStdString()).size() != expected_breaks)
            return {false, {}, QStringLiteral("请在换行后输入续文，或撤销末尾换行后再保存。")};
        const auto restored_images = markdown_images(markdown.toUtf8().toStdString());
        if (restored_images.size() != images.size())
            return {false, {}, QStringLiteral("图片数量暂时无法完整保存；请撤销最近的操作后重试。")};
        for (std::size_t index = 0; index < images.size(); ++index)
        {
            const auto expected = markdown_images(images[images.size() - 1 - index].source.toStdString());
            if (expected.size() != 1 || restored_images[index].url != expected[0].url ||
                restored_images[index].alt != expected[0].alt ||
                restored_images[index].title != expected[0].title)
                return {false, {}, QStringLiteral("图片说明暂时无法完整保存；请撤销最近的操作后重试。")};
        }
        const auto blocks = markdown_blocks(markdown.toUtf8().toStdString());
        std::size_t code_index = 0;
        std::size_t rule_count = 0;
        for (const auto& block : blocks)
        {
            rule_count += block.kind == "thematicBreak" ? 1 : 0;
            if (block.kind != "code")
                continue;
            if (code_index >= code.ranges.size())
                return {false, {}, QStringLiteral("代码块暂时无法完整保存；请撤销最近的操作后重试。")};
            const auto& range = code.ranges[code_index++];
            const auto expected = range.text.isEmpty() ? QString{} : range.text + '\n';
            if (QString::fromStdString(block.text) != expected ||
                QString::fromStdString(block.language) != language_name(range.language))
                return {false, {}, QStringLiteral("代码内容暂时无法完整保存；请撤销最近的操作后重试。")};
        }
        if (code_index != code.ranges.size())
            return {false, {}, QStringLiteral("代码块暂时无法完整保存；请撤销最近的操作后重试。")};
        if (rule_count != rules.size())
            return {false, {}, QStringLiteral("分隔线暂时无法完整保存；请撤销最近的操作后重试。")};
        return {true, markdown, {}};
    }

}
