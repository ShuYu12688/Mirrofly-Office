#include "presentation_render_scheduler.hpp"
#include "slide_renderer.hpp"

#include <QPainter>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace
{
    constexpr int thumbnail_maximum_width = 240;
    constexpr int thumbnail_maximum_height = 135;
}

namespace mirrorfly
{
    SlideThumbnailRenderer::SlideThumbnailRenderer(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        setAntialiasing(false);
        setImplicitSize(160, 90);
        connect(&worker_, &QFutureWatcher<QImage>::finished, this, [this]()
        {
            QImage image;
            try
            {
                image = worker_.future().result();
            }
            catch (...)
            {
                image = {};
            }
            if (presentation_thumbnail_key(running_document_, running_slide_, running_theme_) == cacheKey() &&
                !image.isNull())
            {
                thumbnail_ = std::move(image);
                pending_ = false;
                update();
            }
            running_document_.reset();
            running_slide_ = -1;
            running_theme_.clear();
            startPending();
        });
    }

    QVariant SlideThumbnailRenderer::document() const
    {
        return document_ ? QVariant::fromValue(document_) : QVariant{};
    }

    void SlideThumbnailRenderer::setDocument(const QVariant& document)
    {
        const auto next = document.value<RenderPresentationPtr>();
        if (next == document_)
        {
            return;
        }
        const auto previous_key = cacheKey();
        document_ = next;
        if (document_ && document_->scene)
        {
            setImplicitSize(document_->scene->width * 4 / 3, document_->scene->height * 4 / 3);
        }
        else
        {
            setImplicitSize(160, 90);
        }
        if (previous_key != cacheKey())
        {
            schedule();
        }
        emit documentChanged();
    }

    QVariantMap SlideThumbnailRenderer::theme() const
    {
        return theme_;
    }

    void SlideThumbnailRenderer::setTheme(const QVariantMap& theme)
    {
        if (theme == theme_)
        {
            return;
        }
        theme_ = theme;
        schedule();
        emit themeChanged();
    }

    int SlideThumbnailRenderer::slideIndex() const
    {
        return slide_index_;
    }

    void SlideThumbnailRenderer::setSlideIndex(int index)
    {
        if (index == slide_index_)
        {
            return;
        }
        slide_index_ = index;
        schedule();
        emit slideIndexChanged();
    }

    QString SlideThumbnailRenderer::cacheKey() const
    {
        return presentation_thumbnail_key(document_, slide_index_, theme_);
    }

    void SlideThumbnailRenderer::schedule()
    {
        thumbnail_ = {};
        pending_ = false;
        if (!document_ || !document_->scene || slide_index_ < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()))
        {
            update();
            return;
        }
        thumbnail_ = cached_presentation_thumbnail(document_, cacheKey());
        if (!thumbnail_.isNull())
        {
            update();
            return;
        }
        pending_ = true;
        update();
        if (!start_queued_)
        {
            start_queued_ = true;
            QMetaObject::invokeMethod(this, [this]()
            {
                start_queued_ = false;
                startPending();
            }, Qt::QueuedConnection);
        }
    }

    void SlideThumbnailRenderer::startPending()
    {
        if (!pending_ || worker_.isRunning() || !document_ || !document_->scene || slide_index_ < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()))
        {
            return;
        }
        pending_ = false;
        running_document_ = document_;
        running_slide_ = slide_index_;
        running_theme_ = theme_;
        const auto document = running_document_;
        const auto render_document = presentation_thumbnail_document(document);
        const auto theme = running_theme_;
        const int slide = running_slide_;
        const QString key = cacheKey();
        worker_.setFuture(
            QtConcurrent::run(&presentation_thumbnail_pool(), [document, render_document, theme, slide, key]()
        {
            if (const auto cached = cached_presentation_thumbnail(document, key); !cached.isNull())
            {
                return cached;
            }
            const auto& scene = *render_document->scene;
            const double scale =
                std::min(thumbnail_maximum_width / scene.width, thumbnail_maximum_height / scene.height);
            const QSize size(
                std::max(1, qRound(scene.width * scale)), std::max(1, qRound(scene.height * scale)));
            QImage image(size, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.setRenderHints(
                QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
            paint_presentation_slide(
                painter, render_document, static_cast<std::size_t>(slide), theme, image.rect());
            painter.end();
            cache_presentation_thumbnail(document, key, image);
            return image;
        }));
    }

    void SlideThumbnailRenderer::paint(QPainter* painter)
    {
        painter->fillRect(QRectF(0, 0, width(), height()), Qt::white);
        if (!thumbnail_.isNull())
        {
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawImage(QRectF(0, 0, width(), height()), thumbnail_);
        }
    }
}
