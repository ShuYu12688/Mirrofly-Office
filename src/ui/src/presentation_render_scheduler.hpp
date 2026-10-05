#pragma once

#include "presentation_scene.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QSize>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <memory>

class QThreadPool;

namespace mirrorfly
{
    class PresentationRenderScheduler : public QObject
    {
        Q_OBJECT

    public:
        explicit PresentationRenderScheduler(QObject* parent = nullptr);
        ~PresentationRenderScheduler() override;

        void request(const RenderPresentationPtr& document, const QVariantMap& theme, const QSize& viewport,
            int current_slide, int previous_slide = -1, bool preload_neighbors = true, int delay_ms = 0);
        void cancel();
        QImage frame(const RenderPresentationPtr& document, const QVariantMap& theme, const QSize& viewport,
            int slide) const;

    signals:
        void frameReady(int slide);

    private:
        void startPending();

        RenderPresentationPtr pending_document_;
        QVariantMap pending_theme_;
        QSize pending_viewport_;
        int pending_current_slide_ = -1;
        int pending_previous_slide_ = -1;
        bool pending_preload_neighbors_ = true;
        int pending_delay_ms_ = 0;
        bool pending_ = false;
        QFutureWatcher<int> primary_worker_;
        QFutureWatcher<int> secondary_worker_;
        bool primary_busy_ = false;
        bool secondary_busy_ = false;
        QTimer refresh_timer_;
        std::shared_ptr<std::atomic_uint64_t> generation_;
    };

    QThreadPool& presentation_thumbnail_pool();
}
