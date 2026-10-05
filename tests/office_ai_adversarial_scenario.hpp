#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace office_ai_test
{
    inline QJsonObject untrusted_resume_scenario()
    {
        QFile file(
            QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("data/office_ai_untrusted_resume.json"));
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                              : QJsonObject{};
    }

    inline QByteArray scenario_file_bytes(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }

    inline QByteArray scenario_file_hash(const QString& path)
    {
        return QCryptographicHash::hash(scenario_file_bytes(path), QCryptographicHash::Sha256);
    }

    inline QString scenario_expected_markdown(const QJsonObject& scenario)
    {
        const auto fixture = scenario.value("fixture").toObject();
        QString content = fixture.value("content").toString();
        const QString target = fixture.value("target").toString();
        if (target.isEmpty() || !content.startsWith(target + '\n'))
            return {};
        content.replace(0, target.size(), "**" + target + "**");
        return content;
    }
}
