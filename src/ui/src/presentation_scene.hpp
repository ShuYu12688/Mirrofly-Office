#pragma once

#include <mirrorfly/presentation.hpp>

#include <QHash>
#include <QImage>
#include <QMetaType>
#include <QPainter>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

namespace mirrorfly
{
    class PresentationImageCache;
    class PresentationFrameCache;
    class PresentationFontLoader;
    class PresentationThumbnailCache;

    struct PresentationRenderEnvironment
    {
        QHash<QString, QString> installed_fonts;
        QStringList cjk_fonts;
        QString latin_fallback;
        QString cjk_fallback;
    };

    using PresentationRenderEnvironmentPtr = std::shared_ptr<const PresentationRenderEnvironment>;

    enum class PresentationEditLayerChange
    {
        Replace,
        Remove
    };

    struct PresentationPrepareOptions
    {
        PresentationRenderEnvironmentPtr environment;
        std::function<void(std::size_t completed, std::size_t total)> progress;
        bool reuse_analysis = false;
        bool reuse_thumbnails = false;
        bool eager_image_analysis = true;
        // Zero requests a new visual revision; retained revisions can share bounded cache entries.
        std::vector<std::uint64_t> thumbnail_revisions;
        // Only the named object's pixels changed; all other objects retain their stacking order.
        int edited_slide = -1;
        int edited_shape = -1;
        PresentationEditLayerChange edit_layer_change = PresentationEditLayerChange::Replace;
    };

    struct RenderPresentation
    {
        std::shared_ptr<const PresentationScene> scene;
        QHash<QString, QString> fonts;
        QSet<QString> embedded_fonts;
        QSet<QString> substituted_fonts;
        QString latin_fallback;
        QString cjk_fallback;
        QString font_summary;
        QHash<QString, std::size_t> image_index;
        QHash<QString, QSize> image_sizes;
        std::shared_ptr<PresentationImageCache> image_cache;
        std::shared_ptr<PresentationImageCache> thumbnail_image_cache;
        // Thumbnail views may scale an already decoded full image without decoding the asset again.
        std::shared_ptr<PresentationImageCache> full_image_cache;
        std::shared_ptr<PresentationFrameCache> frame_cache;
        std::shared_ptr<PresentationThumbnailCache> thumbnail_cache;
        std::vector<std::uint64_t> thumbnail_revisions;
        std::shared_ptr<PresentationFontLoader> font_loader;
        PresentationRenderEnvironmentPtr environment;
        std::shared_ptr<const std::atomic_uint64_t> render_request_token;
        std::uint64_t render_request_revision = 0;
        int image_decode_side = 1536;
        bool load_embedded_fonts = true;
        bool allow_image_decode = true;
        std::uint64_t edit_layer_source_revision = 0;
        int edited_slide = -1;
        int edited_shape = -1;
        PresentationEditLayerChange edit_layer_change = PresentationEditLayerChange::Replace;

        bool renderCancelled() const
        {
            return render_request_token &&
                render_request_token->load(std::memory_order_acquire) != render_request_revision;
        }
    };

    using RenderPresentationPtr = std::shared_ptr<const RenderPresentation>;

    bool presentation_east_asian_character(char32_t character);
    PresentationRenderEnvironmentPtr presentation_render_environment();
    RenderPresentationPtr prepare_presentation(std::shared_ptr<const PresentationScene> scene,
        const RenderPresentationPtr& previous = {}, bool reuse_analysis = false);
    RenderPresentationPtr prepare_presentation(std::shared_ptr<const PresentationScene> scene,
        const RenderPresentationPtr& previous, const PresentationPrepareOptions& options);
    QImage presentation_image(const RenderPresentationPtr& document, const std::string& path);
    RenderPresentationPtr presentation_thumbnail_document(const RenderPresentationPtr& document);
    RenderPresentationPtr presentation_interaction_document(const RenderPresentationPtr& document);
    void ensure_presentation_fonts(const RenderPresentationPtr& document, std::size_t slide);
    bool presentation_embedded_font_loaded(const RenderPresentationPtr& document, const QString& family);
    QSize presentation_image_source_size(
        const RenderPresentationPtr& document, const std::string& path, const QSize& fallback = {});
    QImage cached_presentation_thumbnail(const RenderPresentationPtr& document, const QString& key);
    QString presentation_thumbnail_key(
        const RenderPresentationPtr& document, int slide, const QVariantMap& theme);
    void cache_presentation_thumbnail(
        const RenderPresentationPtr& document, const QString& key, const QImage& image);
    QImage cached_presentation_frame(
        const RenderPresentationPtr& document, int slide, const QVariantMap& theme, const QSize& size);
    void cache_presentation_frame(const RenderPresentationPtr& document, int slide, const QVariantMap& theme,
        const QSize& size, const QImage& image);
    void paint_presentation_slide(QPainter& painter, const RenderPresentationPtr& document, std::size_t index,
        const QVariantMap& theme, const QRectF& area);
}

Q_DECLARE_METATYPE(mirrorfly::RenderPresentationPtr)
