#include "presentation_scene.hpp"

#include <mirrorfly/embedded_font.hpp>
#include <mirrorfly/image_decode.hpp>

#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImageIOHandler>
#include <QImageReader>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

namespace
{
    Q_LOGGING_CATEGORY(pptxSceneLatency, "mirrorfly.pptx.latency", QtInfoMsg)

    constexpr qint64 maximum_image_pixels = 16 * 1024 * 1024;
    constexpr qint64 maximum_lazy_image_pixels = 256 * 1024 * 1024;
    constexpr int maximum_source_image_side = 32768;
    constexpr int maximum_image_side = 2048;
    constexpr int image_cache_kib = 64 * 1024;
    constexpr int thumbnail_image_cache_kib = 8 * 1024;
    constexpr int thumbnail_cache_kib = 8 * 1024;
    // Four maximum-size preloaded frames occupy about 22 MiB; retain the visible page too.
    constexpr int frame_cache_kib = 32 * 1024;

    QString utf8(const std::string& value)
    {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }

    QString frame_key(std::uint64_t revision, int slide, const QVariantMap& theme, const QSize& size)
    {
        const auto theme_bytes = QJsonDocument::fromVariant(theme).toJson(QJsonDocument::Compact);
        const auto theme_hash =
            QCryptographicHash::hash(theme_bytes, QCryptographicHash::Sha256).toHex().left(16);
        return QStringLiteral("%1:%2:%3x%4:%5")
            .arg(revision)
            .arg(slide)
            .arg(size.width())
            .arg(size.height())
            .arg(QString::fromLatin1(theme_hash));
    }

    bool readable_image(QImageReader& reader)
    {
        const auto format = reader.format().toLower();
        static const QSet<QByteArray> formats{"png", "jpeg", "jpg", "bmp", "gif", "webp"};
        if (!formats.contains(format))
        {
            return false;
        }
        const QSize size = reader.size();
        if (!size.isValid() || size.width() > maximum_source_image_side ||
            size.height() > maximum_source_image_side)
        {
            return false;
        }
        const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
        return pixels <= maximum_image_pixels ||
            (pixels <= maximum_lazy_image_pixels && reader.supportsOption(QImageIOHandler::ScaledSize));
    }

    bool is_metafile(const std::string& mime_type)
    {
        return mime_type == "image/x-emf" || mime_type == "image/x-wmf";
    }

    bool is_wdp(const std::string& mime_type)
    {
        return mime_type == "image/vnd.ms-photo" || mime_type == "image/jxr";
    }

    mirrorfly::MetafileFormat metafile_format(const std::string& mime_type)
    {
        return mime_type == "image/x-emf" ? mirrorfly::MetafileFormat::Emf : mirrorfly::MetafileFormat::Wmf;
    }

    void report_progress(const std::function<void(std::size_t, std::size_t)>& progress, std::size_t completed,
        std::size_t total) noexcept
    {
        if (!progress)
        {
            return;
        }
        try
        {
            progress(completed, total);
        }
        catch (...)
        {
            // Rendering preparation remains valid when diagnostics are unavailable.
        }
    }

    QString first_font(
        const QHash<QString, QString>& installed, const QStringList& candidates, const QString& fallback)
    {
        for (const auto& candidate : candidates)
        {
            const auto found = installed.constFind(candidate.toCaseFolded());
            if (found != installed.cend())
            {
                return found.value();
            }
        }
        return fallback;
    }
}

namespace mirrorfly
{
    bool presentation_east_asian_character(char32_t character)
    {
        const auto script = QChar::script(character);
        return script == QChar::Script_Han || script == QChar::Script_Hangul ||
            script == QChar::Script_Hiragana || script == QChar::Script_Katakana ||
            script == QChar::Script_Bopomofo || (character >= 0x3000 && character <= 0x303F) ||
            (character >= 0xFF00 && character <= 0xFFEF);
    }

    class PresentationImageCache
    {
    public:
        struct InFlight
        {
            QWaitCondition ready;
            QImage image;
            bool done = false;
            bool rejected = false;
        };

