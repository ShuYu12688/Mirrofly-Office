#include <mirrorfly/presentation_storage.hpp>

#include "archive_storage.hpp"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include <utility>

namespace
{
    mirrorfly::archive_storage::Limits limits()
    {
        using namespace mirrorfly;
        return {maximum_presentation_archive_bytes, maximum_presentation_expanded_bytes,
            maximum_presentation_part_bytes, maximum_presentation_xml_bytes, maximum_presentation_parts};
    }

    mirrorfly::PresentationError presentation_error(mirrorfly::archive_storage::Error error)
    {
        using ArchiveError = mirrorfly::archive_storage::Error;
        using Error = mirrorfly::PresentationError;
        switch (error)
        {
        case ArchiveError::None:
            return Error::None;
        case ArchiveError::ReadFailed:
            return Error::ReadFailed;
        case ArchiveError::TooLarge:
            return Error::TooLarge;
        case ArchiveError::InvalidArchive:
            return Error::InvalidArchive;
        case ArchiveError::EncryptedArchive:
            return Error::EncryptedArchive;
        case ArchiveError::InvalidPackage:
            return Error::InvalidPackage;
        case ArchiveError::WriteFailed:
            return Error::WriteFailed;
        case ArchiveError::ChangedOnDisk:
            return Error::ChangedOnDisk;
        }
        return Error::InvalidPackage;
    }

    mirrorfly::PresentationResult failure(mirrorfly::PresentationError error, const std::string& message = {})
    {
        mirrorfly::PresentationResult result;
        result.error = error;
        result.message = message;
        return result;
    }

    void report_progress(const mirrorfly::PresentationLoadProgressCallback& progress,
        mirrorfly::PresentationLoadStage stage, std::size_t completed, std::size_t total) noexcept
    {
        if (!progress)
        {
            return;
        }
        try
        {
            progress({stage, completed, total});
        }
        catch (...)
        {
            // Progress reporting is observational and cannot invalidate a document.
        }
    }

    bool valid_image_bytes(const QByteArray& bytes, const QString& suffix)
    {
        if (suffix == QStringLiteral("png"))
        {
            return bytes.startsWith(QByteArray::fromHex("89504E470D0A1A0A"));
        }
        if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        {
            return bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
                static_cast<unsigned char>(bytes[1]) == 0xD8 && static_cast<unsigned char>(bytes[2]) == 0xFF;
        }
        if (suffix == QStringLiteral("gif"))
        {
            return bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a");
        }
        if (suffix == QStringLiteral("bmp"))
        {
            return bytes.startsWith("BM");
        }
        if (suffix == QStringLiteral("webp"))
        {
            return bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP";
        }
        return false;
    }

    std::string image_mime(const QString& suffix)
    {
        if (suffix == QStringLiteral("png"))
        {
            return "image/png";
        }
        if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        {
            return "image/jpeg";
        }
        if (suffix == QStringLiteral("gif"))
        {
            return "image/gif";
        }
        if (suffix == QStringLiteral("bmp"))
        {
            return "image/bmp";
        }
        if (suffix == QStringLiteral("webp"))
        {
            return "image/webp";
        }
        return {};
    }
}

namespace mirrorfly
{
    PresentationResult load_presentation_file(
        const std::string& path, const PresentationLoadProgressCallback& progress)
    {
        if (!is_presentation_path(path))
        {
            return failure(PresentationError::UnsupportedType);
        }
        report_progress(progress, PresentationLoadStage::Reading, 0, 1);
        auto archive = archive_storage::read(path, limits(),
            [&](archive_storage::ReadStage stage, std::size_t completed, std::size_t total)
        {
            PresentationLoadStage mapped = PresentationLoadStage::Reading;
            if (stage == archive_storage::ReadStage::Validating)
            {
                mapped = PresentationLoadStage::Validating;
            }
            else if (stage == archive_storage::ReadStage::Extracting)
            {
                mapped = PresentationLoadStage::Extracting;
            }
            report_progress(progress, mapped, completed, total);
        });
        if (archive.error != archive_storage::Error::None)
        {
            return failure(presentation_error(archive.error), archive.message);
        }
        try
        {
            auto result =
                parse_presentation(std::move(archive.parts), [&](std::size_t completed, std::size_t total)
            {
                report_progress(progress, PresentationLoadStage::Parsing, completed, total);
            });
            if (result.error == PresentationError::None)
            {
                result.path = path;
                result.revision = archive.revision;
                result.source_bytes = archive.source_bytes;
            }
            return result;
        }
        catch (const std::bad_alloc&)
        {
            return failure(PresentationError::TooLarge);
        }
    }

