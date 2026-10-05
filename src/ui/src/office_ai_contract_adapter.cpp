#include "office_ai_contract_adapter.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <cmath>
#include <limits>

namespace mirrorfly
{
    QJsonObject office_ai_action_signature(
        const QJsonObject& catalog, const QString& module, const QString& action)
    {
        for (const auto& value : catalog.value("modules").toObject().value(module).toArray())
        {
            const auto entry = value.toObject();
            if (entry.value("name") == action && entry.value("available").toBool())
                return {{"ok", true}, {"kind", "public_action"}, {"module", module}, {"action", action},
                    {"parameters", entry.value("parameters")}, {"completion", entry.value("completion")}};
        }
        return {{"ok", false}, {"error", "action_not_available"}, {"module", module}, {"action", action}};
    }

    QJsonObject office_ai_normalize_action(
        const QJsonObject& step, const QJsonObject& signature, int current_page)
    {
        if (!signature.value("ok").toBool())
            return signature;
        const auto parameters = signature.value("parameters").toArray();
        auto input = step.value("args");
        if (input.isString() && input.toString().size() <= 64 * 1024)
        {
            QJsonParseError error;
            const auto decoded = QJsonDocument::fromJson(input.toString().toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && (decoded.isObject() || decoded.isArray()))
                input = decoded.isObject() ? QJsonValue(decoded.object()) : QJsonValue(decoded.array());
        }
        // Accept one redundant office_action envelope only when its operation is identical.
        // Extra keys, a different operation or nested envelopes still fail the normal contract checks.
        const auto envelope = input.toObject();
        const auto inner = envelope.value("args");
        bool declares_op = false;
        bool declares_action = false;
        for (const auto& parameter : parameters)
        {
            declares_op = declares_op || parameter.toObject().value("name") == "op";
            declares_action = declares_action || parameter.toObject().value("name") == "action";
        }
        if (!declares_op && envelope.size() == 2 &&
            envelope.value("op") == step.value("module").toString() + '.' + step.value("action").toString() &&
            (inner.isObject() || inner.isArray()))
            input = inner;
        if (!declares_action && input.isObject() && input.toObject().value("action") == step.value("action"))
        {
            auto explicit_action = input.toObject();
            explicit_action.remove("action");
            input = explicit_action;
        }
        // The convenience save tool's explicit current=true has identical intent to public save().
        // Do not discard other keys, false values or arguments to any other operation.
        if (step.value("action") == "save" && parameters.isEmpty() &&
            input == QJsonValue(QJsonObject{{"current", true}}))
            input = QJsonObject{};
        // A sole path for saveTo unambiguously names its sole local-file destination.
        // Mixed names or additional options remain errors rather than being discarded.
        if (step.value("action") == "saveTo" && parameters.size() == 1 &&
            parameters.first().toObject().value("type") == "local_file" && input.isObject() &&
            input.toObject().size() == 1 && input.toObject().contains("path"))
            input = QJsonObject{
                {parameters.first().toObject().value("name").toString(), input.toObject().value("path")}};
        const auto named = input.toObject();
        auto positional = input.toArray();
        const bool read = step.value("module") == "slides" &&
            (step.value("action") == "semanticPage" || step.value("action") == "semanticTree");
        const auto fail = [&](const QString& field, const QString& expected) -> QJsonObject
        {
            return {{"ok", false}, {"error", "invalid_arguments"}, {"field", field}, {"expected", expected},
                {"signature", signature},
                {"hint", "Use args as a named object or an ordered array matching this signature."}};
        };
        if (!input.isArray() && !input.isObject())
            return fail("args", "object or array");
        if (input.isArray() && positional.size() > parameters.size())
            return fail("args", QStringLiteral("%1 parameters").arg(parameters.size()));
        for (auto it = named.begin(); it != named.end(); ++it)
        {
            bool known = false;
            for (const auto& parameter : parameters)
                known = known || parameter.toObject().value("name") == it.key();
            if (!known)
                return fail(it.key(), "a declared parameter name");
        }
        QJsonArray args;
        for (int index = 0; index < parameters.size(); ++index)
        {
            const auto parameter = parameters.at(index).toObject();
            const QString name = parameter.value("name").toString();
            const QString type = parameter.value("type").toString();
            QJsonValue value(QJsonValue::Undefined);
            if (input.isObject())
                value = named.value(name);
            else if (index < positional.size())
                value = positional.at(index);
            if (value.isUndefined() && parameter.contains("default"))
                value = parameter.value("default");
            if (value.isUndefined() && read)
            {
                if (name == "page" && current_page >= 0)
                    value = current_page;
                if (name == "offset")
                    value = 0;
                if (name == "limit")
                    value = 8;
            }
            if ((type == "integer" || type == "finite_number") && value.isString())
            {
                bool converted = false;
                const double number = value.toString().toDouble(&converted);
                if (converted && std::isfinite(number))
                    value = number;
            }
            bool valid = false;
            if (type == "string")
                valid = value.isString();
            if (type == "local_file")
            {
                const auto path = value.toString();
                QUrl url(path);
                if (url.scheme().isEmpty() || (path.size() > 2 && path[1] == ':'))
                    url = QUrl::fromLocalFile(path);
                valid = value.isString() && !path.isEmpty() && url.isValid() && url.isLocalFile();
            }
            if (type == "object")
                valid = value.isObject();
            if (type == "boolean")
                valid = value.isBool();
            if (type == "finite_number")
                valid = value.isDouble() && std::isfinite(value.toDouble());
            if (type == "integer")
                valid = value.isDouble() && std::isfinite(value.toDouble()) &&
                    std::floor(value.toDouble()) == value.toDouble() &&
                    value.toDouble() >= std::numeric_limits<int>::min() &&
                    value.toDouble() <= std::numeric_limits<int>::max();
            if (type == "json_value")
                valid = !value.isUndefined() && !value.isNull();
            if (!valid)
                return fail(name, type);
            if (read && name == "limit" && (value.toInt() < 1 || value.toInt() > 16))
                return fail(name, "integer 1..16; use nextOffset to paginate");
            args.append(value);
        }
        return {{"ok", true},
            {"step",
                QJsonObject{
                    {"module", step.value("module")}, {"action", step.value("action")}, {"args", args}}}};
    }
}
