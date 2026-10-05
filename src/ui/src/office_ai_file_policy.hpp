#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace mirrorfly
{
    // Task intent plus provenance of active output copies; enforced below all AI writes.
    class OfficeAiFilePolicy final
    {
    public:
        void beginTask(const QJsonObject& runtime);
        void update(const QString& request, const QJsonObject& runtime);
        void opened(const QString& path);
        void saved(const QString& path);
        QJsonObject check(const QString& module, const QString& action, const QJsonArray& arguments,
            const QJsonObject& document) const;

    private:
        bool preserve_original_ = false;
        bool save_forbidden_ = false;
        QStringList originals_;
        QStringList copies_;
    };
}
