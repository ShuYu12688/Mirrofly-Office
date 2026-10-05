#include "canvas_renderer.hpp"
#include "canvas_paint.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    CanvasRenderer::CanvasRenderer(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        setAntialiasing(true);
    }
    QImage CanvasRenderer::image() const
    {
        return image_;
    }
    QVariantMap CanvasRenderer::scene() const
    {
        return scene_;
    }
    QVariantMap CanvasRenderer::theme() const
    {
        return theme_;
    }
    QPointF CanvasRenderer::offset() const
    {
        return offset_;
    }
    qreal CanvasRenderer::zoom() const
    {
        return zoom_;
    }
    void CanvasRenderer::setZoom(qreal value)
    {
        if (!std::isfinite(value) || value < 0.25 || value > 4 || qFuzzyCompare(value, zoom_))
            return;
        zoom_ = value;
        update();
        emit changed();
    }
    QVariantMap CanvasRenderer::interaction() const
    {
        return interaction_;
    }
    void CanvasRenderer::setInteraction(const QVariantMap& value)
    {
        const bool moved = interaction_.value("dragId") != value.value("dragId") ||
            interaction_.value("x") != value.value("x") || interaction_.value("y") != value.value("y");
        interaction_ = value;
        if (moved)
            routes_ = route_mindmap(scene_, interaction_);
        update();
        emit changed();
    }
    void CanvasRenderer::setImage(const QImage& image)
    {
        image_ = image;
        update();
        emit changed();
    }
    void CanvasRenderer::setScene(const QVariantMap& scene)
    {
        scene_ = scene;
        routes_ = route_mindmap(scene_, interaction_);
        update();
        emit changed();
    }
    void CanvasRenderer::setTheme(const QVariantMap& theme)
    {
        theme_ = theme;
        update();
        emit changed();
    }
    void CanvasRenderer::setOffset(QPointF offset)
    {
        offset_ = offset;
        update();
        emit changed();
    }
    void CanvasRenderer::paint(QPainter* painter)
    {
        if (!image_.isNull())
        {
            QSizeF size = image_.size();
            size.scale(boundingRect().size(), Qt::KeepAspectRatio);
            size *= zoom_;
            const QRectF target(std::max<qreal>(0, (width() - size.width()) / 2) - offset_.x(),
                std::max<qreal>(0, (height() - size.height()) / 2) - offset_.y(), size.width(),
                size.height());
            painter->drawImage(target, image_);
            return;
        }
        if (!scene_.contains("nodes"))
            return;
        painter->translate(-offset_);
        painter->scale(zoom_, zoom_);
        const QRectF viewport(offset_ / zoom_, QSizeF(width() / zoom_, height() / zoom_));
        paint_mindmap(*painter, scene_, theme_, viewport, interaction_, &routes_);
    }

    QString CanvasRenderer::edgeAt(qreal x, qreal y) const
    {
        QPainterPathStroker stroke;
        stroke.setWidth(18 / zoom_);
        for (auto it = routes_.cbegin(); it != routes_.cend(); ++it)
            if (stroke.createStroke(it.value()).contains(QPointF(x, y)))
                return it.key();
        return {};
    }
}
