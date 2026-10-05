#pragma once

#include <QJsonObject>
#include <QObject>
#include <QThread>

namespace mirrorfly
{
    // Technical metadata only. No conversation database or model-readable history.
    class OfficeAiLog final
    {
    public:
        explicit OfficeAiLog(const QString& path = {});
        ~OfficeAiLog();
        void append(const QJsonObject& record);

    private:
        QString path_;
        QThread thread_;
        QObject* worker_ = nullptr;
    };
}
