#pragma once

#include <QGlyphRun>
#include <QQuickPaintedItem>
#include <QTransform>
#include <QVariantMap>

namespace mirrorfly
{
    struct SpreadsheetTextLayout
    {
        QList<QGlyphRun> glyphs;
        qreal height = 0;
        qreal width = 0;
        qreal scale = 1;
        QTransform transform;
    };
    SpreadsheetTextLayout layout_spreadsheet_text(const QString& text, const QFont& font, qreal width,
        const QVariantMap& format, const QPaintDevice* device = nullptr);
    SpreadsheetTextLayout layout_spreadsheet_cell_text(const QString& text, const QFont& font,
        const QSizeF& size, const QVariantMap& format, const QPaintDevice* device = nullptr);
    void paint_spreadsheet_aligned_text(
        QPainter& painter, const QRectF& rect, const QString& text, const QVariantMap& format);

    class SpreadsheetTextRenderer : public QQuickPaintedItem
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(QString text READ text WRITE setText NOTIFY changed)
        Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY changed)
        Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
        Q_PROPERTY(QVariantMap format READ format WRITE setFormat NOTIFY changed)
    public:
        explicit SpreadsheetTextRenderer(QQuickItem* parent = nullptr);
        QString text() const;
        QFont font() const;
        QColor color() const;
        QVariantMap format() const;
        void setText(const QString& text);
        void setFont(const QFont& font);
        void setColor(const QColor& color);
        void setFormat(const QVariantMap& format);
        void paint(QPainter* painter) override;
    signals:
        void changed();

    private:
        QString text_;
        QFont font_;
        QColor color_;
        QVariantMap format_;
    };
}
