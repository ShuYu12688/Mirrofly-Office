#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

namespace mirrorfly
{
    QJsonArray office_ai_compose_tools(const QSet<QString>& groups);
    QJsonArray office_ai_document_tools(const QSet<QString>& groups);
    QJsonArray office_ai_tools(const QSet<QString>& groups);
    QJsonObject office_ai_groups();
    QJsonObject office_ai_guide(const QString& group);
    QString office_ai_compose_guide(const QString& module);
    QString office_ai_system_prompt();
    struct OfficeAiToolTraits
    {
        bool known = false;
        bool observation = false;
        bool independent = false;
        bool asynchronous = false;
        bool document_work = false;
        bool saving = false;
    };

    OfficeAiToolTraits office_ai_tool_traits(const QString& name, const QJsonObject& arguments);
    bool office_ai_permitted(const QString& module, const QString& action);
}
