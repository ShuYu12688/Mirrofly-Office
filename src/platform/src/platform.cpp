#include "mirrorfly/platform.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <unordered_set>

namespace
{

    constexpr qint64 maximum_history_bytes = 1024 * 1024;
    constexpr std::size_t maximum_history_entries = 30;

    QString recent_file_path()
    {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

        if (directory.isEmpty())
        {
            return {};
        }

        return QDir(directory).filePath(QStringLiteral("recent.json"));
    }

    QString from_utf8(const std::string& value)
    {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }

    std::string to_utf8(const QString& value)
    {
        return value.toUtf8().toStdString();
    }

    std::string format_size(qint64 bytes)
    {
        constexpr qint64 bytes_per_kilobyte = 1024;
        constexpr qint64 bytes_per_megabyte = bytes_per_kilobyte * 1024;

        if (bytes >= bytes_per_megabyte)
        {
            const double megabytes = static_cast<double>(bytes) / static_cast<double>(bytes_per_megabyte);
            return to_utf8(QString::number(megabytes, 'f', 1) + QStringLiteral(" MB"));
        }

        const qint64 kilobytes = (bytes + bytes_per_kilobyte - 1) / bytes_per_kilobyte;
        return to_utf8(QString::number(kilobytes) + QStringLiteral(" KB"));
    }

    QJsonObject serialize_file(const mirrorfly::RecentFile& file)
    {
        QJsonObject object;
        object.insert(QStringLiteral("path"), from_utf8(file.path));
        object.insert(QStringLiteral("name"), from_utf8(file.name));
        object.insert(QStringLiteral("modified"), from_utf8(file.modified));
        object.insert(QStringLiteral("size_text"), from_utf8(file.size_text));
        object.insert(QStringLiteral("starred"), file.starred);
        return object;
    }

    std::optional<mirrorfly::RecentFile> deserialize_file(const QJsonValue& value)
    {
        if (!value.isObject())
        {
            return std::nullopt;
        }

        const QJsonObject object = value.toObject();
        const QJsonValue path = object.value(QStringLiteral("path"));
        const QJsonValue name = object.value(QStringLiteral("name"));
        const QJsonValue modified = object.value(QStringLiteral("modified"));
        const QJsonValue size = object.value(QStringLiteral("size_text"));
        const QJsonValue starred = object.value(QStringLiteral("starred"));

        if (!path.isString() || path.toString().isEmpty() || !name.isString() || !modified.isString() ||
            !size.isString() || !starred.isBool())
        {
            return std::nullopt;
        }

        mirrorfly::RecentFile file;
        file.path = to_utf8(path.toString());
        file.name = to_utf8(name.toString());
        file.modified = to_utf8(modified.toString());
        file.size_text = to_utf8(size.toString());
        file.kind = mirrorfly::classify_document(file.path);
        file.starred = starred.toBool();
        return file;
    }

}

namespace mirrorfly
{

    std::vector<RecentFile> load_recent_files()
    {
        const QString path = recent_file_path();

        if (path.isEmpty())
        {
            return {};
        }

        QFile source(path);

        if (!source.open(QIODevice::ReadOnly) || source.size() > maximum_history_bytes)
        {
            return {};
        }

        // Bound the read as well, in case the file grows after the size check.
        const QByteArray data = source.read(maximum_history_bytes + 1);

        if (data.size() > maximum_history_bytes || source.error() != QFileDevice::NoError)
        {
            return {};
        }

        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);

        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            return {};
        }

        const QJsonObject root = document.object();
        const QJsonValue entries = root.value(QStringLiteral("files"));

        if (root.value(QStringLiteral("version")).toInt(-1) != 1 || !entries.isArray())
        {
            return {};
        }

        const QJsonArray array = entries.toArray();
        std::vector<RecentFile> files;
        files.reserve(std::min(maximum_history_entries, static_cast<std::size_t>(array.size())));
        std::unordered_set<std::string> seen_paths;

        for (const QJsonValue& value : array)
        {
            const std::optional<RecentFile> file = deserialize_file(value);

            if (file && seen_paths.insert(file->path).second)
            {
                files.push_back(*file);

                if (files.size() == maximum_history_entries)
                {
                    break;
                }
            }
        }

        return files;
    }

    bool save_recent_files(const std::vector<RecentFile>& files)
    {
        const QString path = recent_file_path();

        if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath()))
        {
            return false;
        }

        QJsonArray array;

        for (const RecentFile& file : files)
        {
            if (static_cast<std::size_t>(array.size()) == maximum_history_entries)
            {
                break;
            }

            array.append(serialize_file(file));
        }

        QJsonObject root;
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("files"), array);
        const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);

        if (data.size() > maximum_history_bytes)
        {
            return false;
        }

        QSaveFile destination(path);

        if (!destination.open(QIODevice::WriteOnly))
        {
            return false;
        }

        if (destination.write(data) != data.size())
        {
            destination.cancelWriting();
            return false;
        }

        return destination.commit();
    }

    std::optional<RecentFile> inspect_local_file(const std::string& path)
    {
        if (path.empty())
        {
            return std::nullopt;
        }

        const QFileInfo information(from_utf8(path));

        if (!information.exists() || !information.isFile())
        {
            return std::nullopt;
        }

        RecentFile file;
        file.path = to_utf8(QDir::cleanPath(information.absoluteFilePath()));
        file.name = to_utf8(information.fileName());
        file.modified = to_utf8(information.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        file.size_text = format_size(information.size());
        file.kind = classify_document(file.path);
        return file;
    }

}
