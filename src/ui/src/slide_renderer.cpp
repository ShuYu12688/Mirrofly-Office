#include "slide_renderer.hpp"
#include "presentation_animation_painter.hpp"
#include "presentation_media_session.hpp"
#include "presentation_playback.hpp"
#include "presentation_render_scheduler.hpp"
#include "slide_renderer_paint.hpp"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QQuickWindow>
#include <QScopedValueRollback>
#include <QVariantAnimation>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    Q_LOGGING_CATEGORY(pptxCanvasLatency, "mirrorfly.pptx.latency", QtInfoMsg)

    struct CanvasPaintTrace
    {
        CanvasPaintTrace(bool active, int slide, bool deferred) : active(active), slide(slide)
        {
            if (active)
            {
                timer.start();
                qCDebug(pptxCanvasLatency) << "canvas.paint.begin" << slide << deferred;
            }
        }

        ~CanvasPaintTrace()
        {
            if (active)
            {
                qCDebug(pptxCanvasLatency)
                    << "canvas.paint.end.ms" << slide << timer.nsecsElapsed() / 1000000.0;
            }
        }

        bool active;
        int slide;
        QElapsedTimer timer;
    };

    constexpr double maximum_texture_pixels = 8 * 1024 * 1024;
    constexpr double maximum_texture_side = 4096;

    using mirrorfly::slide_paint::animation_space;
    using mirrorfly::slide_paint::document_color;
    using mirrorfly::slide_paint::draw_shape;
    using mirrorfly::slide_paint::fill_brush;
    using mirrorfly::slide_paint::paint_shape_patch;
    using mirrorfly::slide_paint::preview_shape;
    using mirrorfly::slide_paint::run_font;
    using mirrorfly::slide_paint::shape_effect_padding;
    using mirrorfly::slide_paint::shape_has_deferred_asset;
    using mirrorfly::slide_paint::shape_scene_bounds;
    using mirrorfly::slide_paint::slide_has_deferred_assets;
    using mirrorfly::slide_paint::utf8;

}

namespace mirrorfly
{
    QSize bounded_slide_texture(const QSizeF& logical_size, qreal pixel_ratio)
    {
        const double ratio = std::isfinite(pixel_ratio) && pixel_ratio > 0 ? pixel_ratio : 1;
        double width = std::max(1.0, logical_size.width() * ratio);
        double height = std::max(1.0, logical_size.height() * ratio);
        if (!std::isfinite(width) || !std::isfinite(height))
        {
            return QSize(1, 1);
        }
        const double scale = std::min({1.0, maximum_texture_side / width, maximum_texture_side / height,
            std::sqrt(maximum_texture_pixels / (width * height))});
        width = std::max(1.0, std::floor(std::floor(width * scale) / ratio));
        height = std::max(1.0, std::floor(std::floor(height * scale) / ratio));
        return QSize(static_cast<int>(width), static_cast<int>(height));
    }

