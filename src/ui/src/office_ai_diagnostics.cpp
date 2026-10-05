#include "office_ai_agent.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/build_version.hpp>

#include <QDateTime>
#include <QJsonDocument>

namespace mirrorfly
{
    void OfficeAiAgent::diagnostic(const QString& event, const QJsonObject& fields)
    {
        // Callers supply only technical metadata, never arguments, document text or provider reasoning.
        auto record = fields;
        record.insert("at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        record.insert("turn", current_turn_);
        record.insert("event", event);
        record.insert("document", document_key_);
        record.insert("version", MIRRORFLY_VERSION_STRING);
        if (event == "request" || event == "tool" || event == "workspace_error")
        {
            const auto snapshot =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
            const auto workspace = office_ai_workspace(snapshot);
            const auto ui = snapshot.value("ui").toObject();
            const auto location = ui.value("state").toObject();
            QString module = workspace.value("currentModule").toString();
            if (module == "markdown")
                module = "text";
            const auto document = snapshot.value("modules").toObject().value(module).toObject();
            QJsonObject state{{"valid", workspace.value("ok")}, {"module", module},
                {"ready", ui.value("ready")}, {"pendingInput", location.value("pendingInput")},
                {"blockers", location.value("blockers")}};
            const QStringList state_fields{"active", "editable", "busy", "locked", "syncing", "pendingEdits",
                "documentSession", "editGeneration", "currentSlide", "slideCount", "modified"};
            for (const auto& key : state_fields)
                if (document.contains(key))
                    state.insert(key, document.value(key));
            record.insert("workspace", state);
        }
        log_.append(record);
    }
}
