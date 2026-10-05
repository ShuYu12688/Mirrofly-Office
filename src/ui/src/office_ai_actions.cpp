#include "document_path.hpp"
#include "office_ai_context.hpp"
#include "office_ai_contract_adapter.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/office_ai.hpp>

#include <QFileInfo>
#include <QJsonDocument>
#include <QUrl>

namespace
{
    QJsonObject parse_object(const std::string& json)
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
    }

    QByteArray compact(const QJsonObject& object)
    {
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    QJsonObject failure(const QString& error)
    {
        return {{"ok", false}, {"error", error}};
    }
}

namespace mirrorfly
{
    QJsonObject OfficeAiToolbox::stateFor(const QString& module, bool details) const
    {
        if (!QStringList{"app", "text", "word", "slides", "sheets", "mindmap", "pdf", "export", "images"}
                .contains(module))
            return failure(QStringLiteral("unsupported_module"));
        const auto snapshot = parse_object(office_runtime_snapshot());
        if (!snapshot.value("ok").toBool())
            return snapshot;
        const auto state = snapshot.value("modules").toObject().value(module).toObject();
        QJsonObject summary;
        const QStringList fields{"active", "registered", "documentSession", "documentName", "documentPath",
            "modified", "editable", "readOnly", "locked", "busy", "currentSlide", "slideCount", "slideWidth",
            "slideHeight", "editGeneration", "pendingEdits", "syncing", "currentSheet", "sheetNames", "error",
            "message", "saveUrl", "selectedId", "currentPage", "generation"};
        for (const auto& key : fields)
            if (state.contains(key))
                summary.insert(key, state.value(key));
        QJsonObject result{{"ok", true}, {"revision", snapshot.value("revision")}, {"module", module},
            {"state", summary}, {"detailAvailable", office_ai_permitted(module, "readContent")},
            {"hint", "office_read(module,view=overview) lists bounded content views."}};
        if (details && office_ai_permitted(module, "readContent"))
        {
            const QJsonObject request{{"module", module}, {"action", "readContent"},
                {"args", QJsonArray{QJsonObject{}}}, {"expectedRevision", snapshot.value("revision")}};
            result.insert(
                "content", parse_object(office_execute(compact(request).toStdString())).value("result"));
        }
        return result;
    }

