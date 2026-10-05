#pragma once

#include "presentation_edit_layers.hpp"
#include "presentation_scene.hpp"

#include <QFutureWatcher>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <atomic>

class QVariantAnimation;

namespace mirrorfly
{
    class PresentationMediaSession;
    class PresentationPlayback;
    class PresentationRenderScheduler;
    QSize bounded_slide_texture(const QSizeF& logical_size, qreal pixel_ratio);

    class SlideRenderer : public QQuickPaintedItem
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(SlideView)
        Q_PROPERTY(QVariant document READ document WRITE setDocument NOTIFY documentChanged)
        Q_PROPERTY(QVariantMap theme READ theme WRITE setTheme NOTIFY themeChanged)
        Q_PROPERTY(int slideIndex READ slideIndex WRITE setSlideIndex NOTIFY slideIndexChanged)
        Q_PROPERTY(int selectedShape READ selectedShape WRITE setSelectedShape NOTIFY selectedShapeChanged)
        Q_PROPERTY(int editingShape READ editingShape WRITE setEditingShape NOTIFY editingShapeChanged)
        Q_PROPERTY(bool mediaEnabled READ mediaEnabled WRITE setMediaEnabled NOTIFY mediaEnabledChanged)
        Q_PROPERTY(QVariantList mediaItems READ mediaItems NOTIFY mediaItemsChanged)
        Q_PROPERTY(QVariantMap mediaStates READ mediaStates NOTIFY mediaStatesChanged)
        Q_PROPERTY(bool animationEnabled READ animationEnabled WRITE setAnimationEnabled NOTIFY
                animationEnabledChanged)
        Q_PROPERTY(QVariantMap animationState READ animationState NOTIFY animationStateChanged)
        Q_PROPERTY(bool transitionsEnabled READ transitionsEnabled WRITE setTransitionsEnabled NOTIFY
                transitionsEnabledChanged)
        Q_PROPERTY(
            bool deferredFrames READ deferredFrames WRITE setDeferredFrames NOTIFY deferredFramesChanged)
        Q_PROPERTY(QVariantMap transitionState READ transitionState NOTIFY transitionStateChanged)
        Q_PROPERTY(QVariantMap textEditorState READ textEditorState NOTIFY textEditorChanged)
        Q_PROPERTY(QVariantMap transformPreview READ transformPreview WRITE setTransformPreview NOTIFY
                transformPreviewChanged)

    public:
        explicit SlideRenderer(QQuickItem* parent = nullptr);
        QVariant document() const;
        void setDocument(const QVariant& document);
        QVariantMap theme() const;
        void setTheme(const QVariantMap& theme);
        int slideIndex() const;
        void setSlideIndex(int index);
        int selectedShape() const;
        void setSelectedShape(int index);
        int editingShape() const;
        void setEditingShape(int index);
        bool mediaEnabled() const;
        void setMediaEnabled(bool enabled);
        QVariantList mediaItems() const;
        QVariantMap mediaStates() const;
        Q_INVOKABLE bool mediaCommand(int shape, const QString& action, double value = 0);
        bool animationEnabled() const;
        void setAnimationEnabled(bool enabled);
        QVariantMap animationState() const;
        Q_INVOKABLE bool advanceAnimation();
        Q_INVOKABLE bool triggerAnimation(int shape);
        Q_INVOKABLE bool triggerAnimationAt(qreal x, qreal y);
        Q_INVOKABLE void restartAnimation();
        Q_INVOKABLE bool seekAnimation(double time);
        bool transitionsEnabled() const;
        void setTransitionsEnabled(bool enabled);
        bool deferredFrames() const;
        void setDeferredFrames(bool enabled);
        QVariantMap transitionState() const;
        Q_INVOKABLE bool seekTransition(double progress);
        QVariantMap textEditorState() const;
        QVariantMap transformPreview() const;
        void setTransformPreview(const QVariantMap& preview);
        Q_INVOKABLE void commitTextInput();
        Q_INVOKABLE int hitTest(qreal x, qreal y) const;
        Q_INVOKABLE QVariantMap clickNavigationAt(qreal x, qreal y, int last_viewed_slide) const;
        void paint(QPainter* painter) override;

