#pragma once

#include <QJsonObject>
#include <QString>

namespace mirrorfly
{
    QJsonObject office_ai_style_catalog(const QString& id = {});
    QJsonObject office_ai_style_theme(const QString& id);
}
