#include "office_ai_log.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace mirrorfly
{
    OfficeAiLog::OfficeAiLog(const QString& path) : path_(path)
    {
        if (path_.isEmpty())
            path_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                "/ai/diagnostics.jsonl";
        worker_ = new QObject;
        worker_->moveToThread(&thread_);
        QObject::connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
        thread_.start();
    }

    OfficeAiLog::~OfficeAiLog()
    {
        // The queued barrier drains accepted writes before the worker exits.
        QMetaObject::invokeMethod(worker_, []()
        {
            QThread::currentThread()->quit();
        }, Qt::QueuedConnection);
        thread_.wait();
    }

    void OfficeAiLog::append(const QJsonObject& record)
    {
        QMetaObject::invokeMethod(worker_, [path = path_, record]()
        {
            const auto folder = QFileInfo(path).absoluteDir();
            if (!QDir().mkpath(folder.absolutePath()))
                return;
            if (QFileInfo(path).size() > 2 * 1024 * 1024)
            {
                const QString previous =
                    folder.filePath(QFileInfo(path).completeBaseName() + ".previous.jsonl");
                QFile::remove(previous);
                if (!QFile::rename(path, previous))
                    return;
            }
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append))
                file.write(QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n');
        }, Qt::QueuedConnection);
    }
}
