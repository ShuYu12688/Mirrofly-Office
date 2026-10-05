#pragma once

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <filesystem>
#include <system_error>

namespace mirrorfly
{
    inline bool new_document_destination(const QUrl& destination, const QStringList& extensions)
    {
        if (!destination.isLocalFile())
            return false;
        const QFileInfo file(destination.toLocalFile());
        return file.isAbsolute() && extensions.contains(file.suffix().toLower()) && !file.exists() &&
            !file.isSymLink() && !file.isJunction() && file.dir().exists();
    }

    inline bool same_document_path(const QString& first, const QString& second)
    {
        if (first.isEmpty() || second.isEmpty())
        {
            return false;
        }
        const auto normalize = [](const QString& path)
        {
            const QFileInfo information(path);
            const QString canonical = information.canonicalFilePath();
            return QDir::cleanPath(canonical.isEmpty() ? information.absoluteFilePath() : canonical);
        };
        const auto left = normalize(first);
        const auto right = normalize(second);
#ifdef Q_OS_WIN
        if (left.compare(right, Qt::CaseInsensitive) == 0)
#else
        if (left == right)
#endif
        {
            return true;
        }
        const auto left_path = std::filesystem::u8path(left.toUtf8().toStdString());
        const auto right_path = std::filesystem::u8path(right.toUtf8().toStdString());
        std::error_code error;
        if (std::filesystem::equivalent(left_path, right_path, error) && !error)
        {
            return true;
        }
        const auto left_name = QFileInfo(left).fileName();
        const auto right_name = QFileInfo(right).fileName();
#ifdef Q_OS_WIN
        const bool same_name = left_name.compare(right_name, Qt::CaseInsensitive) == 0;
#else
        const bool same_name = left_name == right_name;
#endif
        error.clear();
        return same_name &&
            std::filesystem::equivalent(left_path.parent_path(), right_path.parent_path(), error) && !error;
    }
}
