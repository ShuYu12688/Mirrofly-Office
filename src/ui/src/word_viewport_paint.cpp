#include "word_document.hpp"
#include "word_viewport.hpp"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QQuickWindow>
#include <QTextCursor>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mirrorfly
{
    void WordViewport::updatePolish()
    {
        if (!document_ || width() <= 0 || height() <= 0)
        {
            pixels_ = {};
            return;
        }
        // Rasterize on the GUI thread; the render thread only receives immutable visible pixels.
        const auto ratio = window() ? window()->devicePixelRatio() : 1.0;
        const auto scale = std::min(ratio * scale_, std::sqrt(4.0 * 1024 * 1024 / (width() * height())));
        const QSize size(
            std::max(1, int(std::floor(width() * scale))), std::max(1, int(std::floor(height() * scale))));
        const QRectF visible(0, top_, width(), height());
        bool complete = full_dirty_ || pixels_.size() != size || pixels_.isNull();
        QRectF paint_area = complete ? visible : dirty_document_.intersected(visible);
        bool advanced_viewport = complete;
        if (!complete && top_ != painted_top_)
        {
            const qreal vertical_scale = size.height() / height();
            const qreal delta = top_ - painted_top_;
            const int pixel_delta = qRound(delta * vertical_scale);
            // Only integral pixel translations preserve the original glyph sampling phase.
            if (std::abs(delta * vertical_scale - pixel_delta) > 0.000001 || std::abs(delta) >= height() ||
                std::abs(pixel_delta) >= size.height())
            {
                complete = true;
                advanced_viewport = true;
                paint_area = visible;
            }
            else if (pixel_delta != 0)
            {
                advanced_viewport = true;
                const qsizetype stride = pixels_.bytesPerLine();
                auto* data = pixels_.bits();
                const int shifted = std::abs(pixel_delta);
                const qsizetype retained = static_cast<qsizetype>(size.height() - shifted) * stride;
                if (pixel_delta > 0)
                {
                    std::memmove(data, data + static_cast<qsizetype>(shifted) * stride,
                        static_cast<std::size_t>(retained));
                    std::memset(data + retained, 0, static_cast<std::size_t>(shifted) * stride);
                    const QRectF exposed(0, top_ + height() - delta - 2, width(), delta + 4);
                    paint_area = exposed.united(paint_area).intersected(visible);
                }
                else
                {
                    std::memmove(data + static_cast<qsizetype>(shifted) * stride, data,
                        static_cast<std::size_t>(retained));
                    std::memset(data, 0, static_cast<std::size_t>(shifted) * stride);
                    const QRectF exposed(0, top_ - 2, width(), -delta + 4);
                    paint_area = exposed.united(paint_area).intersected(visible);
                }
            }
        }
        full_dirty_ = false;
        dirty_document_ = {};
        last_painted_document_area_ = paint_area;
        if (advanced_viewport)
        {
            painted_top_ = top_;
        }
        if (paint_area.isEmpty())
        {
            return;
        }
        if (complete)
        {
            pixels_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
            pixels_.fill(Qt::transparent);
        }
        setTextureSize(
            QSize(std::max(1, int(size.width() / ratio)), std::max(1, int(size.height() / ratio))));
        QPainter painter(&pixels_);
        painter.setRenderHints(
            QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        const qreal horizontal_scale = size.width() / width();
        const qreal vertical_scale = size.height() / height();
        if (!complete)
        {
            const QRectF scaled_area(paint_area.x() * horizontal_scale,
                (paint_area.y() - top_) * vertical_scale, paint_area.width() * horizontal_scale,
                paint_area.height() * vertical_scale);
            const QRect pixel_area = scaled_area.toAlignedRect().intersected(pixels_.rect());
            // Clear and redraw exactly the same device pixels, including fractional dirty edges.
            paint_area = QRectF(pixel_area.x() / horizontal_scale, top_ + pixel_area.y() / vertical_scale,
                pixel_area.width() / horizontal_scale, pixel_area.height() / vertical_scale);
            painter.setClipRect(pixel_area);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(pixel_area, Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        else
            painter.setClipRect(pixels_.rect());
        painter.scale(horizontal_scale, vertical_scale);
        painter.translate(0, -top_);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = paint_area;
        context.palette.setColor(
            QPalette::Text, selection_.value("color", QColor(Qt::black)).value<QColor>());
        const auto start = selection_.value("start").toInt();
        const auto end = selection_.value("end").toInt();
        if (start >= 0 && end > start && end < document_->characterCount())
        {
            QAbstractTextDocumentLayout::Selection selected;
            selected.cursor = QTextCursor(document_);
            selected.cursor.setPosition(start);
            selected.cursor.setPosition(end, QTextCursor::KeepAnchor);
            selected.format.setBackground(selection_.value("background").value<QColor>());
            selected.format.setForeground(selection_.value("foreground").value<QColor>());
            context.selections.append(selected);
        }
        document_->documentLayout()->draw(&painter, context);
        // Decorations share the visible raster; editing never rebuilds one QML item per paragraph.
        for (const auto& value : word_paragraph_decorations(*document_, context.clip))
            paint_word_decoration(painter, value.toMap());
    }

    void WordViewport::paint(QPainter* painter)
    {
        painter->drawImage(boundingRect(), pixels_);
    }
}
