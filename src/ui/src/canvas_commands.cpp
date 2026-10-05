#include "canvas_bridge.hpp"
#include "pdf_stamp.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace mirrorfly
{
    bool CanvasBridge::execute(const QString& action, const QVariantMap& args)
    {
        if (!active_ || locked() || args.size() > 16)
            return false;
        QStringList allowed;
        if (pdf_)
        {
            allowed = {"pageId"};
            if (action == "rotate")
                allowed += "turns";
            else if (action == "movePage")
                allowed += "index";
            else if (action == "deleteAnnotation")
                allowed += "id";
            else if (action == "note" || action == "highlight" || action == "label" ||
                action == "watermark" || action == "image")
                allowed += QStringList{"text", "x", "y", "width", "height"};
            if (action == "label" || action == "watermark" || action == "image")
                allowed += QStringList{"file", "font", "size", "color", "opacity"};
            const bool needs_position = action == "note" || action == "highlight" || action == "label" ||
                action == "watermark" || action == "image";
            if ((action == "movePage" && !args.contains("index")) ||
                (action == "deleteAnnotation" && !args.contains("id")) ||
                (needs_position && (!args.contains("x") || !args.contains("y"))))
            {
                error(QStringLiteral("操作缺少必要参数。"));
                return false;
            }
        }
        else
        {
            allowed = {"id"};
            if (action == "autoLayout")
                allowed.clear();
            if (action == "rename" || action == "addChild" || action == "addSibling")
                allowed += "text";
            if (action == "addChild" || action == "addSibling")
                allowed += "newId";
            if (action == "reparent")
                allowed += "parentId";
            if (action == "createNode" || action == "addChild" || action == "addSibling")
                allowed += QStringList{"x", "y", "newId", "text"};
            if (action == "moveNode")
                allowed += QStringList{"x", "y"};
            if (action == "styleNode")
                allowed += QStringList{"width", "height", "border", "fill", "shape", "stroke"};
            if (action == "connect")
                allowed += QStringList{"toId", "newId", "text"};
            if ((action == "rename" && !args.contains("text")) ||
                (action == "reparent" && !args.contains("parentId")))
            {
                error(QStringLiteral("操作缺少必要参数。"));
                return false;
            }
            if ((action == "moveNode" && (!args.contains("x") || !args.contains("y"))) ||
                (action == "connect" && !args.contains("toId")))
                return false;
        }
        const QStringList numbers{"x", "y", "width", "height", "turns", "index", "stroke", "size", "opacity"};
        for (auto value = args.cbegin(); value != args.cend(); ++value)
        {
            const bool numeric = numbers.contains(value.key());
            const int type = value->metaType().id();
            const bool valid_number = type == QMetaType::Int || type == QMetaType::UInt ||
                type == QMetaType::Double || type == QMetaType::LongLong;
            if (!allowed.contains(value.key()) ||
                (numeric ? !valid_number || !std::isfinite(value->toDouble()) : type != QMetaType::QString) ||
                ((value.key() == "index" || value.key() == "turns") &&
                    (value->toDouble() != std::floor(value->toDouble()) ||
                        std::abs(value->toDouble()) > 100000)))
            {
                error(QStringLiteral("操作参数名称或类型无效。"));
                return false;
            }
        }
        bool committed = false;
        try
        {
            auto candidate = document_;
            bool changed = false;
            QString next_node = node_;
            if (pdf_)
            {
                auto& document = std::get<PdfDocument>(candidate);
                auto& page = document.pages.at(page_);
                PdfCommand command;
                command.page_id =
                    args.value("pageId", QString::fromStdString(page.id)).toString().toStdString();
                if (action == "rotate")
                {
                    command.kind = PdfCommandKind::RotatePage;
                    command.clockwise_quarter_turns = args.value("turns", 1).toInt();
                }
                else if (action == "deletePage")
                    command.kind = PdfCommandKind::DeletePage;
                else if (action == "movePage")
                {
                    command.kind = PdfCommandKind::MovePage;
                    command.destination_index = args.value("index", -1).toUInt();
                }
                else if (action == "deleteAnnotation")
                {
                    command.kind = PdfCommandKind::DeleteAnnotation;
                    command.annotation_id = args.value("id").toString().toStdString();
                }
                else if (action == "note" || action == "highlight" || action == "label" ||
                    action == "watermark" || action == "image")
                {
                    if (command.page_id != page.id)
                    {
                        error(QStringLiteral("请先切换到需要批注的页面。"));
                        return false;
                    }
                    command.kind = PdfCommandKind::AddAnnotation;
                    command.annotation.kind =
                        action == "note" ? PdfAnnotationKind::Text : PdfAnnotationKind::Highlight;
                    command.annotation.contents = args.value("text").toString().toUtf8().toStdString();
                    const double x = args.value("x", -1).toDouble(), y = args.value("y", -1).toDouble();
                    const double w = args.value("width", action == "note" ? 0.045 : 0.22).toDouble();
                    const double h = args.value("height", action == "note" ? 0.045 : 0.035).toDouble();
                    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
                        x < 0 || y < 0 || x >= 1 || y >= 1 || w <= 0 || h <= 0 || w > 1 || h > 1)
                    {
                        error(QStringLiteral("批注位置或范围无效。"));
                        return false;
                    }
                    const auto point = [&page](double u, double v) -> std::pair<double, double>
                    {
                        if (page.rotation == 1)
                            return {v * page.width_points, u * page.height_points};
                        if (page.rotation == 2)
                            return {(1 - u) * page.width_points, v * page.height_points};
                        if (page.rotation == 3)
                            return {(1 - v) * page.width_points, (1 - u) * page.height_points};
                        return {u * page.width_points, (1 - v) * page.height_points};
                    };
                    const auto a = point(x, y), b = point(std::min(1.0, x + w), std::min(1.0, y + h));
                    command.annotation.rect = {std::min(a.first, b.first), std::min(a.second, b.second),
                        std::abs(a.first - b.first), std::abs(a.second - b.second)};
                    if (action == "label" || action == "watermark" || action == "image")
                    {
                        const auto& rect = command.annotation.rect;
                        auto stamp =
                            make_pdf_stamp(action, QSizeF(rect.width, rect.height), args, page.rotation);
                        if (stamp.error != PdfError::None)
                        {
                            error(QString::fromStdString(stamp.message));
                            return false;
                        }
                        command.annotation.kind = PdfAnnotationKind::Stamp;
                        command.annotation.stamp_bytes =
                            std::make_shared<const std::vector<std::uint8_t>>(std::move(stamp.bytes));
                        if (action == "image")
                            command.annotation.contents = "图片标注";
                    }
                }
                else
                {
                    error(QStringLiteral("未知的 PDF 操作。"));
                    return false;
                }
                const auto result = apply_pdf_command(document, command);
                if (result.error != PdfError::None)
                {
                    error(QString::fromStdString(result.message));
                    return false;
                }
                changed = result.changed;
            }
            else
            {
                MindMapCommand command;
                command.target_id = args.value("id", node_).toString().toStdString();
                command.new_id =
                    args.value("newId", QUuid::createUuid().toString(QUuid::Id128)).toString().toStdString();
                command.parent_id = args.value("toId", args.value("parentId")).toString().toStdString();
                command.text = args.value("text", QStringLiteral("新主题")).toString().toUtf8().toStdString();
                auto& graph = std::get<MindMapDocument>(candidate);
                const auto node = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& item)
                {
                    return item.id == command.target_id;
                });
                command.x = args.value("x", node != graph.nodes.end() ? std::min(48000.0, node->x + 260) : 80)
                                .toDouble();
                command.y = args.value("y", node != graph.nodes.end() ? std::min(48000.0, node->y + 110) : 80)
                                .toDouble();
                if (node != graph.nodes.end())
                {
                    command.width = args.value("width", node->width).toDouble();
                    command.height = args.value("height", node->height).toDouble();
                    command.border =
                        args.value("border", QString::fromStdString(node->border)).toString().toStdString();
                    command.fill =
                        args.value("fill", QString::fromStdString(node->fill)).toString().toStdString();
                    command.shape =
                        args.value("shape", QString::fromStdString(node->shape)).toString().toStdString();
                    command.border_width = args.value("stroke", node->border_width).toDouble();
                }
                if (action == "autoLayout")
                    command.type = MindMapCommandType::AutoLayout;
                else if (action == "addChild")
                    command.type = MindMapCommandType::AddChild;
                else if (action == "addSibling")
                    command.type = MindMapCommandType::AddSibling;
                else if (action == "rename")
                    command.type = MindMapCommandType::Rename;
                else if (action == "deleteNode")
                    command.type = MindMapCommandType::DeleteSubtree;
                else if (action == "moveUp")
                    command.type = MindMapCommandType::MoveUp;
                else if (action == "moveDown")
                    command.type = MindMapCommandType::MoveDown;
                else if (action == "reparent")
                    command.type = MindMapCommandType::Reparent;
                else if (action == "collapse")
                    command.type = MindMapCommandType::ToggleCollapse;
                else if (action == "createNode")
                    command.type = MindMapCommandType::CreateNode;
                else if (action == "moveNode")
                    command.type = MindMapCommandType::MoveNode;
                else if (action == "styleNode")
                    command.type = MindMapCommandType::StyleNode;
                else if (action == "connect")
                {
                    command.type = MindMapCommandType::Connect;
                    command.text = args.value("text").toString().toStdString();
                }
                else if (action == "disconnect")
                    command.type = MindMapCommandType::Disconnect;
                else
                {
                    error(QStringLiteral("未知的思维导图操作。"));
                    return false;
                }
                const auto result = apply_mindmap_command(graph, command);
                if (result.error != MindMapError::None)
                {
                    error(QString::fromStdString(result.message));
                    return false;
                }
                changed = result.changed;
                if (action == "addChild" || action == "addSibling" || action == "createNode")
                    next_node = QString::fromStdString(command.new_id);
            }
            if (!changed)
                return true;
            undo_.push_back({document_, generation_, documentBytes(document_)});
            document_ = std::move(candidate);
            committed = true;
            generation_ = next_generation_++;
            node_ = next_node;
            redo_.clear();
            trim(undo_);
            message_.clear();
            refreshView();
            emit stateChanged();
            return true;
        }
        catch (const std::bad_alloc&)
        {
            error(committed ? QStringLiteral("修改已提交，界面更新内存不足，可撤销本次修改。")
                            : QStringLiteral("内存不足，未提交本次操作。"));
            return committed;
        }
    }

    void CanvasBridge::refreshView()
    {
        view_.clear();
        if (!active_)
        {
            emit viewChanged();
            return;
        }
        if (pdf_)
        {
            const auto& document = std::get<PdfDocument>(document_);
            page_ = std::clamp(page_, 0, static_cast<int>(document.pages.size()) - 1);
            const auto& page = document.pages[page_];
            QVariantList annotations;
            for (const auto* source : {&page.original_annotations, &page.added_annotations})
                for (const auto& annotation : *source)
                    if (!page.removed_annotation_ids.count(annotation.id))
                        annotations.push_back(QVariantMap{{"id", QString::fromStdString(annotation.id)},
                            {"text", QString::fromStdString(annotation.contents)},
                            {"kind", static_cast<int>(annotation.kind)}});
            view_ = {{"page", page_}, {"pageCount", static_cast<int>(document.pages.size())},
                {"pageId", QString::fromStdString(page.id)}, {"width", pdf_page_display_width(page)},
                {"height", pdf_page_display_height(page)}, {"editable", document.editable},
                {"reason", QString::fromStdString(document.read_only_reason)},
                {"text", QString::fromStdString(page.text)}, {"textTruncated", page.text_truncated},
                {"annotations", annotations}};
            ++render_token_;
            render_timer_.start();
        }
        else
        {
            const auto& document = std::get<MindMapDocument>(document_);
            if (!connection_from_.isEmpty() &&
                std::none_of(document.nodes.begin(), document.nodes.end(), [&](const auto& node)
            {
                return node.id == connection_from_.toStdString();
            }))
                beginConnection(connection_mode_ == "once" ? "off" : connection_mode_, {});
            if (document.free_layout)
            {
                QVariantList nodes, edges;
                std::map<std::string, QString> titles;
                double width = 800, height = 500;
                bool selected_found = false;
                for (const auto& node : document.nodes)
                    if (node.id == node_.toStdString())
                        selected_found = true;
                if (!selected_found)
                    node_ = QString::fromStdString(document.root_id);
                QVariantMap selected;
                for (const auto& node : document.nodes)
                {
                    titles[node.id] = QString::fromStdString(node.text).left(28);
                    QVariantMap item{{"id", QString::fromStdString(node.id)},
                        {"text", QString::fromStdString(node.text)}, {"x", node.x}, {"y", node.y},
                        {"width", node.width}, {"height", node.height},
                        {"border", QString::fromStdString(node.border)},
                        {"fill", QString::fromStdString(node.fill)},
                        {"shape", QString::fromStdString(node.shape)}, {"stroke", node.border_width}};
                    if (node.id == node_.toStdString())
                        selected = item;
                    width = std::max(width, node.x + node.width + 120);
                    height = std::max(height, node.y + node.height + 120);
                    nodes.push_back(item);
                }
                for (const auto& edge : document.edges)
                    edges.push_back(QVariantMap{{"id", QString::fromStdString(edge.id)},
                        {"from", QString::fromStdString(edge.from)}, {"to", QString::fromStdString(edge.to)},
                        {"label", QString::fromStdString(edge.label)},
                        {"display", titles[edge.from] + QStringLiteral(" → ") + titles[edge.to]}});
                view_ = {{"nodes", nodes}, {"edges", edges}, {"width", width}, {"height", height},
                    {"freeLayout", true}, {"selectedId", node_}, {"selectedText", selected.value("text")},
                    {"selectedStyle", selected}, {"nodeCount", static_cast<int>(nodes.size())},
                    {"editable", !document.read_only}};
                emit viewChanged();
                return;
            }
        }
        emit viewChanged();
    }
    QVariantMap CanvasBridge::snapshot() const
    {
        auto result = view_;
        result.insert("zoom", zoom());
        result.insert("connectionMode", connectionMode());
        result.insert("connectionFrom", connectionFrom());
        result.insert("active", active_);
        result.insert("locked", locked());
        result.insert("modified", modified());
        result.insert("name", documentName());
        result.insert("kind", kind());
        result.insert("revision", QVariant::fromValue(revision_));
        return result;
    }

    PdfExportSource CanvasBridge::pdfSource() const
    {
        PdfExportSource result;
        if (!active_ || locked())
            result.error = "画布尚未准备好导出。";
        else if (pdf_)
            result.content = std::get<PdfDocument>(document_);
        else
            result.content = std::get<MindMapDocument>(document_);
        result.title = documentName().toStdString();
        result.source_path = path_.toStdString();
        result.protected_source_path = source_path_.toStdString();
        result.current = static_cast<std::size_t>(page_);
        return result;
    }
    QString CanvasBridge::outline() const
    {
        if (!active_)
            return {};
        if (pdf_)
            return QString::fromStdString(std::get<PdfDocument>(document_).pages[page_].text);
        const auto result = export_mindmap_outline(std::get<MindMapDocument>(document_));
        return result.error == MindMapError::None ? QString::fromStdString(result.text) : QString{};
    }
    void CanvasBridge::copyOutline()
    {
        if (!active_ || locked())
            return;
        QGuiApplication::clipboard()->setText(outline());
    }
}
