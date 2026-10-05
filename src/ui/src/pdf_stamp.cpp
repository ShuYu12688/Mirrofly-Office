#include "pdf_stamp.hpp"
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>
#include <QUrl>
#include <cmath>
#include <mirrorfly/presentation_storage.hpp>

namespace mirrorfly
{
    PdfBytesResult make_pdf_stamp(
        const QString& action, const QSizeF& size, const QVariantMap& options, int page_rotation)
    {
        const auto text = options.value("text").toString();
        const double font_size = options.value("size", action == "watermark" ? 36 : 14).toDouble();
        const double opacity = options.value("opacity", action == "watermark" ? .22 : 1).toDouble();
        const QColor color(options.value("color", "#35434E").toString());
        const auto family = options.value("font", "Microsoft YaHei").toString();
        if (!size.isValid() || size.width() > 20000 || size.height() > 20000 || size.width() < 4 ||
            size.height() < 4 || !std::isfinite(font_size) || font_size < 6 || font_size > 96 ||
            !std::isfinite(opacity) || opacity < .05 || opacity > 1 || !color.isValid() ||
            family.size() > 128 ||
            (action != "image" && (text.trimmed().isEmpty() || text.toUtf8().size() > 8192)))
            return {PdfError::InvalidAnnotation, "标注尺寸、颜色、文字或透明度无效。", {}};
        QImage image;
        if (action == "image")
        {
            const QUrl url(options.value("file").toString());
            if (!url.isLocalFile())
                return {PdfError::InvalidAnnotation, "请选择本地图片。", {}};
            const auto loaded = load_presentation_image_file(url.toLocalFile().toUtf8().toStdString());
            if (loaded.error != PresentationError::None)
                return {PdfError::InvalidAnnotation, "无法读取此图片。", {}};
            const auto& bytes = loaded.bytes;
            QByteArray data(
                reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
            QBuffer input(&data);
            input.open(QIODevice::ReadOnly);
            QImageReader reader(&input);
            const auto dimensions = reader.size();
            if (!dimensions.isValid() ||
                static_cast<qint64>(dimensions.width()) * dimensions.height() > 8 * 1024 * 1024)
                return {PdfError::TooLarge, "标注图片最多 800 万像素。", {}};
            image = reader.read();
            if (image.isNull())
                return {PdfError::InvalidAnnotation, "图片无法解码。", {}};
        }
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        {
            QPdfWriter writer(&buffer);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(size, QPageSize::Point, "Stamp", QPageSize::ExactMatch));
            writer.setPageMargins(QMarginsF(), QPageLayout::Point);
            QPainter painter(&writer);
            if (!painter.isActive())
                return {PdfError::WriteFailed, "无法准备图文标注。", {}};
            painter.setRenderHints(
                QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
            painter.setOpacity(opacity);
            QSizeF visible_size = size;
            if (page_rotation % 2 != 0)
                visible_size.transpose();
            if (page_rotation == 1)
            {
                painter.translate(0, size.height());
                painter.rotate(-90);
            }
            else if (page_rotation == 2)
            {
                painter.translate(size.width(), size.height());
                painter.rotate(-180);
            }
            else if (page_rotation == 3)
            {
                painter.translate(size.width(), 0);
                painter.rotate(-270);
            }
            const QRectF bounds(QPointF(), visible_size);
            if (!image.isNull())
            {
                const auto scaled = image.size().scaled(visible_size.toSize(), Qt::KeepAspectRatio);
                painter.drawImage(
                    QRectF(bounds.center() - QPointF(scaled.width() / 2.0, scaled.height() / 2.0), scaled),
                    image);
            }
            else
            {
                QFont font(family);
                font.setPointSizeF(font_size);
                painter.setFont(font);
                painter.setPen(color);
                if (action == "watermark")
                {
                    painter.translate(bounds.center());
                    painter.rotate(-24);
                    const QRectF watermark(
                        -visible_size.width() * .4, -font_size, visible_size.width() * .8, font_size * 2);
                    painter.drawText(watermark, Qt::AlignCenter | Qt::TextSingleLine, text);
                }
                else
                    painter.drawText(
                        bounds.adjusted(2, 2, -2, -2), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
            }
            if (!painter.end())
                return {PdfError::WriteFailed, "图文标注写入失败。", {}};
        }
        if (data.size() > 8 * 1024 * 1024)
            return {PdfError::TooLarge, "单个图文标注最多 8 MiB。", {}};
        PdfBytesResult result;
        result.bytes.assign(data.begin(), data.end());
        return result;
    }
}
