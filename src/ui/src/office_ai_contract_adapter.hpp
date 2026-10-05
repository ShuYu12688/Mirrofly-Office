#pragma once

#include <QJsonObject>

namespace mirrorfly
{
    QJsonObject office_ai_action_signature(
        const QJsonObject& catalog, const QString& module, const QString& action);
    QJsonObject office_ai_normalize_action(
        const QJsonObject& step, const QJsonObject& signature, int current_page = -1);
}
