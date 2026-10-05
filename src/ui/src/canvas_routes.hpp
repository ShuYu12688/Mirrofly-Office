#pragma once

#include <QMap>
#include <QPainterPath>
#include <QVariantMap>

namespace mirrorfly
{
    using CanvasRoutes = QMap<QString, QPainterPath>;
    CanvasRoutes route_mindmap(const QVariantMap& scene, const QVariantMap& interaction = {});
}
