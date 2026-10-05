#include "presentation_fill_renderer.hpp"

#include <QCache>

#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    QBrush presentation_pattern_fill(const PresentationFill& fill)
    {
        const QColor background(QString::fromStdString(fill.color));
        if (!presentation_pattern_supported(fill.pattern))
        {
            auto color = background;
            color.setAlphaF(std::clamp(fill.opacity, 0.0, 1.0));
            return QBrush(color);
        }
        auto foreground = QColor(QString::fromStdString(fill.pattern_foreground_color));
        foreground.setAlphaF(std::clamp(fill.pattern_foreground_opacity, 0.0, 1.0));
        auto base = background;
        base.setAlphaF(std::clamp(fill.opacity, 0.0, 1.0));
        QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
        image.fill(base);
        const int density = fill.pattern.rfind("pct", 0) == 0
            ? static_cast<int>(std::lround(std::stoi(fill.pattern.substr(3)) * 64 / 100.0))
            : 0;
        static constexpr int bayer[8][8]{{0, 48, 12, 60, 3, 51, 15, 63}, {32, 16, 44, 28, 35, 19, 47, 31},
            {8, 56, 4, 52, 11, 59, 7, 55}, {40, 24, 36, 20, 43, 27, 39, 23}, {2, 50, 14, 62, 1, 49, 13, 61},
            {34, 18, 46, 30, 33, 17, 45, 29}, {10, 58, 6, 54, 9, 57, 5, 53},
            {42, 26, 38, 22, 41, 25, 37, 21}};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                bool ink = false;
                if (fill.pattern.rfind("pct", 0) == 0)
                    ink = bayer[y][x] < density;
                else if (fill.pattern == "horz")
                    ink = y == 0;
                else if (fill.pattern == "vert")
                    ink = x == 0;
                else if (fill.pattern == "dnDiag")
                    ink = x == y;
                else if (fill.pattern == "upDiag")
                    ink = (x + y) % 8 == 0;
                else if (fill.pattern == "diagCross")
                    ink = x == y || (x + y) % 8 == 0;
                // The Office legacy hatch family treats Cross and LargeGrid as aliases.
                else if (fill.pattern == "lgGrid" || fill.pattern == "cross")
                    ink = x == 0 || y == 0;
                else if (fill.pattern == "smGrid")
                    ink = x % 4 == 0 || y % 4 == 0;
                else if (fill.pattern == "smCheck" || fill.pattern == "lgCheck")
                {
                    const int size = fill.pattern == "smCheck" ? 2 : 4;
                    ink = (x / size + y / size) % 2 == 0;
                }
                if (ink)
                    image.setPixelColor(x, y, foreground);
            }
        QBrush brush(image);
        QTransform transform;
        transform.scale(0.75, 0.75);
        brush.setTransform(transform);
        return brush;
    }

    QBrush presentation_image_fill(
        const RenderPresentationPtr& document, const PresentationFill& fill, const QRectF& bounds)
    {
        auto source = presentation_image(document, fill.image_path);
        if (source.isNull() || bounds.isEmpty())
            return Qt::NoBrush;
        const double dpi = fill.image_dpi > 0
            ? fill.image_dpi
            : (source.dotsPerMeterX() > 0 ? source.dotsPerMeterX() * 0.0254 : 96);
        const auto& crop = fill.image_crop;
        const QSize original = presentation_image_source_size(document, fill.image_path, source.size());
        const QSizeF physical_crop(
            original.width() * (1 - crop[0] - crop[2]), original.height() * (1 - crop[1] - crop[3]));
        const QRect cropped(qRound(source.width() * crop[0]), qRound(source.height() * crop[1]),
            std::max(1, qRound(source.width() * (1 - crop[0] - crop[2]))),
            std::max(1, qRound(source.height() * (1 - crop[1] - crop[3]))));
        const bool flip_x = fill.image_tile && (fill.image_flip == "x" || fill.image_flip == "xy");
        const bool flip_y = fill.image_tile && (fill.image_flip == "y" || fill.image_flip == "xy");
        QString key = QString::number(source.cacheKey()) + ':' + QString::number(fill.opacity, 'g', 10) +
            ':' + QString::number(cropped.x()) + ':' + QString::number(cropped.y()) + ':' +
            QString::number(cropped.width()) + ':' + QString::number(cropped.height()) + ':' +
            QString::number(flip_x) + ':' + QString::number(flip_y);
        thread_local QCache<QString, QImage> cache(32 * 1024);
        QImage image;
        if (auto found = cache.object(key))
            image = *found;
        else
        {
            source = source.copy(cropped);
            if ((flip_x || flip_y) && source.width() * source.height() > 1024 * 1024)
                source = source.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            image = QImage(source.width() * (flip_x ? 2 : 1), source.height() * (flip_y ? 2 : 1),
                QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setOpacity(std::clamp(fill.opacity, 0.0, 1.0));
            painter.drawImage(0, 0, source);
            if (flip_x)
                painter.drawImage(source.width(), 0, source.mirrored(true, false));
            if (flip_y)
                painter.drawImage(0, source.height(), source.mirrored(false, true));
            if (flip_x && flip_y)
                painter.drawImage(source.width(), source.height(), source.mirrored(true, true));
            painter.end();
            cache.insert(key, new QImage(image), static_cast<int>((image.sizeInBytes() + 1023) / 1024));
        }
        QTransform mapping;
        if (fill.image_tile)
        {
            const double tile_width = physical_crop.width() * 72 / dpi * fill.image_scale[0];
            const double tile_height = physical_crop.height() * 72 / dpi * fill.image_scale[1];
            double x = bounds.left(), y = bounds.top();
            if (fill.image_alignment == "ctr" || fill.image_alignment == "t" || fill.image_alignment == "b")
                x += (bounds.width() - tile_width) / 2;
            else if (fill.image_alignment == "r" || fill.image_alignment == "tr" ||
                fill.image_alignment == "br")
                x += bounds.width() - tile_width;
            if (fill.image_alignment == "ctr" || fill.image_alignment == "l" || fill.image_alignment == "r")
                y += (bounds.height() - tile_height) / 2;
            else if (fill.image_alignment == "b" || fill.image_alignment == "bl" ||
                fill.image_alignment == "br")
                y += bounds.height() - tile_height;
            mapping.translate(x + fill.image_offset[0], y + fill.image_offset[1]);
            mapping.scale(tile_width * (flip_x ? 2 : 1) / image.width(),
                tile_height * (flip_y ? 2 : 1) / image.height());
        }
        else
        {
            const auto& rect = fill.image_fill_rect;
            if (std::any_of(rect.begin(), rect.end(), [](double value)
            {
                return value != 0;
            }))
            {
                const double scale = std::min(2.0, 2048.0 / std::max(bounds.width(), bounds.height()));
                const QSize size(std::max(1, qRound(bounds.width() * scale)),
                    std::max(1, qRound(bounds.height() * scale)));
                QString stretch_key =
                    key + ":stretch:" + QString::number(size.width()) + ':' + QString::number(size.height());
                for (const double side : rect)
                    stretch_key += ':' + QString::number(side, 'g', 10);
                if (auto found = cache.object(stretch_key))
                    image = *found;
                else
                {
                    QImage stretched(size, QImage::Format_ARGB32_Premultiplied);
                    stretched.fill(Qt::transparent);
                    QPainter painter(&stretched);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform);
                    painter.drawImage(
                        QRectF(size.width() * rect[0], size.height() * rect[1],
                            size.width() * (1 - rect[0] - rect[2]), size.height() * (1 - rect[1] - rect[3])),
                        image);
                    painter.end();
                    image = std::move(stretched);
                    cache.insert(stretch_key, new QImage(image),
                        static_cast<int>((image.sizeInBytes() + 1023) / 1024));
                }
            }
            mapping.translate(bounds.left(), bounds.top());
            mapping.scale(bounds.width() / image.width(), bounds.height() / image.height());
        }
        QBrush brush(image);
        brush.setTransform(mapping);
        return brush;
    }
}