    QJsonObject OfficeAiToolbox::executeAction(const QJsonObject& arguments)
    {
        QElapsedTimer preparation;
        preparation.start();
        qint64 schema_ms = 0;
        if (!checkWorkspace())
            return failure("workspace_changed");
        const QString module = arguments.value("module").toString();
        QString action = arguments.value("action").toString();
        const bool navigation = action == "showHome" || action == "requestHome";
        if ((module != "app" && !loaded_groups_.contains(module) && !navigation) ||
            !office_ai_permitted(module, action))
            return failure(QStringLiteral("action_not_permitted"));
        if (!arguments.value("args").isArray() || !arguments.value("expectedRevision").isString())
            return failure(QStringLiteral("invalid_arguments"));
        if (module == "app" && action == "new")
        {
            const auto args = arguments.value("args").toArray();
            if (args.size() != 1 ||
                !QStringList{"writer", "markdown", "word", "slides", "sheets", "mindmap"}.contains(
                    args.first().toString()))
                return failure(QStringLiteral("unsupported_document_kind"));
        }
        const auto before = parse_object(office_runtime_snapshot());
        const auto document = before.value("modules").toObject().value(module).toObject();
        const auto file_permission =
            file_policy_.check(module, action, arguments.value("args").toArray(), document);
        if (!file_permission.value("ok").toBool())
            return file_permission;
        auto args = arguments.value("args").toArray();
        bool current_save = false;
        if (action == "saveTo" && args.size() == 1 && office_ai_permitted(module, "save"))
        {
            const auto value = args.first().toString();
            const QUrl url(value);
            const QString destination = url.isLocalFile() ? url.toLocalFile() : value;
            const QString path = document.value("documentPath").toString();
            if (QFileInfo(destination).isAbsolute() && same_document_path(path, destination))
            {
                // Preserve the requested destination's file policy; execute the shared public save
                // only after the current-file guards below. Never overwrite a different existing file.
                action = "save";
                args = {};
                current_save = true;
            }
        }
        if (action == "save")
        {
            const QString path = document.value("documentPath").toString();
            const QString destination = QUrl(document.value("saveUrl").toString()).toLocalFile();
            if (path.isEmpty() || !QFileInfo(path).isFile() || !same_document_path(path, destination) ||
                document.value("readOnly").toBool() ||
                (module == "slides" && !document.value("editable").toBool()))
                return {{"ok", false}, {"error", "current_save_unavailable"},
                    {"hint",
                        "Use office_save(title) for a new file; imported Word/PPT need an editable copy."}};
        }
        if ((module == "word" || module == "slides") &&
            (action == "save" || action == "saveTo" || action == "applyEdit" || action == "format") &&
            (module == "word" ? document.value("readOnly").toBool() : !document.value("editable").toBool()))
            return {{"ok", false}, {"error", "editable_copy_required"},
                {"hint", "Call office_editable_copy before editing or saving this imported file."}};
        if (module == "app" && (action == "new" || action == "open"))
        {
            const auto documents = office_ai_workspace(before).value("documents").toObject();
            for (auto it = documents.begin(); it != documents.end(); ++it)
                if (it.value().toObject().value("modified").toBool())
                    return {{"ok", false}, {"error", "unsaved_changes"}, {"module", it.key()},
                        {"hint",
                            "Preserve the current document before creating the next one. Do not reopen or "
                            "discard it."}};
        }
        if (navigation && document.value("modified").toBool())
            return {{"ok", false}, {"error", "unsaved_changes"},
                {"hint", "Save the current file before returning home. Never discard user edits."}};
        if ((action == "applyEdit" || action == "execute") && !args.isEmpty())
        {
            QElapsedTimer schema_clock;
            schema_clock.start();
            const auto schema = validateEdit(module, args.first().toString(), before.value("revision"));
            schema_ms = schema_clock.elapsed();
            if (!schema.value("ok").toBool())
                return schema;
        }
        const bool absolute_style = module == "slides" && action == "applyEdit" && args.size() == 2 &&
            !args.at(1).toObject().isEmpty() &&
            QStringList{"formatText", "formatShape", "formatTextBox", "background"}.contains(
                args.first().toString());
        const QByteArray style_key = compact(QJsonObject{{"session", document.value("documentSession")},
            {"page", document.value("currentSlide")}, {"shape", document.value("selectedShape")},
            {"args", args}});
        if (absolute_style && style_key == last_style_edit_ &&
            document.value("editGeneration").toString() == last_style_generation_ &&
            before.value("revision") == arguments.value("expectedRevision"))
        {
            // No intervening document edit: reapplying this absolute patch has no useful effect.
            // Selection-only revision changes do not turn it into new content progress.
            ++repeated_style_edits_;
            repeated_edit_response_ = turns_;
            diagnostic("repeat_edit",
                {{"module", module}, {"action", args.first()}, {"generation", last_style_generation_}});
            emit notice("This exact style patch is already applied to this object. Verify once and "
                        "finish, or continue a different unfinished target. Do not replay it.");
            return {{"ok", true}, {"revision", before.value("revision")}, {"progress", "unchanged"},
                {"result", QJsonObject{{"status", "already_applied"}}}};
        }
        const QJsonObject request{{"version", 1}, {"module", module}, {"action", action}, {"args", args},
            {"expectedRevision", arguments.value("expectedRevision")}};
        QElapsedTimer clock;
        const auto preparation_ms = preparation.elapsed();
        clock.start();
        auto result = parse_object(office_execute(compact(request).toStdString()));
        if (current_save)
            result.insert("resolvedAction", "save");
        const auto detail = result.value("result").toObject();
        if (result.value("ok").toBool() && detail.contains("ok") && !detail.value("ok").toBool())
        {
            result.insert("ok", false);
            result.insert("error", detail.value("error"));
        }
        if (!result.value("ok").toBool())
        {
            const auto state =
                parse_object(office_runtime_snapshot()).value("modules").toObject().value(module).toObject();
            result.insert("detail", state.value("error").toString(state.value("message").toString()));
            result.insert("signature",
                office_ai_action_signature(parse_object(office_action_catalog()), module, action));
        }
        if (module == "app" && (action == "new" || action == "open") && result.value("ok").toBool())
        {
            expected_document_change_ = true;
            if (action == "open" && !args.isEmpty())
                file_policy_.opened(args.first().toString());
        }
        if (navigation && result.value("ok").toBool())
            expected_document_change_ = true;
        const auto after = parse_object(office_runtime_snapshot());
        const auto after_document = after.value("modules").toObject().value(module).toObject();
        const QJsonObject selected_fields{{"setSlide", "currentSlide"}, {"selectShape", "selectedShape"},
            {"selectSheet", "currentSheet"}, {"selectPage", "currentPage"}, {"selectNode", "selectedId"}};
        const auto selected_field = selected_fields.value(action).toString();
        if (result.value("ok").toBool() && !selected_field.isEmpty() && !args.isEmpty() &&
            after_document.value(selected_field) != args.first())
        {
            result.insert("ok", false);
            result.insert("error", "selection_not_applied");
            result.insert("hint", "Read current content and use an existing index or object ID.");
        }
        if (result.value("ok").toBool() && module == "sheets" && action == "selectCell" && args.size() >= 2)
        {
            const auto cell = after_document.value("cellInfo").toObject();
            if (cell.value("row") != args.at(0) || cell.value("column") != args.at(1))
            {
                result.insert("ok", false);
                result.insert("error", "selection_not_applied");
                result.insert("hint", "Use a valid row/column and a selection of at most 4096 cells.");
            }
        }
        QString progress = "query_or_session";
        if (module == "text" && action == "formatMarkdown" && result.value("ok").toBool())
        {
            const bool changed = document.value("revision") != after_document.value("revision");
            progress = changed ? "document_edit_accepted" : "unchanged";
            QJsonObject receipt{{"action", args.at(2)}, {"applied", true}, {"changed", changed},
                {"instruction",
                    "This edit is complete. Verify it; find only the next unfinished target. "
                    "Do not replay relative listIndent/listOutdent. Save when all targets are satisfied."}};
            const auto target = args.at(3).toObject().value("expectedText").toString();
            if (!target.isEmpty() && target.toUtf8().size() <= 1024)
                receipt.insert("targetSourceBeforeEdit", target);
            result.insert("editReceipt", receipt);
        }
        if (module == "slides" && document.value("editGeneration") != after_document.value("editGeneration"))
        {
            progress = "document_edit_accepted";
            repeated_style_edits_ = 0;
        }
        if (absolute_style && result.value("ok").toBool())
        {
            last_style_edit_ = style_key;
            last_style_generation_ = after_document.value("editGeneration").toString();
        }
        result.insert("progress", progress);
        diagnostic("action",
            {{"module", module}, {"action", action},
                {"edit", action == "applyEdit" && !args.isEmpty() ? args.first() : QJsonValue()},
                {"ok", result.value("ok")}, {"error", result.value("error")},
                {"revisionBefore", before.value("revision")}, {"revisionAfter", result.value("revision")},
                {"generationBefore", document.value("editGeneration")},
                {"generationAfter", after_document.value("editGeneration")}, {"progress", progress},
                {"elapsedMs", clock.elapsed()}, {"preparationMs", preparation_ms}, {"schemaMs", schema_ms}});
        emit actionFinished(module, action, args, result, clock.elapsed());
        return office_ai_compact_result(result);
    }

}
