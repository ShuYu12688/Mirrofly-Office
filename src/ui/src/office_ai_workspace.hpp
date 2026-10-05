#pragma once

#include <QJsonObject>

namespace mirrorfly
{
    QJsonObject office_ai_workspace(const QJsonObject& snapshot);
    QString office_ai_module_for_file(const QString& path);
    QString office_ai_document_key(const QJsonObject& snapshot);
}