    SlideRenderer::SlideRenderer(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        interaction_update_timer_.setSingleShot(true);
        connect(&interaction_update_timer_, &QTimer::timeout, this, [this]()
        {
            const QRect damaged = pending_interaction_damage_;
            pending_interaction_damage_ = {};
            const bool full = pending_interaction_full_;
            pending_interaction_full_ = false;
            if (full || damaged.isEmpty())
            {
                update();
            }
            else
            {
                update(damaged);
            }
        });
        render_scheduler_ = new PresentationRenderScheduler(this);
        connect(render_scheduler_, &PresentationRenderScheduler::frameReady, this, [this](int slide)
        {
            const bool trace = pptxCanvasLatency().isDebugEnabled();
            QElapsedTimer frame_timer;
            if (trace)
            {
                frame_timer.start();
                qCDebug(pptxCanvasLatency) << "canvas.frame-ready.begin" << slide;
            }
            if (slide == slide_index_ || slide == transition_from_slide_)
            {
                commitCompletedFrame(slide);
                emit textEditorChanged();
                if (trace)
                {
                    qCDebug(pptxCanvasLatency) << "canvas.frame-ready.committed.ms" << frame_timer.elapsed();
                }
                update();
            }
            if (trace)
            {
                qCDebug(pptxCanvasLatency) << "canvas.frame-ready.end.ms" << frame_timer.elapsed();
            }
        });
        playback_ = new PresentationPlayback(this);
        connect(playback_, &PresentationPlayback::changed, this, [this]
        {
            emit animationStateChanged();
            if (!updating_document_)
            {
                update();
            }
        });
        media_ = new PresentationMediaSession(this);
        connect(playback_, &PresentationPlayback::mediaReset, media_,
            &PresentationMediaSession::resetCurrentSlide);
        connect(playback_, &PresentationPlayback::mediaCue, this,
            [this](const QString& target, const QString& action, double position, double volume, bool loop,
                int slide_count)
        {
            if (!document_ || !document_->scene || slide_index_ < 0 ||
                slide_index_ >= static_cast<int>(document_->scene->slides.size()))
                return;
            const auto& slide = document_->scene->slides[slide_index_];
            for (std::size_t index = 0; index < slide.shapes.size(); ++index)
            {
                const auto& shape = slide.shapes[index];
                if (shape.source_id != target.toStdString() || shape.media_path.empty() ||
                    (!shape.source_part.empty() && shape.source_part != slide.source_part))
                    continue;
                const int item = static_cast<int>(index);
                if (action == "play")
                {
                    media_->command(item, "volume", volume);
                    media_->command(item, "loop", loop ? 1 : 0);
                    media_->command(item, "slides", slide_count);
                    if (position >= 0)
                        media_->command(item, "seek", position);
                }
                media_->command(item, action);
            }
        });
        connect(media_, &PresentationMediaSession::frameChanged, this, [this]
        {
            if (!updating_document_)
            {
                update();
            }
        });
        connect(media_, &PresentationMediaSession::stateChanged, this, &SlideRenderer::mediaStatesChanged);
        transition_animation_ = new QVariantAnimation(this);
        transition_animation_->setStartValue(0.0);
        transition_animation_->setEndValue(1.0);
        connect(transition_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value)
        {
            transition_progress_ = value.toDouble();
            emit transitionStateChanged();
            update();
        });
        connect(transition_animation_, &QVariantAnimation::finished, this, [this]
        {
            finishTransition(true);
        });
        setAntialiasing(true);
        setImplicitSize(960, 540);
        setTextureSize(QSize(960, 540));
    }

    QVariant SlideRenderer::document() const
    {
        return document_ ? QVariant::fromValue(document_) : QVariant{};
    }

    void SlideRenderer::setDocument(const QVariant& document)
    {
        const auto next = document.value<RenderPresentationPtr>();
        if (next == document_)
        {
            return;
        }
        const bool trace = pptxCanvasLatency().isDebugEnabled();
        QElapsedTimer document_timer;
        if (trace)
        {
            document_timer.start();
            qCDebug(pptxCanvasLatency) << "canvas.document.begin" << slide_index_;
        }
        finishTransition(false);
        interaction_update_timer_.stop();
        pending_interaction_damage_ = {};
        pending_interaction_full_ = false;
        const bool keeps_previous_frame = next && document_ && next->edit_layer_source_revision != 0 &&
            next->edited_slide == slide_index_ && slide_index_ >= 0 &&
            static_cast<std::size_t>(slide_index_) < document_->thumbnail_revisions.size() &&
            document_->thumbnail_revisions[static_cast<std::size_t>(slide_index_)] ==
                next->edit_layer_source_revision;
        QRect damaged;
        if (keeps_previous_frame)
        {
            damaged = prepareRetainedEditFrame(next, document_);
        }
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.patch.ms" << document_timer.elapsed() << damaged;
        }
        fallback_document_ = keeps_previous_frame ? document_ : RenderPresentationPtr{};
        if (!keeps_previous_frame)
        {
            clearRetainedFrame();
        }
        document_ = next;
        interaction_document_ = presentation_interaction_document(document_);
        frame_ready_pending_.store(true, std::memory_order_release);
        {
            const QScopedValueRollback<bool> updating_document(updating_document_, true);
            media_->setDocument(document_, slide_index_);
            restartAnimation();
        }
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.media.ms" << document_timer.elapsed();
        }
        emit mediaItemsChanged();
        if (document_ && document_->scene)
        {
            setImplicitSize(document_->scene->width * 4 / 3, document_->scene->height * 4 / 3);
        }
        else
        {
            setImplicitSize(960, 540);
        }
        emit documentChanged();
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.signal.ms" << document_timer.elapsed();
        }
        emit transitionStateChanged();
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.transition-signal.ms" << document_timer.elapsed();
        }
        emit textEditorChanged();
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.editor-signal.ms" << document_timer.elapsed();
        }
        scheduleFrames();
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.scheduled.ms" << document_timer.elapsed();
        }
        if (damaged.isEmpty())
        {
            update();
        }
        else
        {
            update(damaged);
        }
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.updated.ms" << document_timer.elapsed();
        }
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.document.end.ms" << document_timer.elapsed();
        }
    }

    QVariantMap SlideRenderer::textEditorState() const
    {
        const bool trace = pptxCanvasLatency().isDebugEnabled();
        QElapsedTimer editor_timer;
        if (trace)
        {
            editor_timer.start();
            qCDebug(pptxCanvasLatency) << "canvas.editor-state.begin" << slide_index_ << selected_shape_;
        }
        if (!document_ || !document_->scene || slide_index_ < 0 || selected_shape_ < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()))
        {
            return {};
        }
        const auto& scene = *document_->scene;
        const auto& slide = scene.slides[static_cast<std::size_t>(slide_index_)];
        if (selected_shape_ >= static_cast<int>(slide.shapes.size()) || scene.width <= 0 || scene.height <= 0)
        {
            return {};
        }
        const auto& shape = slide.shapes[static_cast<std::size_t>(selected_shape_)];
        if (!shape.editable || !shape.image_path.empty() || shape.geometry == "line" || shape.width <= 0 ||
            shape.height <= 0)
        {
            return {};
        }
        const auto& matrix = shape.transform;
        const double scale = std::min(width() / scene.width, height() / scene.height);
        const QTransform transform(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
        if (!transform.isInvertible() || !std::isfinite(scale) || scale <= 0)
        {
            return {};
        }
        QStringList paragraphs;
        for (const auto& paragraph : shape.text.paragraphs)
        {
            QString text;
            for (const auto& run : paragraph.runs)
            {
                text += utf8(run.text);
            }
            paragraphs.append(text);
        }
        const PresentationRun run =
            shape.text.paragraphs.empty() || shape.text.paragraphs.front().runs.empty()
            ? PresentationRun{}
            : shape.text.paragraphs.front().runs.front();
        const auto characters = paragraphs.join('\n').toUcs4();
        const bool contains_cjk =
            std::any_of(characters.begin(), characters.end(), mirrorfly::presentation_east_asian_character);
        // Selection and QML binding refreshes only observe prepared fonts; rendering loads them.
        const QFont font = run_font(document_, run, shape.text.font_scale, contains_cjk);
        if (trace)
        {
            qCDebug(pptxCanvasLatency) << "canvas.editor-state.font.ms" << editor_timer.elapsed();
        }
        const QString alignment = shape.text.paragraphs.empty()
            ? QStringLiteral("left")
            : utf8(shape.text.paragraphs.front().alignment);
        return {{"valid", true}, {"id", QString::number(shape.id)}, {"text", paragraphs.join('\n')},
            {"hasText", !shape.text.paragraphs.empty()}, {"width", shape.width}, {"height", shape.height},
            {"a", matrix[0] * scale}, {"b", matrix[1] * scale}, {"c", matrix[2] * scale},
            {"d", matrix[3] * scale}, {"tx", (width() - scene.width * scale) / 2 + matrix[4] * scale},
            {"ty", (height() - scene.height * scale) / 2 + matrix[5] * scale},
            {"fontFamily", font.families().value(0)}, {"font", font}, {"fontSize", font.pixelSize()},
            {"bold", run.bold}, {"italic", run.italic}, {"underline", run.underline},
            {"color", document_color(run.color, run.opacity)}, {"left", shape.text.inset_left},
            {"right", shape.text.inset_right}, {"top", shape.text.inset_top},
            {"bottom", shape.text.inset_bottom}, {"wrap", shape.text.wrap},
            {"vertical", utf8(shape.text.vertical_alignment)}, {"alignment", alignment}};
    }

    QVariantMap SlideRenderer::transformPreview() const
    {
        return transform_preview_;
    }

    void SlideRenderer::setTransformPreview(const QVariantMap& preview)
    {
        if (transform_preview_ != preview)
        {
            QRect damaged;
            if (document_ && document_->scene && slide_index_ >= 0 &&
                slide_index_ < static_cast<int>(document_->scene->slides.size()) && selected_shape_ >= 0 &&
                selected_shape_ <
                    static_cast<int>(
                        document_->scene->slides[static_cast<std::size_t>(slide_index_)].shapes.size()) &&
                document_->scene->width > 0 && document_->scene->height > 0)
            {
                const auto& shape = document_->scene->slides[static_cast<std::size_t>(slide_index_)]
                                        .shapes[static_cast<std::size_t>(selected_shape_)];
                QRectF scene_damage = shape_scene_bounds(shape);
                if (!transform_preview_.isEmpty())
                {
                    scene_damage =
                        scene_damage.united(shape_scene_bounds(preview_shape(shape, transform_preview_)));
                }
                if (!preview.isEmpty())
                {
                    scene_damage = scene_damage.united(shape_scene_bounds(preview_shape(shape, preview)));
                }
                const double scale =
                    std::min(width() / document_->scene->width, height() / document_->scene->height);
                const QPointF origin((width() - document_->scene->width * scale) / 2,
                    (height() - document_->scene->height * scale) / 2);
                const QRectF logical(origin.x() + scene_damage.x() * scale,
                    origin.y() + scene_damage.y() * scale, scene_damage.width() * scale,
                    scene_damage.height() * scale);
                const QRect expanded = logical.toAlignedRect().adjusted(-2, -2, 2, 2);
                damaged = expanded.intersected(QRect(0, 0, qRound(width()), qRound(height())));
            }
            transform_preview_ = preview;
            // Preview pixels are composed from the retained static frame; the background render stays alive.
            emit transformPreviewChanged();
            queueInteractionUpdate(damaged);
        }
    }

    void SlideRenderer::geometryChange(const QRectF& geometry, const QRectF& previous)
    {
        QQuickPaintedItem::geometryChange(geometry, previous);
        clearRetainedFrame();
        const qreal ratio = window() ? window()->devicePixelRatio() : 1;
        setTextureSize(bounded_slide_texture(geometry.size(), ratio));
        scheduleFrames();
        emit textEditorChanged();
    }

    void SlideRenderer::itemChange(ItemChange change, const ItemChangeData& data)
    {
        QQuickPaintedItem::itemChange(change, data);
        if (change == ItemDevicePixelRatioHasChanged)
        {
            clearRetainedFrame();
            setTextureSize(bounded_slide_texture(size(), data.realValue));
        }
        else if (change == ItemSceneChange)
        {
            clearRetainedFrame();
            setTextureSize(bounded_slide_texture(size(), data.window ? data.window->devicePixelRatio() : 1));
        }
    }

    void SlideRenderer::notifyFrameReady()
    {
        bool pending = true;
        if (!frame_ready_pending_.compare_exchange_strong(
                pending, false, std::memory_order_acq_rel, std::memory_order_relaxed))
        {
            return;
        }
        const QPointer<SlideRenderer> guard(this);
        QMetaObject::invokeMethod(this, [guard]()
        {
            if (guard)
            {
                emit guard->frameReady();
            }
        }, Qt::QueuedConnection);
    }

    void SlideRenderer::clearRetainedFrame()
    {
        retained_frame_ = {};
        retained_frame_theme_.clear();
        retained_frame_viewport_ = {};
        retained_frame_revision_ = 0;
        retained_frame_slide_ = -1;
    }

    void SlideRenderer::commitCompletedFrame(int slide)
    {
        if (slide != slide_index_ || !document_ || slide < 0 ||
            static_cast<std::size_t>(slide) >= document_->thumbnail_revisions.size())
        {
            return;
        }
        const QSize viewport(qRound(width()), qRound(height()));
        const QImage completed = render_scheduler_->frame(document_, theme_, viewport, slide);
        if (completed.isNull())
        {
            return;
        }
        retained_frame_ = completed;
        retained_frame_theme_ = theme_;
        retained_frame_viewport_ = viewport;
        retained_frame_revision_ = document_->thumbnail_revisions[static_cast<std::size_t>(slide)];
        retained_frame_slide_ = slide;
    }

    void SlideRenderer::queueInteractionUpdate(const QRect& damaged)
    {
        if (damaged.isEmpty())
        {
            pending_interaction_full_ = true;
        }
        else
        {
            pending_interaction_damage_ = pending_interaction_damage_.united(damaged);
        }
        if (!interaction_update_timer_.isActive())
        {
            interaction_update_timer_.start(16);
        }
    }

    QImage SlideRenderer::retainedFrame(const RenderPresentationPtr& document, const QSize& viewport) const
    {
        if (!document || retained_frame_.isNull() || retained_frame_slide_ != slide_index_ ||
            retained_frame_theme_ != theme_ || retained_frame_viewport_ != viewport || slide_index_ < 0 ||
            static_cast<std::size_t>(slide_index_) >= document->thumbnail_revisions.size() ||
            retained_frame_revision_ != document->thumbnail_revisions[static_cast<std::size_t>(slide_index_)])
        {
            return {};
        }
        return retained_frame_;
    }

    void SlideRenderer::retainPaintedFrame(QPainter* painter, const QSize& viewport)
    {
        if (!painter || !document_ || slide_index_ < 0 ||
            static_cast<std::size_t>(slide_index_) >= document_->thumbnail_revisions.size())
        {
            return;
        }
        auto* image = dynamic_cast<QImage*>(painter->device());
        if (!image || image->isNull())
        {
            return;
        }
        // Keep an immutable snapshot; the QQuick image paint device is reused on later frames.
        retained_frame_ = image->copy();
        retained_frame_theme_ = theme_;
        retained_frame_viewport_ = viewport;
        retained_frame_revision_ = document_->thumbnail_revisions[static_cast<std::size_t>(slide_index_)];
        retained_frame_slide_ = slide_index_;
    }

    QRect SlideRenderer::prepareRetainedEditFrame(
        const RenderPresentationPtr& next, const RenderPresentationPtr& previous)
    {
        if (!next || !previous || !next->scene || !previous->scene || !render_scheduler_ ||
            slide_index_ < 0 || next->edited_slide != slide_index_ || next->edited_shape < 0 ||
            width() <= 0 || height() <= 0 ||
            static_cast<std::size_t>(slide_index_) >= next->thumbnail_revisions.size() ||
            slide_index_ >= static_cast<int>(next->scene->slides.size()) ||
            slide_index_ >= static_cast<int>(previous->scene->slides.size()))
        {
            return {};
        }
        const auto& previous_slide = previous->scene->slides[static_cast<std::size_t>(slide_index_)];
        if (next->edited_shape >= static_cast<int>(previous_slide.shapes.size()))
        {
            return {};
        }
        const QSize viewport(qRound(width()), qRound(height()));
        auto base = render_scheduler_->frame(previous, theme_, viewport, slide_index_);
        if (base.isNull())
        {
            base = retainedFrame(previous, viewport);
        }
        if (base.isNull())
        {
            base = cached_presentation_thumbnail(
                previous, presentation_thumbnail_key(previous, slide_index_, theme_));
        }
        if (base.isNull())
        {
            return {};
        }
        QImage composed(viewport, QImage::Format_ARGB32_Premultiplied);
        if (composed.isNull())
        {
            return {};
        }
        composed.fill(Qt::transparent);
        QPainter painter(&composed);
        painter.setRenderHints(
            QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(QPointF(), QSizeF(viewport)), base);
        const mirrorfly::PresentationShape* replacement = nullptr;
        int replacement_index = -1;
        const auto& next_slide = next->scene->slides[static_cast<std::size_t>(slide_index_)];
        if (next->edit_layer_change == PresentationEditLayerChange::Replace &&
            next->edited_shape < static_cast<int>(next_slide.shapes.size()))
        {
            replacement_index = next->edited_shape;
            replacement = &next_slide.shapes[static_cast<std::size_t>(replacement_index)];
        }
        const bool painted = paint_shape_patch(painter, presentation_interaction_document(next), slide_index_,
            theme_, viewport, previous_slide.shapes[static_cast<std::size_t>(next->edited_shape)],
            replacement_index, replacement);
        painter.end();
        if (!painted)
        {
            return {};
        }
        retained_frame_ = std::move(composed);
        retained_frame_theme_ = theme_;
        retained_frame_viewport_ = viewport;
        retained_frame_revision_ = next->thumbnail_revisions[static_cast<std::size_t>(slide_index_)];
        retained_frame_slide_ = slide_index_;
        QRectF dirty =
            shape_scene_bounds(previous_slide.shapes[static_cast<std::size_t>(next->edited_shape)]);
        if (replacement)
        {
            dirty = dirty.united(shape_scene_bounds(*replacement));
        }
        const double scale =
            std::min(viewport.width() / next->scene->width, viewport.height() / next->scene->height);
        const QPointF origin((viewport.width() - next->scene->width * scale) / 2,
            (viewport.height() - next->scene->height * scale) / 2);
        const QRectF logical(origin.x() + dirty.x() * scale, origin.y() + dirty.y() * scale,
            dirty.width() * scale, dirty.height() * scale);
        return logical.toAlignedRect().adjusted(-2, -2, 2, 2).intersected(QRect(QPoint(), viewport));
    }

    void SlideRenderer::scheduleFrames()
    {
        if (!render_scheduler_)
        {
            return;
        }
        const bool local_edit = document_ && document_->edit_layer_source_revision != 0 &&
            document_->edited_slide == slide_index_;
        const bool deferred_local_removal = deferred_frames_ && local_edit &&
            document_->edit_layer_change == PresentationEditLayerChange::Remove;
        const bool deferred = deferred_frames_ && document_ && document_->scene && slide_index_ >= 0 &&
            slide_index_ < static_cast<int>(document_->scene->slides.size());
        if ((!transitions_enabled_ && !deferred && !deferred_local_removal) || editing_shape_ >= 0 ||
            !transform_preview_.isEmpty())
        {
            render_scheduler_->cancel();
            return;
        }
        render_scheduler_->request(document_, theme_, QSize(qRound(width()), qRound(height())), slide_index_,
            transition_from_slide_, transitions_enabled_, local_edit ? 100 : 0);
    }

    void SlideRenderer::paint(QPainter* painter)
    {
        CanvasPaintTrace paint_trace(pptxCanvasLatency().isDebugEnabled(), slide_index_, deferred_frames_);
        if (!document_ || !document_->scene || slide_index_ < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()))
        {
            return;
        }
        const auto& scene = *document_->scene;
        const auto& paint_document =
            deferred_frames_ && interaction_document_ ? interaction_document_ : document_;
        if (scene.width <= 0 || scene.height <= 0 || width() <= 0 || height() <= 0)
        {
            return;
        }
        painter->setRenderHints(
            QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        const QSize viewport(qRound(width()), qRound(height()));
        // Permission to play media does not make an otherwise static slide dynamic.
        const bool dynamic_media = media_->hasVisibleFrame();
        if (transition_from_slide_ < 0 && !animationEnabled() && !dynamic_media && editing_shape_ < 0 &&
            transform_preview_.isEmpty())
        {
            const auto cached = render_scheduler_->frame(document_, theme_, viewport, slide_index_);
            if (!cached.isNull())
            {
                painter->drawImage(QRectF(0, 0, width(), height()), cached);
                commitCompletedFrame(slide_index_);
                fallback_document_.reset();
                notifyFrameReady();
                return;
            }
            if (deferred_frames_)
            {
                const auto retained = retainedFrame(document_, viewport);
                if (!retained.isNull())
                {
                    painter->drawImage(QRectF(0, 0, width(), height()), retained);
                    notifyFrameReady();
                    return;
                }
            }
        }
        const auto& slide = scene.slides[static_cast<std::size_t>(slide_index_)];
        const bool current_slide_was_edited =
            document_->edit_layer_source_revision != 0 && document_->edited_slide == slide_index_;
        const bool selection_only =
            selected_shape_ >= 0 && editing_shape_ < 0 && transform_preview_.isEmpty();
        if (deferred_frames_ && !current_slide_was_edited && transition_from_slide_ < 0 &&
            !animationEnabled() && !dynamic_media && (selection_only || slide_has_deferred_assets(slide)) &&
            render_scheduler_->frame(document_, theme_, viewport, slide_index_).isNull())
        {
            const auto thumbnail = cached_presentation_thumbnail(
                document_, presentation_thumbnail_key(document_, slide_index_, theme_));
            if (!thumbnail.isNull())
            {
                painter->setRenderHint(QPainter::SmoothPixmapTransform);
                painter->drawImage(QRectF(0, 0, width(), height()), thumbnail);
                retainPaintedFrame(painter, viewport);
                return;
            }
            const double scale = std::min(width() / scene.width, height() / scene.height);
            painter->save();
            painter->translate((width() - scene.width * scale) / 2, (height() - scene.height * scale) / 2);
            painter->scale(scale, scale);
            const QRectF bounds(0, 0, scene.width, scene.height);
            painter->setClipRect(bounds);
            painter->fillRect(bounds, Qt::white);
            if (slide.background.image_path.empty())
            {
                painter->fillRect(bounds, fill_brush(slide.background, bounds, paint_document));
            }
            for (const auto& shape : slide.shapes)
            {
                if (!selection_only && !shape_has_deferred_asset(shape))
                {
                    draw_shape(painter, paint_document, shape, theme_, false);
                }
            }
            painter->restore();
            return;
        }
        if (transition_from_slide_ < 0 && !animationEnabled() && !dynamic_media)
        {
            const bool deferred_local_removal = deferred_frames_ &&
                document_->edit_layer_source_revision != 0 && document_->edited_slide == slide_index_ &&
                document_->edit_layer_change == PresentationEditLayerChange::Remove;
            const double scale = std::min(width() / scene.width, height() / scene.height);
            const auto setup = [&](QPainter& target)
            {
                target.translate((width() - scene.width * scale) / 2, (height() - scene.height * scale) / 2);
                target.scale(scale, scale);
                target.setClipRect(QRectF(0, 0, scene.width, scene.height), Qt::IntersectClip);
            };
            const auto draw = [&](QPainter& target, int first, int last, bool background)
            {
                target.save();
                setup(target);
                if (background)
                {
                    const QRectF bounds(0, 0, scene.width, scene.height);
                    target.fillRect(bounds, Qt::white);
                    target.fillRect(bounds, fill_brush(slide.background, bounds, paint_document));
                }
                for (int index = first; index < last; ++index)
                {
                    if (index == selected_shape_ && !transform_preview_.isEmpty())
                        draw_shape(&target, paint_document,
                            preview_shape(slide.shapes[index], transform_preview_), theme_,
                            index == editing_shape_);
                    else
                        draw_shape(
                            &target, paint_document, slide.shapes[index], theme_, index == editing_shape_);
                }
                target.restore();
            };
            if (!transform_preview_.isEmpty() && selected_shape_ >= 0 &&
                selected_shape_ < static_cast<int>(slide.shapes.size()))
            {
                auto base = render_scheduler_->frame(document_, theme_, viewport, slide_index_);
                if (base.isNull())
                {
                    base = retainedFrame(document_, viewport);
                }
                if (base.isNull())
                {
                    base = cached_presentation_thumbnail(
                        document_, presentation_thumbnail_key(document_, slide_index_, theme_));
                }
                if (!base.isNull())
                {
                    const auto replacement = preview_shape(
                        slide.shapes[static_cast<std::size_t>(selected_shape_)], transform_preview_);
                    painter->drawImage(QRectF(0, 0, width(), height()), base);
                    if (paint_shape_patch(*painter, interaction_document_, slide_index_, theme_,
                            QSizeF(width(), height()),
                            slide.shapes[static_cast<std::size_t>(selected_shape_)], selected_shape_,
                            &replacement))
                    {
                        return;
                    }
                }
            }
            const bool needs_edit_layers = editing_shape_ >= 0 || !transform_preview_.isEmpty();
            if ((needs_edit_layers || deferred_local_removal) &&
                edit_layers_.paint(*painter, paint_document, slide_index_, selected_shape_, editing_shape_,
                    theme_, QSizeF(width(), height()), draw, needs_edit_layers))
            {
                retainPaintedFrame(painter, viewport);
                notifyFrameReady();
                return;
            }
            if (deferred_local_removal)
            {
                auto fallback = fallback_document_
                    ? render_scheduler_->frame(fallback_document_, theme_, viewport, slide_index_)
                    : QImage{};
                if (fallback.isNull() && fallback_document_)
                {
                    fallback = retainedFrame(fallback_document_, viewport);
                }
                if (fallback.isNull() && fallback_document_)
                {
                    fallback = cached_presentation_thumbnail(fallback_document_,
                        presentation_thumbnail_key(fallback_document_, slide_index_, theme_));
                }
                if (!fallback.isNull())
                {
                    painter->drawImage(QRectF(0, 0, width(), height()), fallback);
                    if (fallback_document_->scene && slide_index_ >= 0 &&
                        slide_index_ < static_cast<int>(fallback_document_->scene->slides.size()))
                    {
                        const auto& previous_slide =
                            fallback_document_->scene->slides[static_cast<std::size_t>(slide_index_)];
                        const int removed = document_->edited_shape;
                        if (removed >= 0 && removed < static_cast<int>(previous_slide.shapes.size()))
                        {
                            const auto& shape = previous_slide.shapes[static_cast<std::size_t>(removed)];
                            const auto& matrix = shape.transform;
                            const double padding = shape_effect_padding(shape);
                            QRectF local(0, 0, shape.width, shape.height);
                            local.adjust(-padding, -padding, padding, padding);
                            const QTransform transform(
                                matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
                            QPainterPath erased;
                            erased.addRect(local);
                            const auto dirty = transform.map(erased);
                            const auto dirty_bounds = dirty.boundingRect();
                            painter->save();
                            setup(*painter);
                            painter->setClipPath(dirty, Qt::IntersectClip);
                            const QRectF bounds(0, 0, scene.width, scene.height);
                            painter->fillRect(bounds, Qt::white);
                            if (slide.background.image_path.empty())
                            {
                                painter->fillRect(
                                    bounds, fill_brush(slide.background, bounds, paint_document));
                            }
                            for (const auto& current : slide.shapes)
                            {
                                if (!shape_has_deferred_asset(current) &&
                                    shape_scene_bounds(current).intersects(dirty_bounds))
                                {
                                    draw_shape(painter, paint_document, current, theme_, false);
                                }
                            }
                            painter->restore();
                        }
                    }
                    retainPaintedFrame(painter, viewport);
                }
                else
                {
                    // A visible page normally has a retained frame. If the renderer has never painted,
                    // keep this exceptional path non-blocking and let the scheduled full frame replace it.
                    painter->save();
                    setup(*painter);
                    const QRectF bounds(0, 0, scene.width, scene.height);
                    painter->fillRect(bounds, Qt::white);
                    if (slide.background.image_path.empty())
                    {
                        painter->fillRect(bounds, fill_brush(slide.background, bounds, paint_document));
                    }
                    painter->restore();
                    retainPaintedFrame(painter, viewport);
                }
                return;
            }
        }
        if (transition_from_slide_ >= 0 && transition_progress_ < 1)
        {
            const auto& transition = scene.slides[static_cast<std::size_t>(slide_index_)].transition;
            const QRectF area(0, 0, width(), height());
            const double progress = std::clamp(transition_progress_, 0.0, 1.0);
            const auto draw =
                [&](int index, double opacity, const QPointF& offset, double zoom, const QRectF& clip)
            {
                painter->save();
                if (!clip.isNull())
                {
                    painter->setClipRect(clip, Qt::IntersectClip);
                }
                painter->setOpacity(opacity);
                painter->translate(offset);
                if (zoom != 1)
                {
                    painter->translate(area.center());
                    painter->scale(zoom, zoom);
                    painter->translate(-area.center());
                }
                const auto cached = render_scheduler_->frame(document_, theme_, viewport, index);
                if (cached.isNull())
                {
                    paint_presentation_slide(
                        *painter, paint_document, static_cast<std::size_t>(index), theme_, area);
                }
                else
                {
                    painter->drawImage(area, cached);
                }
                painter->restore();
            };
            const auto direction = [&]()
            {
                const QString value = QString::fromStdString(transition.direction).toLower();
                QPointF result;
                if (value.contains(QLatin1Char('l')))
                    result.setX(1);
                else if (value.contains(QLatin1Char('r')))
                    result.setX(-1);
                if (value.contains(QLatin1Char('u')))
                    result.setY(1);
                else if (value.contains(QLatin1Char('d')))
                    result.setY(-1);
                if (result.isNull())
                    result.setX(1);
                return result;
            }();
            const QPointF incoming(direction.x() * area.width() * (1 - progress),
                direction.y() * area.height() * (1 - progress));
            const QPointF outgoing(
                -direction.x() * area.width() * progress, -direction.y() * area.height() * progress);
            const std::string& type = transition.type;
            if (type == "push")
            {
                draw(transition_from_slide_, 1, outgoing, 1, {});
                draw(slide_index_, 1, incoming, 1, {});
            }
            else if (type == "cover")
            {
                draw(transition_from_slide_, 1, {}, 1, {});
                draw(slide_index_, 1, incoming, 1, {});
            }
            else if (type == "pull")
            {
                draw(slide_index_, 1, {}, 1, {});
                draw(transition_from_slide_, 1, outgoing, 1, {});
            }
            else if (type == "wipe")
            {
                draw(transition_from_slide_, 1, {}, 1, {});
                QRectF clip;
                if (direction.x() > 0)
                    clip = QRectF(area.width() * (1 - progress), 0, area.width() * progress, area.height());
                else if (direction.x() < 0)
                    clip = QRectF(0, 0, area.width() * progress, area.height());
                else if (direction.y() > 0)
                    clip = QRectF(0, area.height() * (1 - progress), area.width(), area.height() * progress);
                else
                    clip = QRectF(0, 0, area.width(), area.height() * progress);
                draw(slide_index_, 1, {}, 1, clip);
            }
            else if (type == "split")
            {
                draw(transition_from_slide_, 1, {}, 1, {});
                const bool horizontal = transition.orientation == "horz";
                const bool inward = transition.direction == "in";
                if (horizontal && inward)
                {
                    draw(slide_index_, 1, {}, 1, QRectF(0, 0, area.width(), area.height() * progress / 2));
                    draw(slide_index_, 1, {}, 1,
                        QRectF(0, area.height() * (1 - progress / 2), area.width(),
                            area.height() * progress / 2));
                }
                else if (horizontal)
                {
                    draw(slide_index_, 1, {}, 1,
                        QRectF(
                            0, area.height() * (1 - progress) / 2, area.width(), area.height() * progress));
                }
                else if (inward)
                {
                    draw(slide_index_, 1, {}, 1, QRectF(0, 0, area.width() * progress / 2, area.height()));
                    draw(slide_index_, 1, {}, 1,
                        QRectF(area.width() * (1 - progress / 2), 0, area.width() * progress / 2,
                            area.height()));
                }
                else
                {
                    draw(slide_index_, 1, {}, 1,
                        QRectF(area.width() * (1 - progress) / 2, 0, area.width() * progress, area.height()));
                }
            }
            else if (type == "zoom")
            {
                draw(transition_from_slide_, 1, {}, 1, {});
                draw(slide_index_, progress, {}, 0.85 + 0.15 * progress, {});
            }
            else
            {
                draw(transition_from_slide_, 1, {}, 1, {});
                draw(slide_index_, progress, {}, 1, {});
            }
            notifyFrameReady();
            return;
        }
        painter->save();
        const double scale = std::min(width() / scene.width, height() / scene.height);
        painter->translate((width() - scene.width * scale) / 2, (height() - scene.height * scale) / 2);
        painter->scale(scale, scale);
        const QRectF bounds(0, 0, scene.width, scene.height);
        painter->setClipRect(bounds);
        painter->fillRect(bounds, Qt::white);
        painter->fillRect(bounds, fill_brush(slide.background, bounds, paint_document));
        for (std::size_t index = 0; index < slide.shapes.size(); ++index)
        {
            painter->save();
            const auto& original = slide.shapes[index];
            const bool animated = animationEnabled() &&
                (original.source_part.empty() || original.source_part == slide.source_part);
            bool visible = true;
            std::string inherited_color;
            if (animated)
                for (auto group = original.source_groups.rbegin(); group != original.source_groups.rend();
                    ++group)
                {
                    const auto frame = playback_->group(*group);
                    if (!frame)
                        continue;
                    const auto state = playback_->state(*group, scene.width, scene.height);
                    if (!apply_presentation_animation(*painter, *frame, state))
                    {
                        visible = false;
                        break;
                    }
                    if (!state.color.empty())
                        inherited_color = state.color;
                }
            PresentationAnimationState state;
            if (animated)
                state = playback_->state(original.source_id, scene.width, scene.height);
            std::vector<PresentationAnimationState> paragraph_states;
            std::vector<std::vector<PresentationAnimationState>> character_states;
            if (animated && !original.text.paragraphs.empty())
            {
                paragraph_states.reserve(original.text.paragraphs.size());
                for (std::size_t paragraph = 0; paragraph < original.text.paragraphs.size(); ++paragraph)
                {
                    paragraph_states.push_back(playback_->paragraphState(
                        original.source_id, static_cast<int>(paragraph), scene.width, scene.height));
                }
                const bool has_character_animation = std::any_of(slide.animations.begin(),
                    slide.animations.end(), [&](const PresentationAnimation& animation)
                {
                    return animation.target == original.source_id &&
                        (animation.character_start >= 0 || !animation.iterate_type.empty());
                });
                if (has_character_animation)
                {
                    int letter_count = 0;
                    int word_count = 0;
                    for (std::size_t paragraph = 0; paragraph < original.text.paragraphs.size(); ++paragraph)
                    {
                        bool in_word = false;
                        for (const auto& run : original.text.paragraphs[paragraph].runs)
                            for (const auto character : utf8(run.text).toStdU32String())
                            {
                                const bool space = animation_space(character);
                                if (!space)
                                {
                                    ++letter_count;
                                    if (!in_word)
                                        ++word_count;
                                }
                                in_word = !space;
                            }
                    }
                    character_states.reserve(original.text.paragraphs.size());
                    int global_character = 0;
                    int letter = 0;
                    int word = -1;
                    for (std::size_t paragraph = 0; paragraph < original.text.paragraphs.size(); ++paragraph)
                    {
                        bool in_word = false;
                        std::vector<PresentationAnimationState> states;
                        for (const auto& run : original.text.paragraphs[paragraph].runs)
                        {
                            for (const auto character : utf8(run.text).toStdU32String())
                            {
                                const bool space = animation_space(character);
                                if (!space && !in_word)
                                    ++word;
                                PresentationAnimationTextPosition position;
                                position.character = global_character++;
                                position.letter = space ? std::max(0, letter - 1) : letter++;
                                position.letter_count = letter_count;
                                position.word = space ? std::max(0, word) : word;
                                position.word_count = word_count;
                                position.element = static_cast<int>(paragraph);
                                position.element_count = static_cast<int>(original.text.paragraphs.size());
                                states.push_back(playback_->characterState(
                                    original.source_id, position, scene.width, scene.height));
                                in_word = !space;
                            }
                        }
                        character_states.push_back(std::move(states));
                        if (paragraph + 1 < original.text.paragraphs.size())
                            ++global_character;
                    }
                }
            }
            if (state.color.empty())
                state.color = inherited_color;
            if (!visible || !apply_presentation_animation(*painter, original, state))
            {
                painter->restore();
                continue;
            }
            if (static_cast<int>(index) == selected_shape_ && !transform_preview_.isEmpty())
            {
                draw_shape(painter, paint_document, preview_shape(slide.shapes[index], transform_preview_),
                    theme_, false, media_->frame(static_cast<int>(index)), paragraph_states,
                    character_states);
            }
            else
            {
                if (state.color.empty())
                    draw_shape(painter, paint_document, original, theme_,
                        static_cast<int>(index) == editing_shape_, media_->frame(static_cast<int>(index)),
                        paragraph_states, character_states);
                else
                {
                    auto colored = original;
                    colored.fill.color = state.color;
                    colored.fill.stops.clear();
                    for (auto& paragraph : colored.text.paragraphs)
                        for (auto& run : paragraph.runs)
                            run.color = state.color;
                    draw_shape(painter, paint_document, colored, theme_, false,
                        media_->frame(static_cast<int>(index)), paragraph_states, character_states);
                }
            }
            painter->restore();
        }
        painter->restore();
        notifyFrameReady();
    }

}
