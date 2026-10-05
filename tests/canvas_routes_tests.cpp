#include "canvas_renderer.hpp"
#include "canvas_routes.hpp"

#include <QGuiApplication>
#include <QPainterPathStroker>
#include <iostream>

namespace
{
    int failures = 0;
    void check(bool value, const char* message)
    {
        if (!value)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
    QVariantMap node(const QString& id, double x, double y, const QString& shape = "rectangle")
    {
        return {{"id", id}, {"x", x}, {"y", y}, {"width", 160}, {"height", 80}, {"shape", shape}};
    }
    QVariantMap edge(const QString& id, const QString& from, const QString& to)
    {
        return {{"id", id}, {"from", from}, {"to", to}};
    }
    QPainterPath stroke(const QPainterPath& path)
    {
        QPainterPathStroker stroker;
        stroker.setWidth(2);
        return stroker.createStroke(path);
    }
    void check_routes()
    {
        using namespace mirrorfly;
        const QVariantMap scene{{"freeLayout", true},
            {"nodes", QVariantList{node("a", 80, 200), node("b", 420, 80), node("c", 420, 320)}},
            {"edges", QVariantList{edge("ab", "a", "b"), edge("ac", "a", "c")}}};
        auto routes = route_mindmap(scene);
        check(routes.size() == 2 && routes["ab"].pointAtPercent(0) != routes["ac"].pointAtPercent(0),
            "branches have distinct outgoing ports");
        check(!stroke(routes["ab"]).intersects(stroke(routes["ac"])),
            "tree siblings do not overlap their connecting paths");
        const QVariantMap obstructed{{"freeLayout", true},
            {"nodes", QVariantList{node("a", 80, 200), node("obstacle", 420, 200), node("b", 760, 200)}},
            {"edges", QVariantList{edge("ab", "a", "b")}}};
        const auto detour = route_mindmap(obstructed).value("ab");
        check(!stroke(detour).intersects(QRectF(412, 192, 176, 96)),
            "long connection avoids an intervening node");
        check(!stroke(detour).intersects(QRectF(82, 202, 156, 76)) &&
                !stroke(detour).intersects(QRectF(762, 202, 156, 76)),
            "detour never travels back through either endpoint");
        CanvasRenderer renderer;
        renderer.setScene(obstructed);
        const auto pick = detour.pointAtPercent(.5);
        check(renderer.edgeAt(pick.x(), pick.y()) == "ab",
            "hit testing follows routed line instead of old center chord");
        renderer.setZoom(2);
        check(renderer.edgeAt(pick.x(), pick.y()) == "ab", "zoom preserves graph-coordinate picking");
        renderer.setInteraction({{"dragId", "b"}, {"x", 760}, {"y", 500}});
        const auto moved = route_mindmap(obstructed, {{"dragId", "b"}, {"x", 760}, {"y", 500}}).value("ab");
        const auto moved_pick = moved.pointAtPercent(.5);
        check(renderer.edgeAt(moved_pick.x(), moved_pick.y()) == "ab",
            "drag preview and selection share new geometry");
        QVariantMap cycles{{"freeLayout", true},
            {"nodes", QVariantList{node("a", 80, 200, "ellipse"), node("b", 420, 200, "diamond")}},
            {"edges", QVariantList{edge("ab", "a", "b"), edge("ba", "b", "a"), edge("self", "a", "a")}}};
        routes = route_mindmap(cycles);
        check(routes.size() == 3 && routes["ab"].pointAtPercent(.5) != routes["ba"].pointAtPercent(.5),
            "opposite directions use different ports and do not coincide");
        check(routes["self"].length() > 80 &&
                routes["self"].pointAtPercent(0) != routes["self"].pointAtPercent(1),
            "self loop remains selectable and non-degenerate");
        check(route_mindmap(cycles) == routes, "routing is deterministic for screen and PDF reuse");
    }
}

int run_canvas_routes_tests(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    check_routes();
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_canvas_routes_tests(argc, argv);
}