        explicit PresentationImageCache(int maximum_cost = image_cache_kib) : images(maximum_cost)
        {
        }

        QMutex mutex;
        QCache<QString, QImage> images;
        QHash<QString, QSize> source_sizes;
        QSet<QString> rejected;
        // Keep path tombstones while workers may still finish for an older document revision.
        QSet<QString> known_paths;
        QHash<QString, std::shared_ptr<InFlight>> in_flight;
    };

    class PresentationThumbnailCache
    {
    public:
        QMutex mutex;
        QCache<QString, QImage> images{thumbnail_cache_kib};
    };

    class PresentationFrameCache
    {
    public:
        QMutex mutex;
        QCache<QString, QImage> images{frame_cache_kib};
    };

    class PresentationFontLoader
    {
    public:
        void ensure(const RenderPresentation& document, std::size_t slide)
        {
            if (!document.scene || slide >= document.scene->slides.size() || document.renderCancelled())
            {
                return;
            }
            const auto& scene = *document.scene;
            QSet<QString> required;
            for (const auto& shape : scene.slides[slide].shapes)
            {
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    const auto bullet = utf8(paragraph.bullet_font).trimmed().toCaseFolded();
                    if (!bullet.isEmpty())
                    {
                        required.insert(bullet + QStringLiteral("\x1fregular"));
                    }
                    for (const auto& run : paragraph.runs)
                    {
                        QString style = QStringLiteral("regular");
                        if (run.bold && run.italic)
                        {
                            style = QStringLiteral("bolditalic");
                        }
                        else if (run.bold)
                        {
                            style = QStringLiteral("bold");
                        }
                        else if (run.italic)
                        {
                            style = QStringLiteral("italic");
                        }
                        for (const auto& family : {run.font_family, run.east_asian_font_family})
                        {
                            const auto key = utf8(family).trimmed().toCaseFolded();
                            if (!key.isEmpty())
                            {
                                required.insert(key + QChar(0x1F) + style);
                            }
                        }
                    }
                }
            }

            const bool trace = pptxSceneLatency().isDebugEnabled();
            QElapsedTimer ensure_timer;
            if (trace)
            {
                ensure_timer.start();
                qCDebug(pptxSceneLatency) << "font.ensure.lock.begin" << slide;
            }
            QMutexLocker lock(&mutex_);
            if (trace)
            {
                qCDebug(pptxSceneLatency) << "font.ensure.locked.ms" << ensure_timer.elapsed();
            }
            for (const auto& font : scene.embedded_fonts)
            {
                if (document.renderCancelled())
                {
                    break;
                }
                const auto family = utf8(font.family).trimmed().toCaseFolded();
                const auto style = utf8(font.style).trimmed().toCaseFolded();
                const auto resource = utf8(font.path);
                if (!font.bytes || family.isEmpty() || resource.isEmpty() || attempted_.contains(resource) ||
                    !required.contains(family + QChar(0x1F) + style))
                {
                    continue;
                }
                attempted_.insert(resource);
                if (trace)
                {
                    qCDebug(pptxSceneLatency) << "font.load.begin" << resource << font.bytes->size();
                }
                const auto loaded = session_.load(*font.bytes);
                if (trace)
                {
                    qCDebug(pptxSceneLatency) << "font.load.end.ms" << resource << ensure_timer.elapsed();
                }
                if (loaded.loaded)
                {
                    QMutexLocker state_lock(&state_mutex_);
                    loaded_.insert(family);
                }
            }
        }

        bool loaded(const QString& family) const
        {
            // Font registration never holds this lock, so editor reads cannot wait for the OS loader.
            QMutexLocker lock(&state_mutex_);
            return loaded_.contains(family.toCaseFolded());
        }

