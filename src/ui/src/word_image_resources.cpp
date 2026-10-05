#include "word_image_resources.hpp"

#include <mirrorfly/image_decode.hpp>

#include <QBuffer>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QImageReader>
#include <QPainter>
#include <QPointer>
#include <QThreadPool>
#include <QTransform>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace
{
    QImage decode(const mirrorfly::WordImage& source)
    {
        if (!source.bytes || source.bytes->size() > mirrorfly::maximum_word_part_bytes)
            return {};
        mirrorfly::DecodedRasterImage raster;
        if (source.mime_type == "image/x-emf" || source.mime_type == "image/x-wmf")
        {
            auto format = mirrorfly::MetafileFormat::Wmf;
            if (source.mime_type == "image/x-emf")
                format = mirrorfly::MetafileFormat::Emf;
            raster = mirrorfly::decode_metafile_image(*source.bytes, format);
        }
        else if (source.mime_type == "image/svg+xml")
            raster = mirrorfly::decode_svg_image(*source.bytes);
        if (!raster.rgba.empty())
            return QImage(raster.rgba.data(), raster.size.width, raster.size.height, raster.size.width * 4,
                QImage::Format_RGBA8888)
                .copy();
        if (source.mime_type == "image/x-emf" || source.mime_type == "image/x-wmf" ||
            source.mime_type == "image/svg+xml")
            return {};
        auto bytes =
            QByteArray::fromRawData(source.bytes->data(), static_cast<qsizetype>(source.bytes->size()));
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 32768 || size.height() > 32768 ||
            qint64(size.width()) * size.height() > 128 * 1024 * 1024)
            return {};
        const auto scaled = size.scaled(2048, 2048, Qt::KeepAspectRatio);
        if (scaled.width() < size.width() || scaled.height() < size.height())
            reader.setScaledSize(scaled);
        return reader.read();
    }
    QImage decode_image(const mirrorfly::WordImage& source)
    {
        auto image = decode(source);
        if (!image.isNull() && source.crop != std::array<double, 4>{})
        {
            const QRectF cropped(source.crop[0] * image.width(), source.crop[1] * image.height(),
                (1 - source.crop[0] - source.crop[2]) * image.width(),
                (1 - source.crop[1] - source.crop[3]) * image.height());
            if (cropped.width() > 0 && cropped.height() > 0)
            {
                auto size = cropped.size().toSize().expandedTo(QSize(1, 1));
                if (size.width() > 2048 || size.height() > 2048)
                    size.scale(2048, 2048, Qt::KeepAspectRatio);
                QImage result(size, QImage::Format_ARGB32_Premultiplied);
                result.fill(Qt::transparent);
                QPainter painter(&result);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(QRectF(QPointF{}, QSizeF(size)), image, cropped);
                painter.end();
                image = std::move(result);
            }
        }
        if (!image.isNull() && (source.rotation != 0 || source.flip_horizontal || source.flip_vertical))
        {
            QTransform transform;
            transform.rotate(source.rotation);
            transform.scale(source.flip_horizontal ? -1 : 1, source.flip_vertical ? -1 : 1);
            image = image.transformed(transform, Qt::SmoothTransformation);
        }
        return image;
    }

    QThreadPool& image_pool()
    {
        static QThreadPool pool;
        static const bool configured = []()
        {
            pool.setMaxThreadCount(2);
            pool.setThreadPriority(QThread::LowPriority);
            if (auto* application = QCoreApplication::instance())
                QObject::connect(application, &QCoreApplication::aboutToQuit, application, []()
                {
                    // Finish image plugins before QGuiApplication releases fonts and platform state.
                    pool.clear();
                    pool.waitForDone();
                });
            return true;
        }();
        static_cast<void>(configured);
        return pool;
    }

    QImage pending_image()
    {
        static const QImage image = []()
        {
            QImage result(1, 1, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::transparent);
            return result;
        }();
        return image;
    }
}

namespace mirrorfly
{
    WordImageResources::WordImageResources(
        const std::vector<WordImage>& images, QObject* parent, Decoder decoder)
        : QObject(parent), decoder_(decoder ? std::move(decoder) : decode_image)
    {
        using Key = std::tuple<std::shared_ptr<const std::string>, std::string, std::array<double, 4>, double,
            bool, bool>;
        std::map<Key, qulonglong> identities;
        for (const auto& image : images)
        {
            if (!image.id || aliases_.count(image.id))
                continue;
            const Key key{image.bytes, image.mime_type, image.crop, image.rotation, image.flip_horizontal,
                image.flip_vertical};
            const auto canonical = identities.emplace(key, image.id).first->second;
            aliases_.emplace(image.id, canonical);
            occurrences_[canonical].push_back(image.id);
            images_.emplace(canonical, image);
        }
    }

    WordImageResources::~WordImageResources()
    {
        // Workers own only immutable image bytes and this cancellation flag, never the document.
        cancelled_->store(true);
    }

