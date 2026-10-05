#include "presentation_render_scheduler.hpp"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QPainter>
#include <QPromise>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <algorithm>

namespace
{
    Q_LOGGING_CATEGORY(pptxSchedulerLatency, "mirrorfly.pptx.latency", QtInfoMsg)

    constexpr int maximum_frame_width = 1600;
    constexpr int maximum_frame_height = 900;

    struct PresentationRenderPools
    {
        PresentationRenderPools()
        {
            // A spare frame worker lets the newest page overtake one obsolete decode; thumbnails stay
            // bounded.
            foreground.setMaxThreadCount(2);
            foreground.setExpiryTimeout(10000);
            foreground.setThreadPriority(QThread::LowPriority);
            background.setMaxThreadCount(2);
            background.setExpiryTimeout(10000);
            background.setThreadPriority(QThread::LowPriority);
        }

        ~PresentationRenderPools()
        {
            foreground.waitForDone();
            background.waitForDone();
        }

        QThreadPool foreground;
        QThreadPool background;
    };

    PresentationRenderPools& pools()
    {
        static PresentationRenderPools result;
        return result;
    }

    QSize bounded_frame_size(const QSize& viewport)
    {
        if (viewport.isEmpty())
        {
            return {};
        }
        if (viewport.width() <= maximum_frame_width && viewport.height() <= maximum_frame_height)
        {
            return viewport;
        }
        return viewport.scaled(QSize(maximum_frame_width, maximum_frame_height), Qt::KeepAspectRatio);
    }

    QVector<int> render_order(int current, int previous, int count)
    {
        QVector<int> result;
        const auto append = [&](int slide)
        {
            if (slide >= 0 && slide < count && !result.contains(slide))
            {
                result.append(slide);
            }
        };
        append(current);
        append(current + 1);
        append(previous);
        append(current - 1);
        return result;
    }
}

namespace mirrorfly
{
    PresentationRenderScheduler::PresentationRenderScheduler(QObject* parent)
        : QObject(parent), generation_(std::make_shared<std::atomic_uint64_t>(0))
    {
        const auto connect_worker = [this](QFutureWatcher<int>& worker, bool& busy)
        {
            auto* watched = &worker;
            auto* occupied = &busy;
            connect(watched, &QFutureWatcher<int>::resultReadyAt, this, [this, watched](int index)
            {
                if (watched->property("requestRevision").toULongLong() !=
                    generation_->load(std::memory_order_acquire))
                {
                    return;
                }
                emit frameReady(watched->future().resultAt(index));
            });
            connect(watched, &QFutureWatcher<int>::finished, this, [this, watched, occupied]()
            {
                const bool trace = pptxSchedulerLatency().isDebugEnabled();
                QElapsedTimer finish_timer;
                if (trace)
                {
                    finish_timer.start();
                    qCDebug(pptxSchedulerLatency) << "scheduler.finished.begin";
                }
                *occupied = false;
                if (trace)
                {
                    qCDebug(pptxSchedulerLatency) << "scheduler.finished.result.ms" << finish_timer.elapsed();
                }
                if (!refresh_timer_.isActive())
                {
                    startPending();
                }
                if (trace)
                {
                    qCDebug(pptxSchedulerLatency) << "scheduler.finished.end.ms" << finish_timer.elapsed();
                }
            });
        };
        connect_worker(primary_worker_, primary_busy_);
        connect_worker(secondary_worker_, secondary_busy_);
        refresh_timer_.setSingleShot(true);
        connect(&refresh_timer_, &QTimer::timeout, this, &PresentationRenderScheduler::startPending);
    }

    PresentationRenderScheduler::~PresentationRenderScheduler()
    {
        cancel();
    }

