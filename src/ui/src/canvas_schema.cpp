#include "canvas_bridge.hpp"

namespace mirrorfly
{
    QVariantMap CanvasBridge::editSchema() const
    {
        if (pdf_)
            return {
                {"rotate", QVariantMap{{"pageId", "existing page ID"}, {"turns", "integer quarter turns"}}},
                {"movePage",
                    QVariantMap{{"pageId", "existing page ID"}, {"index", "zero-based destination"}}},
                {"deletePage", QVariantMap{{"pageId", "existing page ID"}}},
                {"deleteAnnotation", QVariantMap{{"pageId", "existing page ID"}, {"id", "annotation ID"}}},
                {"note",
                    QVariantMap{{"text", "string"}, {"x", "normalized [0,1)"}, {"y", "normalized [0,1)"},
                        {"width", "normalized (0,1]"}, {"height", "normalized (0,1]"}}},
                {"highlight",
                    QVariantMap{{"text", "string"}, {"x", "normalized [0,1)"}, {"y", "normalized [0,1)"},
                        {"width", "normalized (0,1]"}, {"height", "normalized (0,1]"}}}};
        const QVariantMap create{{"id", "existing parent/selected node ID for addChild/addSibling"},
            {"newId", "optional unique node ID; may be chosen now and referenced by later batch steps"},
            {"text", "node label"}, {"x", "canvas coordinate [0,48000]"},
            {"y", "canvas coordinate [0,48000]"}};
        const QVariantMap target{{"id", "existing node ID; omitted uses selected node"}};
        return {{"autoLayout",
                    QVariantMap{{"description",
                        "No arguments. Arrange the entire graph by branch and depth, preserving nodes, "
                        "styles and links. One undoable operation; use once after constructing the graph."}}},
            {"addChild", create}, {"addSibling", create}, {"createNode", create},
            {"rename", QVariantMap{{"id", "node ID"}, {"text", "new label"}}}, {"deleteNode", target},
            {"moveUp", target}, {"moveDown", target}, {"collapse", target},
            {"reparent", QVariantMap{{"id", "node ID"}, {"parentId", "new parent ID"}}},
            {"moveNode",
                QVariantMap{{"id", "node ID"}, {"x", "canvas coordinate"}, {"y", "canvas coordinate"}}},
            {"styleNode",
                QVariantMap{{"id", "node ID"}, {"width", "canvas units [64,1000]"},
                    {"height", "canvas units [48,800]"}, {"border", "#RRGGBB"}, {"fill", "#RRGGBB"},
                    {"shape", "rectangle|rounded|ellipse|diamond"}, {"stroke", "border width [1,6]"}}},
            {"connect",
                QVariantMap{{"id", "source node ID"}, {"toId", "target node ID"},
                    {"newId", "optional unique connection ID"}, {"text", "optional label"}}},
            {"disconnect", QVariantMap{{"id", "connection ID"}}}};
    }
}