    PresentationResult save_presentation_file(const std::string& path,
        const std::vector<PresentationPart>& parts, const std::string& expected_revision,
        const OfficeSaveProgress& progress)
    {
        if (!is_presentation_path(path))
        {
            return failure(PresentationError::UnsupportedType);
        }
        const auto archive = archive_storage::write(path, parts, expected_revision, limits(), progress);
        if (archive.error != archive_storage::Error::None)
        {
            return failure(presentation_error(archive.error), archive.message);
        }
        PresentationResult result;
        result.path = path;
        result.revision = archive.revision;
        result.source_bytes = archive.source_bytes;
        return result;
    }

    PresentationResult save_presentation_bytes(
        const std::string& path, const std::string& bytes, const std::string& expected_revision)
    {
        if (!is_presentation_path(path))
        {
            return failure(PresentationError::UnsupportedType);
        }
        const auto archive = archive_storage::write_bytes(path, bytes, expected_revision, limits());
        if (archive.error != archive_storage::Error::None)
        {
            return failure(presentation_error(archive.error), archive.message);
        }
        PresentationResult result;
        result.path = path;
        result.revision = archive.revision;
        result.source_bytes = archive.source_bytes;
        return result;
    }

    PresentationImageFileResult load_presentation_image_file(const std::string& path)
    {
        PresentationImageFileResult result;
        const QString filename = QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
        const QFileInfo information(filename);
        const QString suffix = information.suffix().toLower();
        result.mime_type = image_mime(suffix);
        if (result.mime_type.empty())
        {
            result.error = PresentationError::InvalidImage;
            return result;
        }
        if (!information.isFile() || information.isSymLink() || information.isJunction() ||
            information.size() <= 0 ||
            information.size() > static_cast<qint64>(maximum_presentation_part_bytes))
        {
            result.error = PresentationError::InvalidImage;
            return result;
        }
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly) || file.isSequential())
        {
            result.error = PresentationError::ReadFailed;
            return result;
        }
        const auto bytes = file.read(static_cast<qint64>(maximum_presentation_part_bytes) + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() != information.size() ||
            !valid_image_bytes(bytes, suffix))
        {
            result.error = PresentationError::InvalidImage;
            return result;
        }
        QBuffer buffer;
        buffer.setData(bytes);
        if (!buffer.open(QIODevice::ReadOnly))
        {
            result.error = PresentationError::InvalidImage;
            return result;
        }
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (!size.isValid() || size.width() > 16384 || size.height() > 16384 ||
            static_cast<qint64>(size.width()) * size.height() > 16 * 1024 * 1024)
        {
            result.error = PresentationError::InvalidImage;
            return result;
        }
        const QImage image = reader.read();
        if (image.isNull())
        {
            result.error = PresentationError::InvalidImage;
            return result;
        }
        QByteArray normalized;
        QBuffer output(&normalized);
        if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG") || normalized.isEmpty() ||
            normalized.size() > static_cast<qsizetype>(maximum_presentation_part_bytes))
        {
            result.error = PresentationError::TooLarge;
            return result;
        }
        result.path = (information.completeBaseName() + QStringLiteral(".png")).toUtf8().toStdString();
        result.mime_type = "image/png";
        result.bytes.assign(normalized.constData(), static_cast<std::size_t>(normalized.size()));
        result.width = image.width();
        result.height = image.height();
        return result;
    }
}
