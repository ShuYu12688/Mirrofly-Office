#include "presentation_image_export.hpp"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>

namespace
{
    QString folder_name(const QString& document_name, const QString& format)
    {
        QString stem = QFileInfo(document_name).completeBaseName();
        QString safe;
        for (const QChar character : stem)
        {
            if (safe.size() >= 40)
                break;
            safe +=
                character.isLetterOrNumber() || character == '-' || character == '_' ? character : QChar('-');
        }
        while (safe.endsWith('-'))
            safe.chop(1);
        if (safe.isEmpty())
            safe = QStringLiteral("presentation");
        return safe + QStringLiteral("-slides-") + format;
    }
}

namespace mirrorfly
{
    PresentationImageExportResult render_presentation_images(const RenderPresentationPtr& document,
        int current_slide, const QVariantMap& theme, const QString& parent_path, const QString& document_name,
        const PresentationImageExportOptions& options, PresentationImageExportProgress& progress)
    {
        if (!document || !document->scene || document->scene->slides.empty())
            return {false, QStringLiteral("演示文稿没有可导出的页面。")};
        const auto& scene = *document->scene;
        if (!std::isfinite(scene.width) || !std::isfinite(scene.height) || scene.width <= 0 ||
            scene.height <= 0 || !QStringList{"png", "jpg"}.contains(options.format) ||
            !QStringList{"all", "current"}.contains(options.scope) ||
            !QList<int>{1280, 1920}.contains(options.long_edge) || current_slide < 0 ||
            current_slide >= static_cast<int>(scene.slides.size()))
            return {false, QStringLiteral("图片导出参数或页面尺寸无效。")};
        QDir parent(parent_path);
        if (!parent.exists() || !QFileInfo(parent.absolutePath()).isWritable())
            return {false, QStringLiteral("所选目录不可写。")};
        const double scale = options.long_edge / std::max(scene.width, scene.height);
        const int width = std::max(1, static_cast<int>(std::lround(scene.width * scale)));
        const int height = std::max(1, static_cast<int>(std::lround(scene.height * scale)));
        if (static_cast<qint64>(width) * height > 16'000'000)
            return {false, QStringLiteral("导出尺寸超过安全限制。")};

        QVector<int> pages;
        for (int index = 0; index < static_cast<int>(scene.slides.size()); ++index)
        {
            if (options.scope == "current" && index != current_slide)
                continue;
            if (options.scope == "all" && scene.slides[static_cast<std::size_t>(index)].hidden)
                continue;
            pages.push_back(index);
        }
        if (pages.empty())
            return {false, QStringLiteral("没有可导出的可见幻灯片。")};
        progress.total.store(pages.size());
        QTemporaryDir staging(parent.filePath(QStringLiteral(".mirrorfly-slides-XXXXXX")));
        if (!staging.isValid())
            return {false, QStringLiteral("无法创建临时导出目录。")};
        qint64 bytes = 0;
        for (const int page : pages)
        {
            if (progress.cancelled.load())
                return {false, QStringLiteral("图片导出已取消。")};
            QImage image(width, height, QImage::Format_RGB32);
            if (image.isNull())
                return {false, QStringLiteral("无法分配页面图片内存。")};
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.setRenderHints(
                QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
            paint_presentation_slide(
                painter, document, static_cast<std::size_t>(page), theme, QRectF(0, 0, width, height));
            painter.end();
            if (progress.cancelled.load())
                return {false, QStringLiteral("图片导出已取消。")};
            const QString filename =
                QStringLiteral("slide-%1.%2").arg(page + 1, 3, 10, QChar('0')).arg(options.format);
            const QString path = staging.filePath(filename);
            if (!image.save(
                    path, options.format == "png" ? "PNG" : "JPEG", options.format == "png" ? -1 : 90))
                return {false, QStringLiteral("无法写入页面图片。")};
            bytes += QFileInfo(path).size();
            progress.completed.fetch_add(1);
        }
        if (progress.cancelled.load())
            return {false, QStringLiteral("图片导出已取消。")};
        const QString base = folder_name(document_name, options.format);
        for (int suffix = 1; suffix <= 999; ++suffix)
        {
            const QString name = suffix == 1 ? base : base + '-' + QString::number(suffix);
            if (parent.exists(name))
                continue;
            if (parent.rename(QFileInfo(staging.path()).fileName(), name))
                return {true, QStringLiteral("已导出 %1 张图片。 ").arg(pages.size()), parent.filePath(name),
                    static_cast<int>(pages.size()), bytes};
        }
        return {false, QStringLiteral("无法建立最终导出目录。")};
    }
}
