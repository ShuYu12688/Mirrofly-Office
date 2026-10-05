#pragma once
#include <QJsonArray>
#include <QJsonObject>

namespace mirrorfly::ai_tools
{
    inline QJsonObject definition(const QString& name, const QString& description,
        const QJsonObject& properties = {}, const QJsonArray& required = {})
    {
        return {{"name", name}, {"description", description},
            {"parameters",
                QJsonObject{{"type", "object"}, {"properties", properties}, {"required", required},
                    {"additionalProperties", false}}}};
    }

    inline QJsonObject string_type()
    {
        return {{"type", "string"}};
    }
}