    protected:
        void geometryChange(const QRectF& geometry, const QRectF& previous) override;
        void itemChange(ItemChange change, const ItemChangeData& data) override;

    signals:
        void documentChanged();
        void themeChanged();
        void slideIndexChanged();
        void selectedShapeChanged();
        void editingShapeChanged();
        void mediaEnabledChanged();
        void mediaItemsChanged();
        void mediaStatesChanged();
        void animationEnabledChanged();
        void animationStateChanged();
        void transitionsEnabledChanged();
        void deferredFramesChanged();
        void transitionStateChanged();
        void textEditorChanged();
        void transformPreviewChanged();
        void frameReady();

    private:
        bool startTransition(int previous_slide);
        void finishTransition(bool restart_playback);
        void notifyFrameReady();
        void scheduleFrames();
        void clearRetainedFrame();
        QImage retainedFrame(const RenderPresentationPtr& document, const QSize& viewport) const;
        void retainPaintedFrame(QPainter* painter, const QSize& viewport);
        void commitCompletedFrame(int slide);
        void queueInteractionUpdate(const QRect& damaged);
        QRect prepareRetainedEditFrame(
            const RenderPresentationPtr& next, const RenderPresentationPtr& previous);
        RenderPresentationPtr document_;
        RenderPresentationPtr interaction_document_;
        RenderPresentationPtr fallback_document_;
        PresentationEditLayers edit_layers_;
        QImage retained_frame_;
        QVariantMap theme_;
        QVariantMap retained_frame_theme_;
        QVariantMap transform_preview_;
        QSize retained_frame_viewport_;
        std::uint64_t retained_frame_revision_ = 0;
        int retained_frame_slide_ = -1;
        int slide_index_ = 0;
        int selected_shape_ = -1;
        int editing_shape_ = -1;
        bool media_enabled_ = false;
        bool transitions_enabled_ = false;
        bool deferred_frames_ = false;
        int transition_from_slide_ = -1;
        double transition_progress_ = 1;
        PresentationMediaSession* media_ = nullptr;
        PresentationPlayback* playback_ = nullptr;
        PresentationRenderScheduler* render_scheduler_ = nullptr;
        QVariantAnimation* transition_animation_ = nullptr;
        QTimer interaction_update_timer_;
        QRect pending_interaction_damage_;
        bool pending_interaction_full_ = false;
        bool updating_document_ = false;
        std::atomic_bool frame_ready_pending_{true};
    };

    class SlideThumbnailRenderer : public QQuickPaintedItem
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(SlideThumbnail)
        Q_PROPERTY(QVariant document READ document WRITE setDocument NOTIFY documentChanged)
        Q_PROPERTY(QVariantMap theme READ theme WRITE setTheme NOTIFY themeChanged)
        Q_PROPERTY(int slideIndex READ slideIndex WRITE setSlideIndex NOTIFY slideIndexChanged)

    public:
        explicit SlideThumbnailRenderer(QQuickItem* parent = nullptr);
        QVariant document() const;
        void setDocument(const QVariant& document);
        QVariantMap theme() const;
        void setTheme(const QVariantMap& theme);
        int slideIndex() const;
        void setSlideIndex(int index);
        void paint(QPainter* painter) override;

    signals:
        void documentChanged();
        void themeChanged();
        void slideIndexChanged();

    private:
        void schedule();
        void startPending();
        QString cacheKey() const;
        RenderPresentationPtr document_;
        RenderPresentationPtr running_document_;
        QVariantMap theme_;
        QVariantMap running_theme_;
        QImage thumbnail_;
        QFutureWatcher<QImage> worker_;
        int slide_index_ = 0;
        int running_slide_ = -1;
        bool pending_ = false;
        bool start_queued_ = false;
    };
}
