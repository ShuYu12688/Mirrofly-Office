#pragma once

#include <QJsonObject>
#include <QVariantMap>

namespace mirrorfly
{
    QJsonObject office_ai_word_recipe(const QJsonObject& arguments, const QVariantMap& theme);
    QJsonObject office_ai_table_recipe(const QJsonObject& arguments, const QVariantMap& theme);
}
