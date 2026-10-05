#include "presentation_edit_layers.hpp"

#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    bool PresentationEditLayers::paint(QPainter& painter, const RenderPresentationPtr& document, int slide,
        int selected, int editing, const QVariantMap& theme, const QSizeF& viewport, const PaintRange& draw,
        bool allow_rebuild)
    {
        if (!document || !document->scene || slide < 0 ||
            slide >= static_cast<int>(document->scene->slides.size()) || viewport.isEmpty())
            return false;
        const int count = static_cast<int>(document->scene->slides[slide].shapes.size());
        if (selected < 0 || selected >= count)
            selected = -1;
        const qreal ratio = painter.device()->devicePixelRatioF();
        const double width = viewport.width() * ratio;
        const double height = viewport.height() * ratio;
        if (!std::isfinite(width) || !std::isfinite(height))
            return false;
        // Two bounded RGBA layers cost at most 32 MiB, independent of source image sizes.
        const double scale =
            std::min({1.0, 4096.0 / width, 4096.0 / height, std::sqrt(4.0 * 1024 * 1024 / (width * height))});
        const QSize pixels(
            std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)));
        const bool retained_update = revision_ && revision_ == document->edit_layer_source_revision &&
            document->edited_slide == slide && document->edited_shape == selected && selected >= 0 &&
            document->edit_layer_change == PresentationEditLayerChange::Replace && count_ == count;
        const bool retained_removal = revision_ && revision_ == document->edit_layer_source_revision &&
            document->edited_slide == slide && document->edited_shape == selected_ && selected < 0 &&
            document->edit_layer_change == PresentationEditLayerChange::Remove && count_ == count + 1 &&
            slide_ == slide && theme_ == theme && viewport_ == viewport && pixels_ == pixels &&
            !below_.isNull();
        const bool retained_deselect = document_.lock() == document && selected < 0 && selected_ >= 0 &&
            slide_ == slide && editing < 0 && theme_ == theme && viewport_ == viewport && pixels_ == pixels &&
            count_ == count && !below_.isNull();
        if (retained_deselect)
        {
            const QRectF area(QPointF(), viewport);
            painter.drawImage(area, below_);
            draw(painter, selected_, selected_ + 1, false);
            if (!above_.isNull())
            {
                painter.drawImage(area, above_);
            }
            return true;
        }
        if (retained_removal)
        {
            selected_ = -1;
            editing_ = -1;
            count_ = count;
        }
        const bool retained = retained_update || retained_removal;
        if (!retained && !allow_rebuild)
        {
            return false;
        }
        if ((document_.lock() != document && !retained) || slide_ != slide || selected_ != selected ||
            editing_ != editing || theme_ != theme || viewport_ != viewport || pixels_ != pixels ||
            below_.isNull())
        {
            below_ = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
            above_ = selected >= 0 && selected + 1 < count
                ? QImage(pixels, QImage::Format_ARGB32_Premultiplied)
                : QImage();
            if (below_.isNull() || (selected >= 0 && selected + 1 < count && above_.isNull()))
            {
                below_ = {};
                above_ = {};
                return false;
            }
            ensure_presentation_fonts(document, static_cast<std::size_t>(slide));
            const auto render = [&](QImage& image, int first, int last, bool background)
            {
                if (image.isNull())
                    return;
                image.fill(Qt::transparent);
                QPainter layer(&image);
                layer.setRenderHints(painter.renderHints());
                layer.scale(pixels.width() / viewport.width(), pixels.height() / viewport.height());
                draw(layer, first, last, background);
            };
            render(below_, 0, selected < 0 ? count : selected, true);
            render(above_, selected + 1, count, false);
            slide_ = slide;
            selected_ = selected;
            editing_ = editing;
            count_ = count;
            theme_ = theme;
            viewport_ = viewport;
            pixels_ = pixels;
        }
        document_ = document;
        revision_ = static_cast<std::size_t>(slide) < document->thumbnail_revisions.size()
            ? document->thumbnail_revisions[slide]
            : 0;
        const QRectF area(QPointF(), viewport);
        painter.drawImage(area, below_);
        if (selected >= 0)
            draw(painter, selected, selected + 1, false);
        if (!above_.isNull())
            painter.drawImage(area, above_);
        return true;
    }
}
