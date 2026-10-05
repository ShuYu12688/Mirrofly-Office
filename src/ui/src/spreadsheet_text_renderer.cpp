#include "spreadsheet_text_renderer.hpp"

#include <QPainter>

namespace mirrorfly
{
    void paint_spreadsheet_aligned_text(
        QPainter& painter, const QRectF& rect, const QString& text, const QVariantMap& format)
    {
        const auto layout =
            layout_spreadsheet_cell_text(text, painter.font(), rect.size(), format, painter.device());
        const auto vertical = format.value("valign").toString();
        qreal top = rect.top();
        if (vertical == "bottom")
            top += rect.height() - layout.height;
        else if (vertical != "top")
            top += (rect.height() - layout.height) / 2;
        painter.save();
        painter.setClipRect(rect, Qt::IntersectClip);
        qreal left = rect.left();
        const auto alignment = format.value("align").toString();
        if (alignment == "right")
            left += rect.width() - layout.width;
        else if (alignment == "center")
            left += (rect.width() - layout.width) / 2;
        painter.translate(left, top);
        painter.setWorldTransform(layout.transform, true);
        for (const auto& run : layout.glyphs)
            painter.drawGlyphRun({0, 0}, run);
        painter.restore();
    }
    SpreadsheetTextRenderer::SpreadsheetTextRenderer(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        setAcceptedMouseButtons(Qt::NoButton);
        setAntialiasing(true);
    }
    QString SpreadsheetTextRenderer::text() const
    {
        return text_;
    }
    QFont SpreadsheetTextRenderer::font() const
    {
        return font_;
    }
    QColor SpreadsheetTextRenderer::color() const
    {
        return color_;
    }
    QVariantMap SpreadsheetTextRenderer::format() const
    {
        return format_;
    }
    void SpreadsheetTextRenderer::setText(const QString& text)
    {
        if (text_ == text)
            return;
        text_ = text;
        update();
        emit changed();
    }
    void SpreadsheetTextRenderer::setFont(const QFont& font)
    {
        if (font_ == font)
            return;
        font_ = font;
        update();
        emit changed();
    }
    void SpreadsheetTextRenderer::setColor(const QColor& color)
    {
        if (color_ == color)
            return;
        color_ = color;
        update();
        emit changed();
    }
    void SpreadsheetTextRenderer::setFormat(const QVariantMap& format)
    {
        if (format_ == format)
            return;
        format_ = format;
        update();
        emit changed();
    }
    void SpreadsheetTextRenderer::paint(QPainter* painter)
    {
        if (!painter)
            return;
        painter->setFont(font_);
        painter->setPen(color_);
        paint_spreadsheet_aligned_text(*painter, boundingRect(), text_, format_);
    }
}
