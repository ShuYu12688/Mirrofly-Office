#include "document_read.hpp"
#include "word_bridge.hpp"

#include <QTextBlock>

namespace mirrorfly
{
    QVariantMap WordBridge::readContent(const QVariantMap& query) const
    {
        const DocumentReadQuery read(query);
        if (!read.valid)
            return read_error("invalid_read_query");
        if (!active_ || !editor_)
            return read_error("editor_not_ready");
        if (read.view == "overview")
            return {{"ok", true}, {"characters", editor_->characterCount() - 1},
                {"paragraphs", editor_->blockCount()}, {"readOnly", read_only_},
                {"views", QStringList{"overview", "content", "text", "format"}}, {"unit", "UTF-16"}};
        if (read.view == "text" || read.view == "format")
        {
            const int index = read.index < 0 ? 0 : read.index;
            const auto block = editor_->findBlockByNumber(index);
            if (!block.isValid())
                return read_error("invalid_paragraph_index");
            if (read.view == "format")
            {
                const QString text = block.text();
                if (read.offset > text.size() ||
                    (read.offset < text.size() && text.at(read.offset).isLowSurrogate()))
                    return read_error("invalid_text_offset");
                const int position = block.position() + read.offset;
                const auto preview = read_preview(text, 240);
                return {{"ok", true}, {"index", index}, {"offset", read.offset}, {"position", position},
                    {"start", block.position()}, {"end", block.position() + text.size()},
                    {"textPreview", preview}, {"textComplete", preview.size() == text.size()},
                    {"format", inspect(position)}, {"unit", "UTF-16"},
                    {"scope", "Single position plus its paragraph properties; not a uniform range."}};
            }
            auto result = read_text(block.text(), read.offset, read.limit);
            if (result.value("ok").toBool())
            {
                result.insert("start", block.position() + read.offset);
                result.insert("end", block.position() + read.offset + result.value("text").toString().size());
                result.insert("index", index);
                result.insert("textComplete", read.offset == 0 && result.value("nextOffset").toInt() == -1);
                result.insert("nextIndex", index + 1 < editor_->blockCount() ? index + 1 : -1);
            }
            return result;
        }
        if (read.view != "content")
            return read_error("unsupported_view");
        QVariantList items;
        auto block = editor_->findBlockByNumber(read.offset);
        for (; block.isValid() && items.size() < std::min(read.limit, 16); block = block.next())
        {
            const auto text = block.text();
            const auto preview = read_preview(text, 240);
            if (!append_read_item(items,
                    {{"index", block.blockNumber()}, {"start", block.position()},
                        {"end", block.position() + block.text().size()}, {"textPreview", preview},
                        {"textComplete", preview.size() == text.size()},
                        {"heading", block.blockFormat().headingLevel()}}))
                break;
        }
        auto result = read_items(items, read.offset, editor_->blockCount());
        result.insert(
            "textQuery", "view=text,index=paragraph index; returned start/end are editor positions");
        return result;
    }

}