    private:
        QMutex mutex_;
        mutable QMutex state_mutex_;
        EmbeddedFontSession session_;
        QSet<QString> attempted_;
        QSet<QString> loaded_;
    };

    PresentationRenderEnvironmentPtr presentation_render_environment()
    {
        static const auto environment = []()
        {
            auto result = std::make_shared<PresentationRenderEnvironment>();
            const auto families = QFontDatabase::families();
            result->installed_fonts.reserve(families.size());
            for (const auto& family : families)
            {
                result->installed_fonts.insert(family.toCaseFolded(), family);
            }
            result->latin_fallback = first_font(result->installed_fonts,
                {"Aptos", "Calibri", "Arial", "Segoe UI", "Noto Sans", "DejaVu Sans"},
                QGuiApplication::font().family());
            result->cjk_fonts = QFontDatabase::families(QFontDatabase::SimplifiedChinese);
            result->cjk_fallback = first_font(result->installed_fonts,
                {"Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans CJK SC", "PingFang SC",
                    "WenQuanYi Zen Hei"},
                result->cjk_fonts.isEmpty() ? result->latin_fallback : result->cjk_fonts.constFirst());
            return std::shared_ptr<const PresentationRenderEnvironment>(std::move(result));
        }();
        return environment;
    }

    RenderPresentationPtr prepare_presentation(std::shared_ptr<const PresentationScene> scene,
        const RenderPresentationPtr& previous, bool reuse_analysis)
    {
        PresentationPrepareOptions options;
        options.reuse_analysis = reuse_analysis;
        return prepare_presentation(std::move(scene), previous, options);
    }

    RenderPresentationPtr prepare_presentation(std::shared_ptr<const PresentationScene> scene,
        const RenderPresentationPtr& previous, const PresentationPrepareOptions& options)
    {
        auto result = std::make_shared<RenderPresentation>();
        result->scene = std::move(scene);
        if (previous && options.edited_slide >= 0 && options.edited_shape >= 0 &&
            static_cast<std::size_t>(options.edited_slide) < previous->thumbnail_revisions.size())
        {
            result->edit_layer_source_revision = previous->thumbnail_revisions[options.edited_slide];
            result->edited_slide = options.edited_slide;
            result->edited_shape = options.edited_shape;
            result->edit_layer_change = options.edit_layer_change;
        }
        result->environment = options.environment ? options.environment : presentation_render_environment();
        const auto same_image = [](const PresentationImage& current, const PresentationImage& prior)
        {
            return current.path == prior.path && current.mime_type == prior.mime_type &&
                current.bytes == prior.bytes;
        };
        const auto cached_path =
            [](const std::shared_ptr<PresentationImageCache>& cache, const std::string& path)
        {
            if (!cache)
            {
                return false;
            }
            const auto key = utf8(path);
            QMutexLocker lock(&cache->mutex);
            return cache->known_paths.contains(key) || cache->images.contains(key) ||
                cache->source_sizes.contains(key) || cache->rejected.contains(key) ||
                cache->in_flight.contains(key);
        };
        bool compatible_images = previous && previous->scene;
        std::size_t shared_images = 0;
        if (compatible_images)
        {
            std::unordered_map<std::string, const PresentationImage*> prior_images;
            prior_images.reserve(previous->scene->images.size());
            for (const auto& prior : previous->scene->images)
            {
                if (!prior_images.emplace(prior.path, &prior).second)
                {
                    compatible_images = false;
                    break;
                }
            }
            std::unordered_set<std::string> current_paths;
            current_paths.reserve(result->scene->images.size());
            for (const auto& current : result->scene->images)
            {
                if (!compatible_images || !current_paths.insert(current.path).second)
                {
                    compatible_images = false;
                    break;
                }
                const auto prior = prior_images.find(current.path);
                if (prior == prior_images.end())
                {
                    if (cached_path(previous->image_cache, current.path) ||
                        cached_path(previous->thumbnail_image_cache, current.path) ||
                        cached_path(previous->full_image_cache, current.path))
                    {
                        compatible_images = false;
                        break;
                    }
                    continue;
                }
                if (!same_image(current, *prior->second))
                {
                    compatible_images = false;
                    break;
                }
                ++shared_images;
            }
            compatible_images = compatible_images && shared_images > 0;
        }
        const bool same_images = compatible_images &&
            previous->scene->images.size() == result->scene->images.size() &&
            std::equal(result->scene->images.begin(), result->scene->images.end(),
                previous->scene->images.begin(), previous->scene->images.end(), same_image);
        result->image_cache =
            compatible_images ? previous->image_cache : std::make_shared<PresentationImageCache>();
        result->thumbnail_image_cache = compatible_images
            ? previous->thumbnail_image_cache
            : std::make_shared<PresentationImageCache>(thumbnail_image_cache_kib);
        for (const auto& cache : {result->image_cache, result->thumbnail_image_cache})
        {
            QMutexLocker lock(&cache->mutex);
            for (const auto& asset : result->scene->images)
            {
                cache->known_paths.insert(utf8(asset.path));
            }
        }
        result->frame_cache = previous ? previous->frame_cache : std::make_shared<PresentationFrameCache>();
        result->thumbnail_cache =
            previous ? previous->thumbnail_cache : std::make_shared<PresentationThumbnailCache>();
        result->thumbnail_revisions = options.thumbnail_revisions;
        if (result->thumbnail_revisions.empty() && options.reuse_thumbnails && previous &&
            previous->scene->width == result->scene->width &&
            previous->scene->height == result->scene->height)
        {
            result->thumbnail_revisions = previous->thumbnail_revisions;
        }
        if (result->thumbnail_revisions.size() != result->scene->slides.size())
        {
            result->thumbnail_revisions.assign(result->scene->slides.size(), 0);
        }
        static std::atomic<std::uint64_t> next_thumbnail_revision{1};
        for (auto& revision : result->thumbnail_revisions)
        {
            if (revision == 0)
            {
                revision = next_thumbnail_revision.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const auto same_font =
            [](const PresentationEmbeddedFont& current, const PresentationEmbeddedFont& prior)
        {
            return current.family == prior.family && current.style == prior.style &&
                current.path == prior.path && current.bytes == prior.bytes;
        };
        const bool same_font_resources = previous && previous->scene &&
            std::equal(result->scene->embedded_fonts.begin(), result->scene->embedded_fonts.end(),
                previous->scene->embedded_fonts.begin(), previous->scene->embedded_fonts.end(), same_font);
        // Editing shapes invalidates font analysis, but unchanged font resources retain their OS session.
        result->font_loader = same_font_resources   ? previous->font_loader
            : result->scene->embedded_fonts.empty() ? nullptr
                                                    : std::make_shared<PresentationFontLoader>();
        if (options.reuse_analysis && previous && same_images && same_font_resources)
        {
            result->fonts = previous->fonts;
            result->embedded_fonts = previous->embedded_fonts;
            result->substituted_fonts = previous->substituted_fonts;
            result->latin_fallback = previous->latin_fallback;
            result->cjk_fallback = previous->cjk_fallback;
            result->font_summary = previous->font_summary;
            result->image_index = previous->image_index;
            result->image_sizes = previous->image_sizes;
            report_progress(options.progress, 1, 1);
            return result;
        }

        const auto& installed = result->environment->installed_fonts;
        result->latin_fallback = result->environment->latin_fallback;
        result->cjk_fallback = result->environment->cjk_fallback;

        QSet<QString> used_font_families;
        for (const auto& slide : result->scene->slides)
        {
            for (const auto& shape : slide.shapes)
            {
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    const auto bullet = utf8(paragraph.bullet_font).trimmed().toCaseFolded();
                    if (!bullet.isEmpty())
                    {
                        used_font_families.insert(bullet);
                    }
                    for (const auto& run : paragraph.runs)
                    {
                        for (const auto& family : {run.font_family, run.east_asian_font_family})
                        {
                            const auto key = utf8(family).trimmed().toCaseFolded();
                            if (!key.isEmpty())
                            {
                                used_font_families.insert(key);
                            }
                        }
                    }
                }
            }
        }
        if (!result->scene->embedded_fonts.empty())
        {
            for (const auto& font : result->scene->embedded_fonts)
            {
                const auto key = utf8(font.family).trimmed().toCaseFolded();
                if (!font.bytes || key.isEmpty() || !used_font_families.contains(key))
                {
                    continue;
                }
                result->embedded_fonts.insert(key);
            }
        }

        QStringList substitutions;
        QSet<QString> seen;
        result->fonts.reserve(64);
        const auto register_font = [&](const std::string& family, bool east_asian)
        {
            const QString requested = utf8(family).trimmed();
            if (requested.isEmpty())
            {
                return;
            }
            const QString key = requested.toCaseFolded();
            if (result->fonts.contains(key))
            {
                return;
            }
            const auto found = installed.constFind(key);
            const QString resolved = result->embedded_fonts.contains(key) ? requested
                : found == installed.cend() ? (east_asian ? result->cjk_fallback : result->latin_fallback)
                                            : found.value();
            result->fonts.insert(key, resolved);
            if (!result->embedded_fonts.contains(key) && found == installed.cend() && !seen.contains(key))
            {
                seen.insert(key);
                result->substituted_fonts.insert(key);
                substitutions.append(requested + QStringLiteral(" → ") + resolved);
            }
        };

        QStringList warnings;
        QSet<QString> warning_set;
        const auto register_warning = [&](const std::string& warning)
        {
            const auto text = utf8(warning);
            if (!warning_set.contains(text))
            {
                warning_set.insert(text);
                warnings.append(text);
            }
        };
        for (const auto& warning : result->scene->warnings)
        {
            register_warning(warning);
        }
        const std::size_t progress_total =
            result->scene->slides.size() + (options.eager_image_analysis ? result->scene->images.size() : 0);
        std::size_t progress_completed = 0;
        report_progress(options.progress, 0, progress_total);
        for (const auto& slide : result->scene->slides)
        {
            for (const auto& warning : slide.warnings)
            {
                register_warning(warning);
            }
            for (const auto& shape : slide.shapes)
            {
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    register_font(paragraph.bullet_font, false);
                    for (const auto& run : paragraph.runs)
                    {
                        register_font(run.font_family, false);
                        register_font(run.east_asian_font_family, true);
                    }
                }
            }
            report_progress(options.progress, ++progress_completed, progress_total);
        }

        int unavailable_images = 0;
        result->image_index.reserve(static_cast<qsizetype>(result->scene->images.size()));
        if (options.eager_image_analysis)
        {
            result->image_sizes.reserve(static_cast<qsizetype>(result->scene->images.size()));
        }
        for (std::size_t index = 0; index < result->scene->images.size(); ++index)
        {
            const auto& asset = result->scene->images[index];
            const QString key = utf8(asset.path);
            result->image_index.insert(key, index);
            if (!asset.bytes)
            {
                QMutexLocker lock(&result->image_cache->mutex);
                result->image_cache->rejected.insert(key);
                ++unavailable_images;
                if (options.eager_image_analysis)
                {
                    report_progress(options.progress, ++progress_completed, progress_total);
                }
                continue;
            }
            if (!options.eager_image_analysis)
            {
                continue;
            }
            if (compatible_images)
            {
                bool rejected = false;
                QSize known_size;
                {
                    QMutexLocker lock(&result->image_cache->mutex);
                    rejected = result->image_cache->rejected.contains(key);
                    const auto known = result->image_cache->source_sizes.constFind(key);
                    if (known != result->image_cache->source_sizes.cend())
                    {
                        known_size = known.value();
                    }
                }
                if (rejected)
                {
                    ++unavailable_images;
                }
                else if (known_size.isValid())
                {
                    result->image_sizes.insert(key, known_size);
                }
                if (rejected || known_size.isValid())
                {
                    report_progress(options.progress, ++progress_completed, progress_total);
                    continue;
                }
            }
            if (asset.mime_type == "image/tiff" || asset.mime_type == "image/svg+xml" ||
                is_wdp(asset.mime_type) || is_metafile(asset.mime_type))
            {
                ImageDimensions size;
                if (asset.mime_type == "image/tiff")
                    size = inspect_tiff_image(*asset.bytes);
                else if (asset.mime_type == "image/svg+xml")
                    size = inspect_svg_image(*asset.bytes);
                else if (is_wdp(asset.mime_type))
                    size = inspect_wdp_image(*asset.bytes);
                else
                    size = inspect_metafile_image(*asset.bytes, metafile_format(asset.mime_type));
                if (size.width > 0 && size.height > 0)
                {
                    result->image_sizes.insert(key, QSize(size.width, size.height));
                    QMutexLocker lock(&result->image_cache->mutex);
                    result->image_cache->source_sizes.insert(key, QSize(size.width, size.height));
                }
                else
                {
                    QMutexLocker lock(&result->image_cache->mutex);
                    result->image_cache->rejected.insert(key);
                    ++unavailable_images;
                }
                report_progress(options.progress, ++progress_completed, progress_total);
                continue;
            }
            const auto bytes =
                QByteArray::fromRawData(asset.bytes->data(), static_cast<qsizetype>(asset.bytes->size()));
            QBuffer buffer;
            buffer.setData(bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            if (!readable_image(reader))
            {
                QMutexLocker lock(&result->image_cache->mutex);
                result->image_cache->rejected.insert(key);
                ++unavailable_images;
            }
            else
            {
                result->image_sizes.insert(key, reader.size());
                QMutexLocker lock(&result->image_cache->mutex);
                result->image_cache->source_sizes.insert(key, reader.size());
            }
            report_progress(options.progress, ++progress_completed, progress_total);
        }

        QStringList summary;
        if (!result->embedded_fonts.isEmpty())
        {
            summary.append(QStringLiteral("%1 种嵌入字体将按当前页加载").arg(result->embedded_fonts.size()));
        }
        if (substitutions.isEmpty())
        {
            summary.append(QStringLiteral("使用系统字体；中文回退：") + result->cjk_fallback);
        }
        else
        {
            summary.append(QStringLiteral("字体替代：") + substitutions.join(QStringLiteral("；")));
            summary.append(QStringLiteral("中文回退：") + result->cjk_fallback);
        }
        if (result->environment->cjk_fonts.isEmpty())
        {
            summary.append(QStringLiteral("本机未找到简体中文字体，部分文字可能无法显示"));
        }
        if (unavailable_images > 0)
        {
            summary.append(QStringLiteral("%1 张图片格式不支持、内容损坏或超过安全上限，将显示占位提示")
                    .arg(unavailable_images));
        }
        if (!warnings.isEmpty())
        {
            summary.append(warnings.join(QStringLiteral("；")));
        }
        result->font_summary = summary.join(QStringLiteral(" · "));
        return result;
    }

    QImage presentation_image(const RenderPresentationPtr& document, const std::string& path)
    {
        if (!document || !document->scene || document->renderCancelled())
        {
            return {};
        }
        const QString key = utf8(path);
        const auto found = document->image_index.constFind(key);
        if (found == document->image_index.cend())
        {
            return {};
        }

        const auto& asset = document->scene->images[found.value()];
        if (!asset.bytes)
        {
            QMutexLocker lock(&document->image_cache->mutex);
            document->image_cache->rejected.insert(key);
            return {};
        }
        auto& cache = *document->image_cache;
        std::shared_ptr<PresentationImageCache::InFlight> flight;
        for (;;)
        {
            QMutexLocker lock(&cache.mutex);
            if (cache.rejected.contains(key))
            {
                return {};
            }
            if (const auto* cached = cache.images.object(key))
            {
                return *cached;
            }
            if (!document->allow_image_decode)
            {
                break;
            }
            const auto running = cache.in_flight.constFind(key);
            if (running == cache.in_flight.cend())
            {
                flight = std::make_shared<PresentationImageCache::InFlight>();
                cache.in_flight.insert(key, flight);
                break;
            }
            const auto pending = running.value();
            while (!pending->done && !document->renderCancelled())
            {
                pending->ready.wait(&cache.mutex, 20);
            }
            if (document->renderCancelled())
            {
                return {};
            }
            if (!pending->image.isNull() || pending->rejected)
            {
                return pending->image;
            }
        }
        if (!document->allow_image_decode)
        {
            if (document->thumbnail_image_cache && document->thumbnail_image_cache != document->image_cache)
            {
                QMutexLocker lock(&document->thumbnail_image_cache->mutex);
                if (const auto* cached = document->thumbnail_image_cache->images.object(key))
                {
                    return *cached;
                }
            }
            return {};
        }
        const auto finish = [&](QImage image, const QSize& size, bool rejected)
        {
            QMutexLocker lock(&cache.mutex);
            if (document->renderCancelled())
            {
                image = {};
                rejected = false;
            }
            if (rejected)
            {
                cache.rejected.insert(key);
            }
            if (!image.isNull())
            {
                cache.source_sizes.insert(key, size);
                const int cost =
                    static_cast<int>(std::max<qsizetype>(1, (image.sizeInBytes() + 1023) / 1024));
                cache.images.insert(key, new QImage(image), cost);
            }
            flight->image = image;
            flight->rejected = rejected;
            flight->done = true;
            cache.in_flight.remove(key);
            flight->ready.wakeAll();
            return image;
        };
        if (document->full_image_cache && document->full_image_cache != document->image_cache)
        {
            QImage full_image;
            QSize full_source_size;
            {
                QMutexLocker lock(&document->full_image_cache->mutex);
                if (const auto* cached = document->full_image_cache->images.object(key))
                {
                    full_image = *cached;
                    full_source_size = document->full_image_cache->source_sizes.value(key);
                }
            }
            if (!full_image.isNull())
            {
                const int side = std::clamp(document->image_decode_side, 256, maximum_image_side);
                if (full_image.width() > side || full_image.height() > side)
                {
                    full_image = full_image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
                return finish(std::move(full_image), full_source_size, false);
            }
        }
        QImage image;
        QSize source_size;
        try
        {
            if (asset.mime_type == "image/tiff" || asset.mime_type == "image/svg+xml" ||
                is_wdp(asset.mime_type) || is_metafile(asset.mime_type))
            {
                DecodedRasterImage decoded;
                if (asset.mime_type == "image/tiff")
                    decoded = decode_tiff_image(*asset.bytes);
                else if (asset.mime_type == "image/svg+xml")
                    decoded = decode_svg_image(*asset.bytes);
                else if (is_wdp(asset.mime_type))
                    decoded = decode_wdp_image(*asset.bytes, document->image_decode_side);
                else
                    decoded = decode_metafile_image(*asset.bytes, metafile_format(asset.mime_type));
                if (decoded.rgba.empty())
                {
                    return finish({}, {}, true);
                }
                source_size = QSize(decoded.source_size.width, decoded.source_size.height);
                const QImage view(decoded.rgba.data(), decoded.size.width, decoded.size.height,
                    decoded.size.width * 4, QImage::Format_RGBA8888);
                image = view.copy();
                if (image.isNull())
                {
                    return finish({}, {}, true);
                }
            }
            else
            {
                const auto bytes =
                    QByteArray::fromRawData(asset.bytes->data(), static_cast<qsizetype>(asset.bytes->size()));
                QBuffer buffer;
                buffer.setData(bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                if (!readable_image(reader))
                {
                    return finish({}, {}, true);
                }
                reader.setAutoTransform(true);
                source_size = reader.size();
                const int decode_side = std::clamp(document->image_decode_side, 256, maximum_image_side);
                if (source_size.width() > decode_side || source_size.height() > decode_side)
                {
                    reader.setScaledSize(source_size.scaled(decode_side, decode_side, Qt::KeepAspectRatio));
                }
                image = reader.read();
                if (image.isNull())
                {
                    return finish({}, {}, true);
                }
            }
        }
        catch (...)
        {
            return finish({}, {}, false);
        }
        return finish(std::move(image), source_size, false);
    }

    RenderPresentationPtr presentation_thumbnail_document(const RenderPresentationPtr& document)
    {
        if (!document)
        {
            return {};
        }
        auto result = std::make_shared<RenderPresentation>(*document);
        result->full_image_cache =
            document->full_image_cache ? document->full_image_cache : document->image_cache;
        result->image_cache = document->thumbnail_image_cache;
        result->image_decode_side = 256;
        result->load_embedded_fonts = false;
        return result;
    }

    RenderPresentationPtr presentation_interaction_document(const RenderPresentationPtr& document)
    {
        if (!document)
        {
            return {};
        }
        auto result = std::make_shared<RenderPresentation>(*document);
        result->allow_image_decode = false;
        result->load_embedded_fonts = false;
        return result;
    }

    void ensure_presentation_fonts(const RenderPresentationPtr& document, std::size_t slide)
    {
        if (document && document->load_embedded_fonts && document->font_loader)
        {
            document->font_loader->ensure(*document, slide);
        }
    }

    bool presentation_embedded_font_loaded(const RenderPresentationPtr& document, const QString& family)
    {
        return document && document->font_loader && document->font_loader->loaded(family);
    }

    QSize presentation_image_source_size(
        const RenderPresentationPtr& document, const std::string& path, const QSize& fallback)
    {
        if (!document)
        {
            return fallback;
        }
        const QString key = utf8(path);
        const auto eager = document->image_sizes.constFind(key);
        if (eager != document->image_sizes.cend())
        {
            return eager.value();
        }
        QMutexLocker lock(&document->image_cache->mutex);
        const auto found = document->image_cache->source_sizes.constFind(key);
        return found == document->image_cache->source_sizes.cend() ? fallback : found.value();
    }

    QString presentation_thumbnail_key(
        const RenderPresentationPtr& document, int slide, const QVariantMap& theme)
    {
        if (!document || slide < 0 || static_cast<std::size_t>(slide) >= document->thumbnail_revisions.size())
        {
            return {};
        }
        const auto theme_bytes = QJsonDocument::fromVariant(theme).toJson(QJsonDocument::Compact);
        const auto theme_hash = QCryptographicHash::hash(theme_bytes, QCryptographicHash::Sha256).toHex();
        return QStringLiteral("%1:%2")
            .arg(document->thumbnail_revisions[static_cast<std::size_t>(slide)])
            .arg(QString::fromLatin1(theme_hash));
    }

    QImage cached_presentation_thumbnail(const RenderPresentationPtr& document, const QString& key)
    {
        if (!document || !document->thumbnail_cache)
        {
            return {};
        }
        QMutexLocker lock(&document->thumbnail_cache->mutex);
        const auto* cached = document->thumbnail_cache->images.object(key);
        return cached ? *cached : QImage{};
    }

    void cache_presentation_thumbnail(
        const RenderPresentationPtr& document, const QString& key, const QImage& image)
    {
        if (!document || !document->thumbnail_cache || image.isNull())
        {
            return;
        }
        const int cost = static_cast<int>(std::max<qsizetype>(1, (image.sizeInBytes() + 1023) / 1024));
        QMutexLocker lock(&document->thumbnail_cache->mutex);
        if (!document->thumbnail_cache->images.object(key))
        {
            document->thumbnail_cache->images.insert(key, new QImage(image), cost);
        }
    }

    QImage cached_presentation_frame(
        const RenderPresentationPtr& document, int slide, const QVariantMap& theme, const QSize& size)
    {
        if (!document || !document->frame_cache || slide < 0 || size.isEmpty() ||
            static_cast<std::size_t>(slide) >= document->thumbnail_revisions.size())
        {
            return {};
        }
        const QString key =
            frame_key(document->thumbnail_revisions[static_cast<std::size_t>(slide)], slide, theme, size);
        QMutexLocker lock(&document->frame_cache->mutex);
        const auto* cached = document->frame_cache->images.object(key);
        return cached ? *cached : QImage{};
    }

    void cache_presentation_frame(const RenderPresentationPtr& document, int slide, const QVariantMap& theme,
        const QSize& size, const QImage& image)
    {
        if (!document || !document->frame_cache || slide < 0 || size.isEmpty() || image.isNull() ||
            static_cast<std::size_t>(slide) >= document->thumbnail_revisions.size())
        {
            return;
        }
        const QString key =
            frame_key(document->thumbnail_revisions[static_cast<std::size_t>(slide)], slide, theme, size);
        const int cost = static_cast<int>(std::max<qsizetype>(1, (image.sizeInBytes() + 1023) / 1024));
        QMutexLocker lock(&document->frame_cache->mutex);
        if (!document->frame_cache->images.object(key))
        {
            document->frame_cache->images.insert(key, new QImage(image), cost);
        }
    }
}
