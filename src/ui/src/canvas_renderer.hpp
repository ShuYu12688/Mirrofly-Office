#pragma once
#include "canvas_routes.hpp"
#include <QImage>
#include <QQuickPaintedItem>
#include <QVariantMap>

namespace mirrorfly
{
    class CanvasRenderer : public QQuickPaintedItem
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY changed)
        Q_PROPERTY(QVariantMap scene READ scene WRITE setScene NOTIFY changed)
        Q_PROPERTY(QVariantMap theme READ theme WRITE setTheme NOTIFY changed)
        Q_PROPERTY(QPointF offset READ offset WRITE setOffset NOTIFY changed)
        Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY changed)
        Q_PROPERTY(QVariantMap interaction READ interaction WRITE setInteraction NOTIFY changed)
    public:
        explicit CanvasRenderer(QQuickItem* parent = nullptr);
        void paint(QPainter* painter) override;
        QImage image() const;
        QVariantMap scene() const;
        QVariantMap theme() const;
        QPointF offset() const;
        qreal zoom() const;
        void setZoom(qreal value);
        QVariantMap interaction() const;
        void setInteraction(const QVariantMap& value);
        void setImage(const QImage& image);
        void setScene(const QVariantMap& scene);
        void setTheme(const QVariantMap& theme);
        void setOffset(QPointF offset);
        Q_INVOKABLE QString edgeAt(qreal x, qreal y) const;
    signals:
        void changed();

    private:
        QImage image_;
        QVariantMap scene_, theme_, interaction_;
        QPointF offset_;
        qreal zoom_ = 1;
        CanvasRoutes routes_;
    };
}
