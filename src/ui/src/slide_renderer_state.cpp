#include "presentation_media_session.hpp"
#include "slide_renderer.hpp"

#include <QGuiApplication>
#include <QInputMethod>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    int SlideRenderer::slideIndex() const
    {
        return slide_index_;
    }

    QVariantMap SlideRenderer::theme() const
    {
        return theme_;
    }

    void SlideRenderer::setTheme(const QVariantMap& theme)
    {
        if (theme != theme_)
        {
            theme_ = theme;
            clearRetainedFrame();
            emit themeChanged();
            scheduleFrames();
            update();
        }
    }

    void SlideRenderer::setSlideIndex(int index)
    {
        if (index != slide_index_)
        {
            const int previous_slide = slide_index_;
            finishTransition(false);
            slide_index_ = index;
            fallback_document_.reset();
            clearRetainedFrame();
            frame_ready_pending_.store(true, std::memory_order_release);
            media_->setDocument(document_, slide_index_);
            if (!startTransition(previous_slide))
            {
                restartAnimation();
            }
            emit mediaItemsChanged();
            emit slideIndexChanged();
            emit transitionStateChanged();
            emit textEditorChanged();
            scheduleFrames();
            update();
        }
    }

    int SlideRenderer::selectedShape() const
    {
        return selected_shape_;
    }

    bool SlideRenderer::deferredFrames() const
    {
        return deferred_frames_;
    }

    void SlideRenderer::setDeferredFrames(bool enabled)
    {
        if (enabled == deferred_frames_)
        {
            return;
        }
        deferred_frames_ = enabled;
        scheduleFrames();
        emit deferredFramesChanged();
        update();
    }

    void SlideRenderer::setSelectedShape(int index)
    {
        if (index != selected_shape_)
        {
            selected_shape_ = index;
            // Selection pixels belong to the QML overlay. Restarting a page render here cancels the
            // edited revision that the next optimistic operation may need as its fallback.
            emit selectedShapeChanged();
            emit textEditorChanged();
        }
    }

    int SlideRenderer::editingShape() const
    {
        return editing_shape_;
    }

    void SlideRenderer::commitTextInput()
    {
        QGuiApplication::inputMethod()->commit();
    }

    void SlideRenderer::setEditingShape(int index)
    {
        if (editing_shape_ != index)
        {
            editing_shape_ = index;
            scheduleFrames();
            emit editingShapeChanged();
            update();
        }
    }

    int SlideRenderer::hitTest(qreal x, qreal y) const
    {
        if (!document_ || !document_->scene || slide_index_ < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()) || !std::isfinite(x) ||
            !std::isfinite(y))
        {
            return -1;
        }
        const auto& scene = *document_->scene;
        if (scene.width <= 0 || scene.height <= 0 || width() <= 0 || height() <= 0)
        {
            return -1;
        }
        const double scale = std::min(width() / scene.width, height() / scene.height);
        const QPointF point((x - (width() - scene.width * scale) / 2) / scale,
            (y - (height() - scene.height * scale) / 2) / scale);
        if (!QRectF(0, 0, scene.width, scene.height).contains(point))
        {
            return -1;
        }
        const auto& shapes = scene.slides[static_cast<std::size_t>(slide_index_)].shapes;
        for (std::size_t reverse = shapes.size(); reverse > 0; --reverse)
        {
            const auto& shape = shapes[reverse - 1];
            const auto& matrix = shape.transform;
            bool invertible = false;
            const QTransform transform(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
            const auto inverse = transform.inverted(&invertible);
            if (!invertible)
            {
                continue;
            }
            const double tolerance = shape.geometry == "line" ? 4 / scale : 0;
            if (QRectF(0, 0, shape.width, shape.height)
                    .normalized()
                    .adjusted(-tolerance, -tolerance, tolerance, tolerance)
                    .contains(inverse.map(point)))
            {
                return static_cast<int>(reverse - 1);
            }
        }
        return -1;
    }

    QVariantMap SlideRenderer::clickNavigationAt(qreal x, qreal y, int last_viewed_slide) const
    {
        if (!document_ || !document_->scene || slide_index_ < 0)
            return {};
        const int shape = hitTest(x, y);
        if (shape < 0)
            return {};
        const auto result = resolve_presentation_click(*document_->scene,
            static_cast<std::size_t>(slide_index_), static_cast<std::size_t>(shape), last_viewed_slide);
        if (!result.handled)
            return {};
        if (result.end_show)
            return {{"kind", "endShow"}};
        return {{"kind", "slide"}, {"target", result.target_slide}};
    }
}