    qulonglong WordImageResources::resourceId(const QUrl& url) const
    {
        if (url.scheme() != "mirrorfly-word-image" || !url.host().isEmpty() || url.hasQuery() ||
            url.hasFragment())
            return 0;
        bool valid = false;
        const auto id = url.path().mid(1).toULongLong(&valid);
        const auto found = aliases_.find(id);
        if (!valid || found == aliases_.end() || url.path() != QStringLiteral("/%1").arg(id))
            return 0;
        return found->second;
    }

    void WordImageResources::cacheImage(qulonglong id, const QImage& image)
    {
        // Cache failures too so unsupported images are not decoded on every repaint.
        const int cost = static_cast<int>(std::max<qsizetype>(1, (image.sizeInBytes() + 1023) / 1024));
        cache_.insert(id, new QImage(image), cost);
    }

    QImage WordImageResources::image(const QUrl& url)
    {
        const auto id = resourceId(url);
        if (!id)
            return {};
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (const auto* cached = cache_.object(id))
                return *cached;
        }
        const auto image = decoder_(images_.at(id));
        const std::lock_guard<std::mutex> lock(mutex_);
        cacheImage(id, image);
        return image;
    }

    QImage WordImageResources::request(const QUrl& url)
    {
        const auto id = resourceId(url);
        if (!id)
            return {};
        bool dispatch = false;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (prepared_complete_)
                return prepared_.at(id);
            if (const auto* cached = cache_.object(id))
                return *cached;
            if (!requested_.insert(id).second)
                return pending_image();
            // Favor the most recent viewport when scrolling faster than decoding.
            if (queue_.size() == 64)
            {
                requested_.erase(queue_.front());
                queue_.pop_front();
            }
            queue_.push_back(id);
            dispatch = !dispatch_pending_;
            dispatch_pending_ = true;
        }
        if (dispatch)
            QMetaObject::invokeMethod(this, &WordImageResources::startNext, Qt::QueuedConnection);
        return pending_image();
    }

    bool WordImageResources::prepare(const std::function<void(std::size_t, std::size_t)>& progress)
    {
        // This runs on the document owner before it is exposed to the render thread.
        if (QThread::currentThread() != thread() || active_ != 0)
            return false;
        if (prepared_complete_)
        {
            if (progress)
                progress(images_.size(), images_.size());
            return true;
        }
        constexpr qsizetype pixel_budget = 128 * 1024 * 1024;
        if (images_.size() > static_cast<std::size_t>(pixel_budget / 4))
            return false;
        const auto image_count = std::max<qsizetype>(1, static_cast<qsizetype>(images_.size()));
        const auto pixels_per_image = pixel_budget / (4 * image_count);
        std::map<qulonglong, QImage> prepared;
        if (progress)
            progress(0, images_.size());
        for (const auto& [id, source] : images_)
        {
            QImage image;
            try
            {
                image = decoder_(source);
            }
            catch (...)
            {
                // Keep a failed preview as a stable result, never a repeated decode on scrolling.
            }
            if (!image.isNull())
            {
                const double pixels = double(image.width()) * image.height();
                const auto scale = std::min(1.0, std::sqrt(pixels_per_image / pixels));
                const int target_width =
                    std::min<int>(pixels_per_image, std::max(1, static_cast<int>(image.width() * scale)));
                const QSize size(target_width,
                    std::min<int>(pixels_per_image / target_width,
                        std::max(1, static_cast<int>(image.height() * scale))));
                if (size != image.size())
                    image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }
            prepared.emplace(id, std::move(image));
            if (progress)
                progress(prepared.size(), images_.size());
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        prepared_ = std::move(prepared);
        prepared_complete_ = true;
        cache_.clear();
        queue_.clear();
        requested_.clear();
        dispatch_pending_ = false;
        return true;
    }

    qsizetype WordImageResources::preparedBytes() const
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        qsizetype bytes = 0;
        for (const auto& [id, image] : prepared_)
            bytes += image.sizeInBytes();
        return bytes;
    }

    bool WordImageResources::pending() const
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        return !requested_.empty();
    }

    void WordImageResources::startNext()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            dispatch_pending_ = false;
        }
        while (active_ < 2)
        {
            qulonglong id;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty())
                    return;
                id = queue_.front();
                queue_.pop_front();
            }
            ++active_;
            auto* watcher = new QFutureWatcher<QImage>(this);
            connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, id]()
            {
                const auto image = watcher->result();
                watcher->deleteLater();
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    cacheImage(id, image);
                    requested_.erase(id);
                }
                --active_;
                const auto occurrences = occurrences_.at(id);
                const QPointer<WordImageResources> alive(this);
                for (const auto occurrence : occurrences)
                {
                    emit imageReady(QUrl(QStringLiteral("mirrorfly-word-image:/%1").arg(occurrence)));
                    if (!alive)
                        return;
                }
                startNext();
            });
            watcher->setFuture(QtConcurrent::run(&image_pool(),
                [source = images_.at(id), decoder = decoder_, cancelled = cancelled_]()
            {
                if (cancelled->load())
                    return QImage{};
                try
                {
                    return decoder(source);
                }
                catch (...)
                {
                    return QImage{};
                }
            }));
        }
    }
}
