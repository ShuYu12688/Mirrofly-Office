#include "mirrorfly/text_storage.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <utility>

namespace
{

    QString from_utf8(const std::string& value)
    {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }

    std::string revision_of(const QByteArray& bytes)
    {
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
    }

    mirrorfly::TextFileError file_error(mirrorfly::TextError error)
    {
        switch (error)
        {
        case mirrorfly::TextError::None:
            return mirrorfly::TextFileError::None;
        case mirrorfly::TextError::TooLarge:
            return mirrorfly::TextFileError::TooLarge;
        case mirrorfly::TextError::InvalidEncoding:
            return mirrorfly::TextFileError::InvalidEncoding;
        case mirrorfly::TextError::BinaryContent:
            return mirrorfly::TextFileError::BinaryContent;
        }

        return mirrorfly::TextFileError::InvalidEncoding;
    }

    mirrorfly::TextFileError read_bytes(const QString& path, QByteArray& bytes)
    {
        const QFileInfo information(path);

        if (!information.exists() || !information.isFile() || information.isSymLink() ||
            information.isJunction())
        {
            return mirrorfly::TextFileError::ReadFailed;
        }

        QFile file(path);

        if (!file.open(QIODevice::ReadOnly) || file.isSequential())
        {
            return mirrorfly::TextFileError::ReadFailed;
        }

        constexpr auto maximum_size = static_cast<qint64>(mirrorfly::maximum_text_bytes);

        if (file.size() > maximum_size)
        {
            return mirrorfly::TextFileError::TooLarge;
        }

        bytes = file.read(maximum_size + 1);

        if (bytes.size() > maximum_size)
        {
            bytes.clear();
            return mirrorfly::TextFileError::TooLarge;
        }

        if (file.error() != QFileDevice::NoError)
        {
            bytes.clear();
            return mirrorfly::TextFileError::ReadFailed;
        }

        return mirrorfly::TextFileError::None;
    }

    bool writable_destination(const QString& path)
    {
        const QFileInfo information(path);

        if (information.isSymLink() || information.isJunction())
        {
            return false;
        }

        if (information.exists() && (!information.isFile() || !information.isWritable()))
        {
            return false;
        }

        const QFileInfo parent(information.absolutePath());
        return parent.exists() && parent.isDir();
    }

    bool revision_matches(const QString& path, const std::string& expected_revision)
    {
        if (expected_revision == "missing")
        {
            const QFileInfo target(path);
            return !target.exists() && !target.isSymLink() && !target.isJunction();
        }
        if (expected_revision.empty())
        {
            return true;
        }

        QByteArray current;
        return read_bytes(path, current) == mirrorfly::TextFileError::None &&
            revision_of(current) == expected_revision;
    }

}

namespace mirrorfly
{

    TextFileResult load_text_file(const std::string& path)
    {
        TextFileResult result;

        if (!is_plain_text_path(path))
        {
            result.error = TextFileError::UnsupportedType;
            return result;
        }

        const QString absolute_path = QDir::cleanPath(QFileInfo(from_utf8(path)).absoluteFilePath());
        result.path = absolute_path.toUtf8().toStdString();
        QByteArray bytes;
        result.error = read_bytes(absolute_path, bytes);

        if (result.error != TextFileError::None)
        {
            return result;
        }

        auto decoded = decode_text(bytes.toStdString());
        result.error = file_error(decoded.error);

        if (result.error == TextFileError::None)
        {
            result.text = std::move(decoded.text);
            result.format = decoded.format;
            result.revision = revision_of(bytes);
        }

        return result;
    }

    TextFileResult save_text_file(const std::string& path, const std::string& text, const TextFormat& format,
        const std::string& expected_revision)
    {
        TextFileResult result;

        if (!is_plain_text_path(path))
        {
            result.error = TextFileError::UnsupportedType;
            return result;
        }

        const QString absolute_path = QDir::cleanPath(QFileInfo(from_utf8(path)).absoluteFilePath());
        result.path = absolute_path.toUtf8().toStdString();
        const auto encoded = encode_text(text, format);
        result.error = file_error(encoded.error);

        if (result.error != TextFileError::None)
        {
            return result;
        }

        if (!writable_destination(absolute_path))
        {
            result.error = TextFileError::WriteFailed;
            return result;
        }

        if (!revision_matches(absolute_path, expected_revision))
        {
            result.error = TextFileError::ChangedOnDisk;
            return result;
        }

        QSaveFile destination(absolute_path);
        destination.setDirectWriteFallback(false);

        if (!destination.open(QIODevice::WriteOnly))
        {
            result.error = TextFileError::WriteFailed;
            return result;
        }

        const QByteArray bytes(encoded.bytes.data(), static_cast<qsizetype>(encoded.bytes.size()));

        if (destination.write(bytes) != bytes.size())
        {
            destination.cancelWriting();
            result.error = TextFileError::WriteFailed;
            return result;
        }

        // Recheck after writing the temporary file, immediately before its replacement.
        if (!writable_destination(absolute_path) || !revision_matches(absolute_path, expected_revision))
        {
            destination.cancelWriting();
            result.error = TextFileError::ChangedOnDisk;
            return result;
        }

        if (!destination.commit())
        {
            result.error = TextFileError::WriteFailed;
            return result;
        }

        auto normalized = decode_text(encoded.bytes);
        result.text = std::move(normalized.text);
        result.format = format;
        result.format.mixed_line_endings = false;
        result.revision = revision_of(bytes);
        return result;
    }

}
