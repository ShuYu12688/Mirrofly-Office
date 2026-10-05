#include <mirrorfly/mindmap_storage.hpp>

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

    mirrorfly::MindMapError read_bytes(const QString& path, QByteArray& bytes)
    {
        const QFileInfo information(path);
        if (!information.exists() || !information.isFile() || information.isSymLink() ||
            information.isJunction())
        {
            return mirrorfly::MindMapError::ReadFailed;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.isSequential())
        {
            return mirrorfly::MindMapError::ReadFailed;
        }
        constexpr auto maximum_size = static_cast<qint64>(mirrorfly::maximum_mindmap_xml_bytes);
        if (file.size() > maximum_size)
        {
            return mirrorfly::MindMapError::TooLarge;
        }
        bytes = file.read(maximum_size + 1);
        if (bytes.size() > maximum_size)
        {
            bytes.clear();
            return mirrorfly::MindMapError::TooLarge;
        }
        if (file.error() != QFileDevice::NoError)
        {
            bytes.clear();
            return mirrorfly::MindMapError::ReadFailed;
        }
        return mirrorfly::MindMapError::None;
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
        return read_bytes(path, current) == mirrorfly::MindMapError::None &&
            revision_of(current) == expected_revision;
    }

    mirrorfly::MindMapFileResult failure(
        mirrorfly::MindMapError error, const std::string& message, const std::string& path = {})
    {
        mirrorfly::MindMapFileResult result;
        result.error = error;
        result.message = message;
        result.path = path;
        return result;
    }
}

namespace mirrorfly
{
    MindMapFileResult load_mindmap_file(const std::string& path)
    {
        if (!is_mindmap_path(path))
        {
            return failure(MindMapError::UnsupportedType, "仅支持 .mfg 自由导图。");
        }
        const QString absolute_path = QDir::cleanPath(QFileInfo(from_utf8(path)).absoluteFilePath());
        const std::string result_path = absolute_path.toUtf8().toStdString();
        QByteArray bytes;
        const auto read_error = read_bytes(absolute_path, bytes);
        if (read_error != MindMapError::None)
        {
            return failure(read_error,
                read_error == MindMapError::TooLarge ? "思维导图 XML 超过 2 MiB。" : "无法读取思维导图文件。",
                result_path);
        }
        auto parsed = parse_mindmap(bytes.toStdString());
        if (parsed.error != MindMapError::None)
        {
            return failure(parsed.error, parsed.message, result_path);
        }
        MindMapFileResult result;
        result.path = result_path;
        result.revision = revision_of(bytes);
        result.document = std::move(parsed.document);
        return result;
    }

    MindMapFileResult save_mindmap_file(
        const std::string& path, const MindMapDocument& document, const std::string& expected_revision)
    {
        if (!is_mindmap_path(path))
        {
            return failure(MindMapError::UnsupportedType, "请使用 .mfg 扩展名。");
        }
        const QString absolute_path = QDir::cleanPath(QFileInfo(from_utf8(path)).absoluteFilePath());
        const std::string result_path = absolute_path.toUtf8().toStdString();
        const auto serialized = serialize_mindmap(document);
        if (serialized.error != MindMapError::None)
        {
            return failure(serialized.error, serialized.message, result_path);
        }
        if (!writable_destination(absolute_path))
        {
            return failure(MindMapError::WriteFailed, "思维导图保存位置不可写。", result_path);
        }
        if (!revision_matches(absolute_path, expected_revision))
        {
            return failure(MindMapError::ChangedOnDisk,
                "文件已被其他程序修改或移走，请另存为。当前草稿已保留。", result_path);
        }

        QSaveFile destination(absolute_path);
        destination.setDirectWriteFallback(false);
        if (!destination.open(QIODevice::WriteOnly))
        {
            return failure(MindMapError::WriteFailed, "无法创建思维导图临时文件。", result_path);
        }
        const QByteArray bytes(serialized.text.data(), static_cast<qsizetype>(serialized.text.size()));
        if (destination.write(bytes) != bytes.size())
        {
            destination.cancelWriting();
            return failure(MindMapError::WriteFailed, "思维导图写入失败。", result_path);
        }
        // The final check narrows the race window before atomic replacement.
        if (!writable_destination(absolute_path) || !revision_matches(absolute_path, expected_revision))
        {
            destination.cancelWriting();
            return failure(MindMapError::ChangedOnDisk,
                "文件已被其他程序修改或移走，请另存为。当前草稿已保留。", result_path);
        }
        if (!destination.commit())
        {
            return failure(MindMapError::WriteFailed, "思维导图原子提交失败。", result_path);
        }
        MindMapFileResult result;
        result.path = result_path;
        result.revision = revision_of(bytes);
        result.document = document;
        return result;
    }
}
