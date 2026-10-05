#include "canvas_bridge.hpp"
#include "document_read.hpp"

namespace mirrorfly
{
    QString CanvasBridge::editGeneration() const
    {
        return QString::number(generation_);
    }

    QString CanvasBridge::selectedId() const
    {
        return node_;
    }

    int CanvasBridge::currentPage() const
    {
        return page_;
    }

    int CanvasBridge::pageCount() const
    {
        return pdf_ && active_ ? static_cast<int>(std::get<PdfDocument>(document_).pages.size()) : 0;
    }

    QVariantMap CanvasBridge::readContent(const QVariantMap& query) const
    {
        const DocumentReadQuery read(query);
        if (!read.valid)
            return read_error("invalid_read_query");
        if (!active_)
            return read_error("no_document");
        if (pdf_)
        {
            const auto& document = std::get<PdfDocument>(document_);
            if (read.view == "overview")
            {
                QVariantList items;
                const int count = static_cast<int>(document.pages.size());
                const int end = std::min(count, read.offset + std::min(read.limit, 16));
                for (int index = read.offset; index < end; ++index)
                {
                    const auto& page = document.pages[index];
                    if (!append_read_item(items,
                            {{"index", index}, {"id", QString::fromStdString(page.id)},
                                {"width", pdf_page_display_width(page)},
                                {"height", pdf_page_display_height(page)}, {"rotation", page.rotation},
                                {"textPreview", read_preview(QString::fromStdString(page.text))}}))
                        break;
                }
                auto result = read_items(items, read.offset, count);
                result.insert("current", page_);
                result.insert("editable", document.editable);
                result.insert("views", QStringList{"overview", "text", "annotations"});
                return result;
            }
            const int index = read.index < 0 ? page_ : read.index;
            if (index < 0 || index >= static_cast<int>(document.pages.size()))
                return read_error("invalid_index");
            const auto& page = document.pages[index];
            if ((read.view == "text" || read.view == "content") && read.id.isEmpty())
            {
                auto result = read_text(QString::fromStdString(page.text), read.offset,
                    query.contains("limit") ? read.limit : 1600);
                result.insert("sourceTruncated", page.text_truncated);
                result.insert("index", index);
                return result;
            }
            std::vector<const PdfAnnotation*> annotations;
            for (const auto* list : {&page.original_annotations, &page.added_annotations})
                for (const auto& annotation : *list)
                    if (!page.removed_annotation_ids.count(annotation.id))
                        annotations.push_back(&annotation);
            if (read.view == "text")
            {
                for (const auto* annotation : annotations)
                    if (QString::fromStdString(annotation->id) == read.id)
                        return read_text(
                            QString::fromStdString(annotation->contents), read.offset, read.limit);
                return read_error("annotation_not_found");
            }
            if (read.view != "annotations")
                return read_error("unsupported_view");
            QVariantList items;
            const int count = static_cast<int>(annotations.size());
            const int end = std::min(count, read.offset + std::min(read.limit, 16));
            for (int item_index = read.offset; item_index < end; ++item_index)
            {
                const auto& annotation = *annotations[item_index];
                if (!append_read_item(items,
                        {{"id", QString::fromStdString(annotation.id)},
                            {"kind", static_cast<int>(annotation.kind)},
                            {"textPreview", read_preview(QString::fromStdString(annotation.contents), 240)}}))
                    break;
            }
            return read_items(items, read.offset, count);
        }
        const auto& graph = std::get<MindMapDocument>(document_);
        if (read.view == "overview")
            return {{"ok", true}, {"rootId", QString::fromStdString(graph.root_id)}, {"selectedId", node_},
                {"nodeCount", static_cast<int>(graph.nodes.size())},
                {"edgeCount", static_cast<int>(graph.edges.size())}, {"editable", !graph.read_only},
                {"views", QStringList{"overview", "content", "edges", "text"}}};
        if (read.view == "text")
        {
            for (const auto& node : graph.nodes)
                if (QString::fromStdString(node.id) == read.id)
                    return read_text(QString::fromStdString(node.text), read.offset, read.limit);
            for (const auto& edge : graph.edges)
                if (QString::fromStdString(edge.id) == read.id)
                    return read_text(QString::fromStdString(edge.label), read.offset, read.limit);
            return read_error("object_not_found");
        }
        if (read.view != "content" && read.view != "edges")
            return read_error("unsupported_view");
        const bool edges = read.view == "edges";
        const int count = static_cast<int>(edges ? graph.edges.size() : graph.nodes.size());
        const int end = std::min(count, read.offset + std::min(read.limit, 16));
        QVariantList items;
        for (int index = read.offset; index < end; ++index)
        {
            QVariantMap item;
            if (edges)
            {
                const auto& edge = graph.edges[index];
                item = {{"id", QString::fromStdString(edge.id)}, {"from", QString::fromStdString(edge.from)},
                    {"to", QString::fromStdString(edge.to)},
                    {"textPreview", read_preview(QString::fromStdString(edge.label))}};
            }
            else
            {
                const auto& node = graph.nodes[index];
                item = {{"id", QString::fromStdString(node.id)},
                    {"parentId", QString::fromStdString(node.parent_id)},
                    {"textPreview", read_preview(QString::fromStdString(node.text), 240)}, {"x", node.x},
                    {"y", node.y}, {"width", node.width}, {"height", node.height},
                    {"collapsed", node.collapsed}};
            }
            if (!append_read_item(items, item))
                break;
        }
        return read_items(items, read.offset, count);
    }
}
