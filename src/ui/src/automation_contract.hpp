#pragma once

#include <QJsonObject>
#include <QString>

#include <vector>

namespace mirrorfly
{
    enum class ArgumentKind
    {
        String,
        Url,
        Integer,
        Boolean,
        Number,
        Object,
        Value
    };

    enum class ActionEffect
    {
        Read,
        Session,
        Document,
        Navigation,
        Create,
        Save,
        Export
    };

    struct ActionSpec
    {
        const char* module;
        const char* action;
        const char* method;
        std::vector<ArgumentKind> arguments;
        ActionEffect effect;
        bool ai_permitted = false;
        bool pending = false;
        bool ui_action = false;

        bool mutating() const
        {
            return effect != ActionEffect::Read;
        }
    };

    const std::vector<ActionSpec>& action_specs();
    const ActionSpec* office_action_spec(const QString& module, const QString& action);
    QString office_action_effect_name(ActionEffect effect);
    QJsonObject office_action_metadata(const ActionSpec& spec);
    QJsonObject make_office_ai_contract(const QJsonObject& catalog, const QJsonObject& schemas);
}
