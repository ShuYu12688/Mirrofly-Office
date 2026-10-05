#include "markdown_image.hpp"
#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QTextDocument>
#include <algorithm>

namespace
{
    QImage read_image(const QUrl& base, const QString& source)
    {
        const auto url = base.resolved(QUrl(source));
        // Opening a document never initiates a network request, including file://host shares.
        if (!url.isLocalFile() || !url.host().isEmpty())
            return {};
        const auto path = url.toLocalFile();
        if (path.startsWith("//") || path.startsWith(QStringLiteral("\\\\")))
            return {};
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 24 * 1024 * 1024)
            return {};
        QByteArray bytes = file.read(24 * 1024 * 1024 + 1);
        if (bytes.size() > 24 * 1024 * 1024)
            return {};
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const auto format = reader.format().toLower();
        if (format != "png" && format != "jpeg" && format != "bmp" && format != "gif" && format != "webp")
            return {};
        const auto size = reader.size();
        if (size.width() <= 0 || size.height() <= 0 || qint64(size.width()) * size.height() > 16000000)
            return {};
        reader.setAutoTransform(true);
        if (size.width() > 2048 || size.height() > 2048)
            reader.setScaledSize(size.scaled(2048, 2048, Qt::KeepAspectRatio));
        return reader.read();
    }
}
namespace mirrorfly
{
    void insert_markdown_image(QTextCursor& cursor, const MarkdownImageInfo& image, const QVariantMap& theme)
    {
        auto* document = cursor.document();
        auto cached = document->property("markdownImageResources").toMap();
        const auto identity = document->baseUrl().toEncoded() + '\0' + QByteArray::fromStdString(image.url);
        const auto digest = [](const QByteArray& value)
        {
            return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
        };
        const auto raster_key = digest(identity);
        // Alternative-text edits reuse an already decoded raster; placeholders retain their own caption.
        QString key = cached.contains(raster_key)
            ? raster_key
            : digest(identity + '\0' + QByteArray::fromStdString(image.alt));
        // The undo stack can retain obsolete objects; bound document-lifetime resources as well as input.
        const auto allocated = document->property("markdownImageBytes").toLongLong();
        // Reserve room for one maximum decoded image and the shared placeholder under 64 MiB.
        const bool exhausted = cached.size() >= 256 || allocated >= 47 * 1024 * 1024;
        if (exhausted && !cached.contains(key))
            key = "budget-placeholder";
        QUrl name("mirrorfly-image:" + key);
        QImage pixels;
        if (cached.contains(key))
            pixels = document->resource(QTextDocument::ImageResource, name).value<QImage>();
        else
        {
            if (!exhausted)
                pixels = read_image(document->baseUrl(), QString::fromStdString(image.url));
            if (!pixels.isNull())
            {
                key = raster_key;
                name = QUrl("mirrorfly-image:" + key);
            }
            if (pixels.isNull())
            {
                pixels =
                    QImage(std::clamp(theme.value("markdownImagePlaceholderWidth", 180).toInt(), 32, 640),
                        std::clamp(theme.value("markdownImagePlaceholderHeight", 42).toInt(), 16, 120),
                        QImage::Format_ARGB32_Premultiplied);
                pixels.fill(QColor(theme.value("surfaceColor", "#EEEEEE").toString()));
                QPainter painter(&pixels);
                painter.setPen(QColor(theme.value("textSecondary", "#555555").toString()));
                auto text = QStringLiteral("图片");
                if (!exhausted && !image.alt.empty())
                    text = QString::fromStdString(image.alt);
                painter.drawText(pixels.rect().adjusted(8, 4, -8, -4), Qt::AlignCenter,
                    painter.fontMetrics().elidedText(text, Qt::ElideRight, pixels.width() - 16));
            }
            document->addResource(QTextDocument::ImageResource, name, pixels);
            cached.insert(key, true);
            document->setProperty("markdownImageResources", cached);
            document->setProperty("markdownImageBytes", allocated + pixels.sizeInBytes());
        }
        QTextImageFormat format;
        format.merge(cursor.charFormat());
        format.setObjectType(QTextFormat::ImageObject);
        format.setName(name.toString());
        format.setProperty(markdown_image_url_property, QString::fromStdString(image.url));
        format.setProperty(QTextFormat::ImageAltText, QString::fromStdString(image.alt));
        format.setProperty(QTextFormat::ImageTitle, QString::fromStdString(image.title));
        const auto size = pixels.size().boundedTo(QSize(theme.value("markdownImageMaximumWidth", 640).toInt(),
            theme.value("markdownImageMaximumHeight", 480).toInt()));
        const auto scaled = pixels.size().scaled(size, Qt::KeepAspectRatio);
        format.setWidth(scaled.width());
        format.setHeight(scaled.height());
        const auto original = cursor.charFormat();
        cursor.insertImage(format);
        cursor.setCharFormat(original.isImageFormat() ? QTextCharFormat{} : original);
    }
}
