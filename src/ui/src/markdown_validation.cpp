#include "markdown_document.hpp"
#include <QByteArray>
#include <QRegularExpression>
#include <algorithm>
#include <mirrorfly/markdown.hpp>

namespace mirrorfly
{
    QString markdown_support_error(const QString& source)
    {
        if (!source.isValidUtf16() || source.contains(QChar::Null))
        {
            return QStringLiteral("文档包含无效字符，已保留原文并暂停可视编辑。");
        }
        if (source.size() > maximum_markdown_bytes || source.toUtf8().size() > maximum_markdown_bytes)
        {
            return QStringLiteral("可视编辑支持最多 128 KiB；原文已保留，本次不会改写文件。");
        }
        for (const auto& link : markdown_links(source.toUtf8().toStdString()))
            if (link.text.find_first_of("\r") != std::string::npos ||
                markdown_link_suffix(link.url, link.title).empty())
                return QStringLiteral("链接含暂不支持可视保存的换行或地址格式；已保留原文并暂停可视编辑。");
        for (const auto& image : markdown_images(source.toUtf8().toStdString()))
            if (markdown_image_source(image.url, image.alt, image.title).empty())
                return QStringLiteral("图片说明或地址暂不支持可视保存；原始内容已保留。");
        const auto bytes = source.toUtf8();
        const auto raw = bytes.toStdString();
        auto prose_bytes = bytes;
        const auto mask = [&](std::size_t start, std::size_t end)
        {
            for (auto offset = start; offset < end; ++offset)
                if (prose_bytes[static_cast<qsizetype>(offset)] != '\r' &&
                    prose_bytes[static_cast<qsizetype>(offset)] != '\n')
                    prose_bytes[static_cast<qsizetype>(offset)] = ' ';
        };
        for (const auto& span : markdown_code_spans(raw))
            mask(span.start, span.end);
        std::size_t cells = 0;
        for (const auto& block : markdown_blocks(raw))
        {
            if (block.kind == "code")
                mask(block.start, block.end);
            if (block.kind != "table")
                continue;
            cells += block.cells.size();
            if (block.columns > 32 || cells > 4096)
                return QStringLiteral("表格结构较大，已保留原文并暂停可视编辑。");
        }
        const auto lines = source.split(u'\n');
        if (lines.size() > 4000)
            return QStringLiteral("文档行数较多，已保留原文并暂停可视编辑。");
        for (const auto& line : lines)
            if (line.size() > 8192)
                return QStringLiteral("文档单行较长，已保留原文并暂停可视编辑。");
        const auto prose = QString::fromUtf8(prose_bytes);
        const QRegularExpression footnote(
            QStringLiteral("(?m)^(?:[ \\t>]|(?:[-+*]|[0-9]{1,9}[.)])[ \\t]+)*\\[\\^[^\\]\\r\\n]+\\]:"));
        if (markdown_has_raw_html(raw) || prose.contains(footnote) ||
            prose.contains(QRegularExpression(QStringLiteral("\\A---\\s*\\n[^\\n]*:[\\s\\S]*?\\n---"))))
        {
            return QStringLiteral("文档包含HTML、脚注定义或前置元数据；为保留这些内容，本次暂停可视编辑。");
        }
        return {};
    }

}
