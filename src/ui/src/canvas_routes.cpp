#include "canvas_routes.hpp"

#include <QPainterPathStroker>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    struct Node
    {
        QRectF rect;
        QString shape;
    };
    struct Edge
    {
        QString id, from, to;
        QPointF a, b, start_normal, end_normal;
    };
    struct Port
    {
        std::size_t edge;
        bool start;
        double order;
    };
    QPointF normal(int side)
    {
        if (side == 0)
            return {-1, 0};
        if (side == 1)
            return {1, 0};
        return side == 2 ? QPointF(0, -1) : QPointF(0, 1);
    }
    QPointF anchor(const Node& node, int side, double fraction)
    {
        const auto r = node.rect;
        const double t = (fraction - .5) * 1.2;
        double extent = 1;
        if (node.shape == "ellipse")
            extent = std::sqrt(1 - t * t);
        else if (node.shape == "diamond")
            extent = 1 - std::abs(t);
        if (side < 2)
            return r.center() + QPointF((side == 0 ? -1 : 1) * r.width() * .5 * extent, t * r.height() * .5);
        return r.center() + QPointF(t * r.width() * .5, (side == 2 ? -1 : 1) * r.height() * .5 * extent);
    }
    QPainterPath polyline(const QList<QPointF>& points)
    {
        QPainterPath path(points.first());
        for (qsizetype i = 1; i < points.size(); ++i)
            path.lineTo(points.at(i));
        return path;
    }
}

namespace mirrorfly
{
    CanvasRoutes route_mindmap(const QVariantMap& scene, const QVariantMap& interaction)
    {
        CanvasRoutes routes;
        if (!scene.value("freeLayout").toBool())
            return routes;
        QMap<QString, Node> nodes;
        QRectF bounds;
        for (const auto& value : scene.value("nodes").toList())
        {
            const auto item = value.toMap();
            Node node;
            node.rect = QRectF(item.value("x").toDouble(), item.value("y").toDouble(),
                item.value("width").toDouble(), item.value("height").toDouble());
            node.shape = item.value("shape").toString();
            if (item.value("id") == interaction.value("dragId"))
                node.rect.moveTopLeft({interaction.value("x").toDouble(), interaction.value("y").toDouble()});
            nodes.insert(item.value("id").toString(), node);
            bounds = bounds.united(node.rect);
        }
        std::vector<Edge> edges;
        QMap<QString, QMap<int, std::vector<Port>>> ports;
        for (const auto& value : scene.value("edges").toList())
        {
            const auto item = value.toMap();
            Edge edge{item.value("id").toString(), item.value("from").toString(), item.value("to").toString(),
                {}, {}, {}, {}};
            if (!nodes.contains(edge.from) || !nodes.contains(edge.to))
                continue;
            const auto a = nodes[edge.from].rect, b = nodes[edge.to].rect;
            int start = 1, end = 0;
            if (edge.from == edge.to)
                end = 2;
            else if (a.right() <= b.left())
                start = 1;
            else if (b.right() <= a.left())
            {
                start = 0;
                end = 1;
            }
            else
            {
                start = a.center().y() < b.center().y() ? 3 : 2;
                end = start == 3 ? 2 : 3;
            }
            const auto index = edges.size();
            edges.push_back(edge);
            ports[edge.from][start].push_back({index, true, start < 2 ? b.center().y() : b.center().x()});
            ports[edge.to][end].push_back({index, false, end < 2 ? a.center().y() : a.center().x()});
        }
        // Each incidence has its own port; incoming and outgoing links share the same allocation.
        for (auto node = ports.begin(); node != ports.end(); ++node)
        {
            for (auto side = node->begin(); side != node->end(); ++side)
            {
                auto& group = side.value();
                std::stable_sort(group.begin(), group.end(), [](const Port& a, const Port& b)
                {
                    return a.order < b.order;
                });
                for (std::size_t i = 0; i < group.size(); ++i)
                {
                    const auto port = group[i];
                    auto& edge = edges[port.edge];
                    const auto point =
                        anchor(nodes[node.key()], side.key(), (i + 1.0) / (group.size() + 1.0));
                    if (port.start)
                    {
                        edge.a = point;
                        edge.start_normal = normal(side.key());
                    }
                    else
                    {
                        edge.b = point;
                        edge.end_normal = normal(side.key());
                    }
                }
            }
        }
        for (std::size_t index = 0; index < edges.size(); ++index)
        {
            const auto& edge = edges[index];
            const auto delta = edge.b - edge.a;
            const double distance = edge.start_normal.x() != 0 ? std::abs(delta.x()) : std::abs(delta.y());
            const double reach = edge.from == edge.to ? 90 : std::max(24.0, distance * .5);
            QPainterPath path(edge.a);
            path.cubicTo(edge.a + edge.start_normal * reach, edge.b + edge.end_normal * reach, edge.b);
            const auto collisions = [&](const QPainterPath& candidate)
            {
                int count = 0;
                const auto box = candidate.boundingRect().adjusted(-1, -1, 1, 1);
                QPainterPathStroker stroker;
                stroker.setWidth(1);
                const auto line = stroker.createStroke(candidate);
                for (auto node = nodes.cbegin(); node != nodes.cend(); ++node)
                {
                    const bool endpoint = node.key() == edge.from || node.key() == edge.to;
                    const auto obstacle =
                        endpoint ? node->rect.adjusted(2, 2, -2, -2) : node->rect.adjusted(-8, -8, 8, 8);
                    if (!box.intersects(obstacle))
                        continue;
                    QPainterPath shape;
                    if (endpoint && node->shape == "ellipse")
                        shape.addEllipse(obstacle);
                    else if (endpoint && node->shape == "diamond")
                    {
                        shape.moveTo(obstacle.center().x(), obstacle.top());
                        shape.lineTo(obstacle.right(), obstacle.center().y());
                        shape.lineTo(obstacle.center().x(), obstacle.bottom());
                        shape.lineTo(obstacle.left(), obstacle.center().y());
                        shape.closeSubpath();
                    }
                    else
                        shape.addRect(obstacle);
                    if (line.intersects(shape))
                        ++count;
                }
                return count;
            };
            int best_hits = collisions(path);
            if (best_hits > 0)
            {
                // Bounded detour candidates avoid intermediate nodes without unbounded path search.
                const auto a = edge.a + edge.start_normal * 24;
                const auto b = edge.b + edge.end_normal * 24;
                const double lane = 24 + static_cast<double>(index % 8) * 6;
                double best_length = path.length();
                const auto consider = [&](const QPainterPath& candidate)
                {
                    const int hits = collisions(candidate);
                    const double length = candidate.length();
                    if (hits < best_hits || (hits == best_hits && length < best_length))
                    {
                        path = candidate;
                        best_hits = hits;
                        best_length = length;
                    }
                };
                for (const double y : {bounds.top() - lane, bounds.bottom() + lane})
                    consider(polyline({edge.a, a, {a.x(), y}, {b.x(), y}, b, edge.b}));
                for (const double x : {bounds.left() - lane, bounds.right() + lane})
                    consider(polyline({edge.a, a, {x, a.y()}, {x, b.y()}, b, edge.b}));
            }
            routes.insert(edge.id, path);
        }
        return routes;
    }
}
