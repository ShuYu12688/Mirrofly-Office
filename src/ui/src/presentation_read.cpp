#include "document_read.hpp"
#include "presentation_bridge.hpp"
#include "presentation_semantics.hpp"

namespace mirrorfly
{
    QVariantMap PresentationBridge::readContent(const QVariantMap& query) const
    {
        const DocumentReadQuery read(query);
        if (!read.valid)
            return read_error("invalid_read_query");
        if (!scene_)
            return read_error("no_document");
        const int page_count = static_cast<int>(scene_->slides.size());
        if (read.view == "overview")
        {
            QVariantList pages;
            const int end = std::min(page_count, read.offset + std::min(read.limit, 16));
            for (int index = read.offset; index < end; ++index)
            {
                const auto& slide = scene_->slides[index];
                QString title = QString::fromStdString(slide.title);
                for (const auto& shape : slide.shapes)
                {
                    QString candidate;
                    for (const auto& paragraph : shape.text.paragraphs)
                    {
                        for (const auto& run : paragraph.runs)
                            candidate += QString::fromStdString(run.text);
                        candidate += QLatin1Char(' ');
                    }
                    if (!candidate.trimmed().isEmpty())
                    {
                        title = candidate.trimmed();
                        break;
                    }
                }
                if (!append_read_item(pages,
                        {{"index", index}, {"title", read_preview(title)},
                            {"objects", static_cast<int>(slide.shapes.size())}}))
                    break;
            }
            auto result = read_items(pages, read.offset, page_count);
            result.insert("width", scene_->width);
            result.insert("height", scene_->height);
            result.insert("coordinates", "pt");
            result.insert("current", current_slide_);
            result.insert("generation", editGeneration());
            result.insert("views", QStringList{"overview", "content", "text", "notes", "format"});
            return result;
        }
        const int page = read.index < 0 ? current_slide_ : read.index;
        if (page < 0 || page >= page_count)
            return read_error("invalid_index");
        const auto& slide = scene_->slides[page];
        if (read.view == "format")
        {
            for (std::size_t index = 0; index < slide.shapes.size(); ++index)
            {
                if (QString::number(slide.shapes[index].id) != read.id)
                    continue;
                const auto tree = presentation_semantic_tree(*scene_, page, static_cast<int>(index), 1);
                for (const auto& value : tree.value("nodes").toList())
                {
                    const auto node = value.toMap();
                    if (node.value("id").toString() != read.id)
                        continue;
                    QVariantMap result{{"ok", true}, {"id", read.id}, {"index", page},
                        {"generation", editGeneration()}, {"format", node.value("style")},
                        {"scope", "Object paint and first text run only; not a uniform text range."}};
                    const QStringList fields{"textStyleSources", "textLocalOverrides", "effectStyle",
                        "placeholder", "tableCellStyle", "writable", "lockedReason"};
                    for (const auto& field : fields)
                        if (node.contains(field))
                            result.insert(field, node.value(field));
                    return result;
                }
            }
            return read_error("object_not_found");
        }
        if (read.view == "notes")
            return read_text(QString::fromStdString(slide.speaker_notes), read.offset,
                query.contains("limit") ? read.limit : 1600);
        if (read.view == "text")
        {
            for (const auto& shape : slide.shapes)
            {
                if (QString::number(shape.id) != read.id)
                    continue;
                QString text;
                bool first = true;
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    if (!first)
                        text += '\n';
                    first = false;
                    for (const auto& run : paragraph.runs)
                        text += QString::fromStdString(run.text);
                }
                return read_text(text, read.offset, read.limit);
            }
            return read_error("object_not_found");
        }
        if (read.view != "content")
            return read_error("unsupported_view");
        const auto tree = presentation_semantic_tree(*scene_, page, read.offset, std::min(read.limit, 8));
        if (!tree.value("ok").toBool())
            return tree;
        QVariantList items;
        for (const auto& value : tree.value("nodes").toList())
        {
            const auto node = value.toMap();
            if (!node.contains("index"))
                continue;
            QVariantMap item;
            const QStringList fields{
                "id", "index", "type", "width", "height", "transform", "actions", "writable", "lockedReason"};
            for (const auto& key : fields)
                item.insert(key, node.value(key));
            item.insert("textPreview", read_preview(node.value("text").toString(), 240));
            if (!append_read_item(items, item))
                break;
        }
        auto result = read_items(items, read.offset, static_cast<int>(slide.shapes.size()));
        result.insert("index", page);
        result.insert("generation", editGeneration());
        result.insert("textQuery", "view=text,index=page,id=object ID; offset is UTF-16");
        return result;
    }
}
