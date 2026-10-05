#include "canvas_paint.hpp"
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    QPointF anchor(const QRectF& rect, const QPointF& other)
    {
        const auto delta = other - rect.center();
        const double scale =
            std::max(std::abs(delta.x()) / (rect.width() / 2), std::abs(delta.y()) / (rect.height() / 2));
        return scale > 0 ? rect.center() + delta / scale : rect.center();
    }
    QPainterPath outline(const QRectF& rect, const QString& shape)
    {
        QPainterPath path;
        if (shape == "diamond")
        {
            path.moveTo(rect.center().x(), rect.top());
            path.lineTo(rect.right(), rect.center().y());
            path.lineTo(rect.center().x(), rect.bottom());
            path.lineTo(rect.left(), rect.center().y());
            path.closeSubpath();
        }
        else if (shape == "ellipse")
            path.addEllipse(rect);
        else if (shape == "rectangle")
            path.addRect(rect);
        else
            path.addRoundedRect(rect, 12, 12);
        return path;
    }
}

namespace mirrorfly
{
    void paint_mindmap(QPainter& painter, const QVariantMap& scene, const QVariantMap& theme,
        const QRectF& viewport, const QVariantMap& interaction, const CanvasRoutes* cached_routes)
    {
        std::map<QString, QRectF> rectangles;
        const auto nodes = scene.value("nodes").toList();
        for (const auto& value : nodes)
        {
            const auto node = value.toMap();
            QRectF rect(node["x"].toDouble(), node["y"].toDouble(), node["width"].toDouble(),
                node["height"].toDouble());
            if (interaction["dragId"] == node["id"])
                rect.moveTopLeft({interaction["x"].toDouble(), interaction["y"].toDouble()});
            rectangles[node["id"].toString()] = rect;
        }
        QFont font(theme.value("fontFamily").toString());
        font.setPixelSize(theme.value("fontSize", 14).toInt());
        painter.setFont(font);
        const auto computed_routes = cached_routes ? CanvasRoutes{} : route_mindmap(scene, interaction);
        const auto& routes = cached_routes ? *cached_routes : computed_routes;
        for (const auto& value : scene.value("edges").toList())
        {
            const auto edge = value.toMap();
            const bool directed = scene.value("freeLayout").toBool();
            QPointF a(edge["x1"].toDouble(), edge["y1"].toDouble()),
                b(edge["x2"].toDouble(), edge["y2"].toDouble());
            QPainterPath path;
            if (directed)
            {
                const auto route = routes.constFind(edge["id"].toString());
                if (route == routes.cend())
                    continue;
                path = route.value();
                a = path.pointAtPercent(0);
                b = path.pointAtPercent(1);
            }
            else
            {
                path.moveTo(a);
                path.cubicTo(a + QPointF(16, 0), b - QPointF(16, 0), b);
            }
            if (!viewport.intersects(path.boundingRect().adjusted(-15, -15, 15, 15)))
                continue;
            const bool selected = interaction["edgeId"] == edge["id"] && !edge["id"].toString().isEmpty();
            const QColor ink(
                theme.value(selected ? "accent" : "mindmapEdge", theme.value("accent")).toString());
            painter.setPen(QPen(ink, selected ? 3 : 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            if (directed)
            {
                const auto delta = b - path.pointAtPercent(.97);
                const double angle = std::atan2(delta.y(), delta.x());
                QPolygonF arrow;
                arrow.push_back(b);
                arrow.push_back(b - QPointF(std::cos(angle - .48), std::sin(angle - .48)) * 12);
                arrow.push_back(b - QPointF(std::cos(angle + .48), std::sin(angle + .48)) * 12);
                painter.setBrush(ink);
                painter.drawPolygon(arrow);
                const QString label = edge["label"].toString();
                if (!label.isEmpty())
                {
                    const QRectF box(path.pointAtPercent(.5) - QPointF(85, 13), QSizeF(170, 26));
                    painter.fillRect(box, QColor(theme.value("surfaceColor").toString()));
                    painter.drawText(
                        box, Qt::AlignCenter, painter.fontMetrics().elidedText(label, Qt::ElideRight, 162));
                }
            }
        }
        if (!interaction.value("sourceId").toString().isEmpty())
        {
            const auto source = rectangles.find(interaction["sourceId"].toString());
            if (source != rectangles.end())
            {
                const QPointF target(interaction["edgeX"].toDouble(), interaction["edgeY"].toDouble());
                painter.setPen(QPen(QColor(theme.value("accent").toString()), 2, Qt::DashLine));
                painter.drawLine(anchor(source->second, target), target);
            }
        }
        for (const auto& value : nodes)
        {
            const auto node = value.toMap();
            const auto rect = rectangles.at(node["id"].toString());
            if (!viewport.intersects(rect.adjusted(-6, -6, 6, 6)))
                continue;
            const bool selected = node["id"] == scene["selectedId"];
            const QString custom_border = node.value("border").toString();
            const QString custom_fill = node.value("fill").toString();
            const QColor border(custom_border.isEmpty()
                    ? theme.value("mindmapNodeBorder", theme.value("accent")).toString()
                    : custom_border);
            const QColor fill(custom_fill.isEmpty()
                    ? theme.value("mindmapNodeFill", theme.value("surfaceColor")).toString()
                    : custom_fill);
            const auto shape = outline(rect, node.value("shape").toString());
            painter.setPen(QPen(border, node.value("stroke", 2).toDouble()));
            painter.setBrush(fill);
            painter.drawPath(shape);
            if (selected)
            {
                painter.setPen(QPen(QColor(theme.value("accent").toString()), 1.5, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(rect.adjusted(-5, -5, 5, 5), 14, 14);
            }
            painter.save();
            painter.setClipPath(shape);
            painter.setPen(
                QColor(theme.value(fill.lightnessF() < .45 ? "onAccent" : "textPrimary").toString()));
            const double inset = node.value("shape").toString() == "diamond" ? rect.width() * .25 : 14;
            const auto text = node["text"].toString() +
                (node["collapsed"].toBool() ? QStringLiteral("  [+%1]").arg(node["children"].toInt())
                                            : QString{});
            painter.drawText(rect.adjusted(inset, 10, -inset, -10), Qt::TextWordWrap | Qt::AlignCenter, text);
            painter.restore();
        }
    }
}