    void PresentationRenderScheduler::request(const RenderPresentationPtr& document, const QVariantMap& theme,
        const QSize& viewport, int current_slide, int previous_slide, bool preload_neighbors, int delay_ms)
    {
        const auto size = bounded_frame_size(viewport);
        const auto revision = generation_->load(std::memory_order_acquire);
        const bool latest_active =
            (primary_busy_ && primary_worker_.property("requestRevision").toULongLong() == revision) ||
            (secondary_busy_ && secondary_worker_.property("requestRevision").toULongLong() == revision);
        if ((pending_ || latest_active) && pending_document_ == document && pending_theme_ == theme &&
            pending_viewport_ == size && pending_current_slide_ == current_slide &&
            pending_previous_slide_ == previous_slide && pending_preload_neighbors_ == preload_neighbors &&
            pending_delay_ms_ == delay_ms)
        {
            return;
        }
        const bool trace = pptxSchedulerLatency().isDebugEnabled();
        QElapsedTimer request_timer;
        if (trace)
        {
            request_timer.start();
            qCDebug(pptxSchedulerLatency) << "scheduler.request.begin" << current_slide << delay_ms;
        }
        pending_document_ = document;
        if (trace)
        {
            qCDebug(pptxSchedulerLatency) << "scheduler.request.document.ms" << request_timer.elapsed();
        }
        pending_theme_ = theme;
        pending_viewport_ = size;
        pending_current_slide_ = current_slide;
        pending_previous_slide_ = previous_slide;
        pending_preload_neighbors_ = preload_neighbors;
        pending_delay_ms_ = delay_ms;
        pending_ = true;
        generation_->fetch_add(1, std::memory_order_acq_rel);
        refresh_timer_.stop();
        if (delay_ms > 0)
        {
            refresh_timer_.start(delay_ms);
        }
        else
        {
            startPending();
        }
        if (trace)
        {
            qCDebug(pptxSchedulerLatency) << "scheduler.request.end.ms" << request_timer.elapsed();
        }
    }

    void PresentationRenderScheduler::cancel()
    {
        generation_->fetch_add(1, std::memory_order_acq_rel);
        refresh_timer_.stop();
        pending_ = false;
        pending_document_.reset();
        pending_theme_.clear();
        pending_viewport_ = {};
        pending_current_slide_ = -1;
        pending_previous_slide_ = -1;
        pending_preload_neighbors_ = true;
        pending_delay_ms_ = 0;
    }

    QImage PresentationRenderScheduler::frame(const RenderPresentationPtr& document, const QVariantMap& theme,
        const QSize& viewport, int slide) const
    {
        return cached_presentation_frame(document, slide, theme, bounded_frame_size(viewport));
    }

    void PresentationRenderScheduler::startPending()
    {
        if (!pending_)
        {
            return;
        }
        auto* worker = !primary_busy_ ? &primary_worker_ : !secondary_busy_ ? &secondary_worker_ : nullptr;
        if (!worker)
        {
            return;
        }
        const bool trace = pptxSchedulerLatency().isDebugEnabled();
        QElapsedTimer start_timer;
        if (trace)
        {
            start_timer.start();
            qCDebug(pptxSchedulerLatency) << "scheduler.start.begin" << pending_current_slide_;
        }
        pending_ = false;
        const auto document = pending_document_;
        const auto theme = pending_theme_;
        const auto size = pending_viewport_;
        const int current = pending_current_slide_;
        const int previous = pending_previous_slide_;
        const bool preload_neighbors = pending_preload_neighbors_;
        if (!document || !document->scene || size.isEmpty() || current < 0 ||
            current >= static_cast<int>(document->scene->slides.size()))
        {
            return;
        }
        const auto generation = generation_;
        const auto expected = generation->load(std::memory_order_acquire);
        if (worker == &primary_worker_)
        {
            primary_busy_ = true;
        }
        else
        {
            secondary_busy_ = true;
        }
        worker->setProperty("requestRevision", QVariant::fromValue<qulonglong>(expected));
        worker->setFuture(QtConcurrent::run(&pools().foreground,
            [document, theme, size, current, previous, preload_neighbors, generation, expected](
                QPromise<int>& promise)
        {
            auto render_document = std::make_shared<RenderPresentation>(*document);
            render_document->render_request_token = generation;
            render_document->render_request_revision = expected;
            const auto order = preload_neighbors
                ? render_order(current, previous, static_cast<int>(document->scene->slides.size()))
                : QVector<int>{current};
            for (const int slide : order)
            {
                if (generation->load(std::memory_order_acquire) != expected)
                {
                    break;
                }
                if (!cached_presentation_frame(document, slide, theme, size).isNull())
                {
                    promise.addResult(slide);
                    continue;
                }
                QImage image(size, QImage::Format_ARGB32_Premultiplied);
                if (image.isNull())
                {
                    break;
                }
                image.fill(Qt::white);
                QPainter painter(&image);
                painter.setRenderHints(
                    QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
                paint_presentation_slide(
                    painter, render_document, static_cast<std::size_t>(slide), theme, image.rect());
                painter.end();
                if (generation->load(std::memory_order_acquire) != expected)
                {
                    break;
                }
                cache_presentation_frame(document, slide, theme, size, image);
                promise.addResult(slide);
            }
        }));
        if (trace)
        {
            qCDebug(pptxSchedulerLatency) << "scheduler.start.end.ms" << start_timer.elapsed();
        }
    }

    QThreadPool& presentation_thumbnail_pool()
    {
        return pools().background;
    }
}
