#pragma once
#include "canvas_routes.hpp"
#include <QPainter>
#include <QVariantMap>

namespace mirrorfly
{
    void paint_mindmap(QPainter& painter, const QVariantMap& scene, const QVariantMap& theme,
        const QRectF& viewport, const QVariantMap& interaction = {}, const CanvasRoutes* routes = nullptr);
}
