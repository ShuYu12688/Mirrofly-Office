#pragma once

#include <QQuickPaintedItem>
#include <QVariantMap>

namespace mirrorfly
{
    void paint_spreadsheet_borders(
        QPainter& painter, const QRectF& rect, const QVariantMap& format, qreal scale);

    class SpreadsheetBorderRenderer : public QQuickPaintedItem
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(QVariantMap format READ format WRITE setFormat NOTIFY changed)
        Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY changed)
    public:
        explicit SpreadsheetBorderRenderer(QQuickItem* parent = nullptr);
        QVariantMap format() const;
        qreal zoom() const;
        void setFormat(const QVariantMap& format);
        void setZoom(qreal zoom);
        void paint(QPainter* painter) override;
    signals:
        void changed();

    private:
        QVariantMap format_;
        qreal zoom_ = 1;
    };
}
