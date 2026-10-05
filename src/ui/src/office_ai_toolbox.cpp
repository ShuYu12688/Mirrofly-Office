#include "office_ai_toolbox.hpp"
#include "office_ai_contract_adapter.hpp"
#include "office_ai_layout_catalog.hpp"
#include "office_ai_style.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/office_ai.hpp>

#include <QJsonDocument>

namespace
{
    QJsonObject read_runtime()
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(mirrorfly::office_runtime_snapshot()))
            .object();
    }
}

namespace mirrorfly
{
    OfficeAiToolbox::OfficeAiToolbox(const QString& desktop_directory, QObject* parent)
        : QObject(parent), desktop_directory_(desktop_directory)
    {
        sequence_ = std::make_unique<OfficeAiSequence>(read_runtime, [this](const QJsonObject& action)
        {
            return executeAction(action);
        }, this);
    }

    void OfficeAiToolbox::reset()
    {
        cancel();
        loaded_groups_.clear();
        image_library_.reset();
        const auto runtime = read_runtime();
        file_policy_.beginTask(runtime);
        page_batches_.clear();
        page_theme_ = {};
        deck_document_session_.clear();
        deck_target_pages_ = 0;
        requested_slide_pages_ = 0;
        document_key_ = office_ai_document_key(runtime);
        last_style_edit_.clear();
        last_style_generation_.clear();
        repeated_style_edits_ = 0;
        repeated_edit_response_ = 0;
        observation_required_ = false;
    }

    void OfficeAiToolbox::updateTaskRequest(const QString& request)
    {
        file_policy_.update(request, read_runtime());
    }

    void OfficeAiToolbox::focusWorkspace()
    {
        const auto runtime = read_runtime();
        QString module = office_ai_workspace(runtime).value("currentModule").toString();
        if (module == "markdown")
            module = "text";
        if (QStringList{"text", "word", "sheets", "slides", "mindmap", "pdf"}.contains(module))
        {
            const auto document = runtime.value("modules").toObject().value(module).toObject();
            const bool fresh = QStringList{"word", "sheets", "slides"}.contains(module) &&
                document.value("active").toBool() && document.value("documentPath").toString().isEmpty() &&
                !document.value("readOnly").toBool() &&
                (module != "slides" || document.value("editable").toBool());
            if (loaded_groups_.contains(module))
            {
                if (fresh)
                    loaded_groups_.insert("compose");
                return;
            }
            loaded_groups_ = {module};
            if (fresh)
                loaded_groups_.insert("compose");
        }
        else
            loaded_groups_.clear();
        emit focused(module);
    }

    void OfficeAiToolbox::setRequestedSlidePages(int pages)
    {
        if (pages == requested_slide_pages_)
            return;
        requested_slide_pages_ = pages;
        if (pages <= 0 || deck_document_session_.isEmpty())
            return;
        const auto slides = read_runtime().value("modules").toObject().value("slides").toObject();
        if (!slides.value("active").toBool() || slides.value("documentSession") != deck_document_session_)
            return;
        deck_target_pages_ = pages;
        // Only a direct human update calls this setter. Preserve all executed steps and batch IDs;
        // their cached goal receipts describe the old target, not completion of the new objective.
        for (auto it = page_batches_.begin(); it != page_batches_.end(); ++it)
        {
            auto plan = it.value();
            if (plan.value("session") != deck_document_session_)
                continue;
            plan.insert("targetPages", pages);
            auto result = plan.value("result").toObject();
            if (!result.isEmpty())
            {
                result.insert("historicalReceipt", true);
                result.insert("complete", false);
                result.insert("humanTargetPages", pages);
                result.insert("next",
                    "The human revised the total page count. Read current pages and "
                    "continue the new target; do not replay this applied batch.");
                auto progress = result.value("deckProgress").toObject();
                progress.insert("complete", false);
                result.insert("deckProgress", progress);
                plan.insert("result", result);
            }
            if (!plan.value("complete").toBool())
                for (const auto& receipt : plan.value("receipts").toArray())
                    if (receipt.toObject().value("page").toInt() >= pages)
                        plan.insert("blocked", true);
            it.value() = plan;
        }
    }

    void OfficeAiToolbox::cancel()
    {
        ++page_preflight_epoch_;
        image_library_.cancel();
        if (!active_page_batch_.isEmpty() && sequence_->active())
            recordPageBatch(active_page_batch_, sequence_->checkpoint());
        active_page_batch_.clear();
        sequence_->cancel();
        expected_document_change_ = false;
    }

    void OfficeAiToolbox::beginResponse(const QString& revision, int number)
    {
        response_revision_ = revision;
        chained_revision_ = revision;
        turns_ = number;
        focusWorkspace();
    }

    void OfficeAiToolbox::requireObservation()
    {
        observation_required_ = true;
    }

    bool OfficeAiToolbox::checkWorkspace() const
    {
        const auto state = read_runtime();
        return office_ai_workspace(state).value("ok").toBool() &&
            (document_key_.isEmpty() || office_ai_document_key(state) == document_key_ ||
                expected_document_change_);
    }

    bool OfficeAiToolbox::stagnantEdits() const
    {
        return repeated_style_edits_ >= 3 && repeated_edit_response_ == turns_;
    }

    QString OfficeAiToolbox::documentKey() const
    {
        return document_key_;
    }

    const QSet<QString>& OfficeAiToolbox::groups() const
    {
        return loaded_groups_;
    }

    QJsonObject OfficeAiToolbox::query(const QString& name, const QJsonObject& arguments)
    {
        if (name == "office_workspace")
            return office_ai_workspace(
                QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object());
        if (name == "office_load_group")
        {
            const QString group = arguments.value("group").toString();
            auto result = office_ai_guide(group);
            if (!result.value("ok").toBool())
                return result;
            QString active = office_ai_workspace(read_runtime()).value("currentModule").toString();
            if (active == "markdown")
                active = "text";
            const bool auxiliary =
                (group == "compose" && QStringList{"word", "sheets", "slides"}.contains(active)) ||
                ((group == "images" || group == "media") && active == "slides") ||
                (group == "export" &&
                    QStringList{"word", "sheets", "slides", "mindmap", "text"}.contains(active));
            if (group != active && !auxiliary)
                return {{"ok", true}, {"available", false}, {"currentModule", active},
                    {"hint",
                        "Enter the document with office_new or office_open first; its tools and guide load "
                        "there."}};
            loaded_groups_.insert(group);
            if (group == "compose")
                result.insert("guide", office_ai_compose_guide(active));
            if (group == "media" || group == "compose")
                return result;
            if (!auxiliary)
            {
                loaded_groups_.insert("actions");
                result.remove("guide");
            }
            QJsonArray names;
            const auto catalog =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_action_catalog())).object();
            for (const auto& value : capabilitiesFor(group, catalog).value("actions").toArray())
                names.append(value.toObject().value("name"));
            result.insert("actions", names);
            result.insert("signatureLookup",
                "office_schema(module,name) returns one signature; empty name lists edits.");
            if (auxiliary)
            {
                const auto contract =
                    QJsonDocument::fromJson(QByteArray::fromStdString(office_ai_contract())).object();
                result.insert("semantics", contract.value("semantics").toObject().value(group));
            }
            return result;
        }
        if (name == "office_layout")
        {
            if (!loaded_groups_.contains("slides"))
                return {{"ok", false}, {"error", "group_not_loaded"}, {"requiredGroup", "slides"}};
            return office_ai_layout_catalog(arguments.value("name").toString());
        }
        if (name == "office_style")
        {
            if (!loaded_groups_.contains("slides"))
                return {{"ok", false}, {"error", "group_not_loaded"}, {"requiredGroup", "slides"}};
            for (const auto& key : arguments.keys())
                if (key != "styleId")
                    return {{"ok", false}, {"error", "invalid_style_field"}, {"field", key}};
            if (arguments.contains("styleId") && !arguments.value("styleId").isString())
                return {{"ok", false}, {"error", "invalid_style"}};
            return office_ai_style_catalog(arguments.value("styleId").toString());
        }
        const QString module = arguments.value("module").toString();
        if (module != "app" && !loaded_groups_.contains(module))
            return {{"ok", false}, {"error", "group_not_loaded"}};
        if (name == "office_state")
            return stateFor(module, arguments.value("details").toBool());
        if (name == "office_schema")
        {
            loaded_groups_.insert("actions");
            return schemaFor(module, arguments.value("name").toString());
        }
        if (name == "office_read")
        {
            if (!office_ai_permitted(module, "readContent"))
                return {{"ok", false}, {"error", "unsupported_module"}};
            if ((module == "slides" || module == "mindmap") && arguments.value("view") == "text" &&
                arguments.value("id").toString().isEmpty())
                return {{"ok", false}, {"error", "object_id_required"},
                    {"hint",
                        "Use view=content to list objects/nodes and their ids, then view=text with an "
                        "observed id."}};
            auto query = arguments;
            query.remove("module");
            const auto runtime = read_runtime();
            const QJsonObject request{{"module", module}, {"action", "readContent"},
                {"args", QJsonArray{query}}, {"expectedRevision", runtime.value("revision")}};
            const auto encoded = QJsonDocument(request).toJson(QJsonDocument::Compact).toStdString();
            const auto response =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_execute(encoded))).object();
            if (!response.value("ok").toBool())
                return response;
            auto result = response.value("result").toObject();
            result.insert("revision", response.value("revision"));
            result.insert("documentSession",
                runtime.value("modules").toObject().value(module).toObject().value("documentSession"));
            result.insert("module", module);
            if (result.value("ok").toBool() && office_ai_document_key(runtime).startsWith(module + ':'))
                observation_required_ = false;
            return result;
        }
        if (name == "office_action")
            return executeAction(arguments);
        return {{"ok", false}, {"error", "unknown_tool"}};
    }

    void OfficeAiToolbox::execute(
        const QString& name, const QJsonObject& input, OfficeAiSequence::Completion completion)
    {
        if (name == "office_image_search" || name == "office_image_fetch")
        {
            if (!loaded_groups_.contains("slides"))
            {
                completion({{"ok", false}, {"error", "group_not_loaded"}, {"requiredGroup", "slides"}});
                return;
            }
            if (name == "office_image_search")
            {
                if (input.size() != 1 || !input.value("query").isString())
                {
                    completion({{"ok", false}, {"error", "invalid_image_query"}});
                    return;
                }
                image_library_.search(input.value("query").toString(), std::move(completion));
            }
            else
            {
                if (input.size() != 1 || !input.value("id").isString())
                {
                    completion({{"ok", false}, {"error", "unknown_image"}});
                    return;
                }
                image_library_.fetch(input.value("id").toString(), std::move(completion));
            }
            return;
        }
        if (observation_required_)
        {
            completion({{"ok", false}, {"error", "resume_observation_required"},
                {"hint", "The document changed. Use office_read on the current module before editing."}});
            return;
        }
        auto arguments = input;
        if (name == "office_new" && arguments.value("kind") == "text")
            arguments.insert("kind", "writer");
        if (!arguments.contains("expectedRevision"))
            arguments.insert("expectedRevision", response_revision_);
        if (name == "office_compose_slides" || name == "office_continue_slides")
        {
            executePages(name, arguments, std::move(completion));
            return;
        }
        workflow_receipt_ = {};
        const bool structured = name == "office_compose_word" || name == "office_compose_table";
        const bool workflow = structured || name == "office_save" || name == "office_open" ||
            name == "office_home" || name == "office_compose_slide" || name == "office_editable_copy";
        if (workflow)
        {
            const auto prepared =
                structured ? prepareStructured(name, arguments) : prepareWorkflow(name, arguments);
            if (!prepared.value("ok").toBool())
            {
                completion(prepared);
                return;
            }
            workflow_receipt_ = prepared.value("receipt").toObject();
            arguments = {{"steps", prepared.value("steps")},
                {"expectedRevision", arguments.value("expectedRevision")}};
        }
        if (name == "office_new")
        {
            arguments = {{"module", "app"}, {"action", "new"}, {"args", QJsonArray{arguments.value("kind")}},
                {"expectedRevision", arguments.value("expectedRevision")}};
        }
        if ((name == "office_batch" && !arguments.value("steps").isArray()) ||
            !arguments.value("expectedRevision").isString())
        {
            completion({{"ok", false}, {"error", "invalid_batch"}});
            return;
        }
        auto single_step = arguments;
        single_step.remove("expectedRevision");
        QJsonArray steps{single_step};
        if (name == "office_batch" || workflow)
            steps = arguments.value("steps").toArray();
        const auto catalog =
            QJsonDocument::fromJson(QByteArray::fromStdString(office_action_catalog())).object();
        const auto runtime =
            QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
        const auto slides = runtime.value("modules").toObject().value("slides").toObject();
        int page = slides.value("currentSlide").toInt(-1);
        if (steps.isEmpty() || steps.size() > 256)
        {
            completion({{"ok", false}, {"error", "invalid_batch_size"}, {"maximum", 256}});
            return;
        }
        QJsonArray normalized;
        for (const auto& value : steps)
        {
            auto step = value.toObject();
            if (step.contains("op"))
            {
                QString op = step.take("op").toString();
                if (!op.contains('.') && !step.contains("module") && !step.contains("action"))
                {
                    QString current = office_ai_workspace(runtime).value("currentModule").toString();
                    if (current == "markdown")
                        current = "text";
                    if (loaded_groups_.contains(current) && office_ai_permitted(current, op))
                        op = current + '.' + op;
                }
                const int dot = op.indexOf('.');
                if (dot <= 0 || step.contains("module") || step.contains("action"))
                {
                    completion({{"ok", false}, {"error", "invalid_operation"}, {"executed", 0},
                        {"hint",
                            "Use op=module.action from office_schema, e.g. text.replaceContent, with args "
                            "beside op."}});
                    return;
                }
                step.insert("module", op.left(dot));
                step.insert("action", op.mid(dot + 1));
            }
            if (step.size() != 3)
            {
                completion({{"ok", false}, {"error", "invalid_step_fields"}, {"executed", 0}});
                return;
            }
            const QString module = step.value("module").toString();
            const QString action = step.value("action").toString();
            auto signature = office_ai_action_signature(catalog, module, action);
            if (!signature.value("ok").toBool() && step.value("args").isObject() &&
                QStringList{"slides", "mindmap", "pdf"}.contains(module))
            {
                const auto schema = schemaFor(module, action);
                if (schema.value("invocation").isObject())
                {
                    const QString wrapper = module == "slides" ? "applyEdit" : "execute";
                    step.insert("action", wrapper);
                    step.insert("args", QJsonArray{action, step.value("args")});
                    signature = office_ai_action_signature(catalog, module, wrapper);
                }
            }
            const auto result = office_ai_normalize_action(step, signature, page);
            if (!result.value("ok").toBool())
            {
                auto failure = result;
                failure.insert("stepIndex", normalized.size());
                failure.insert("executed", 0);
                completion(failure);
                return;
            }
            normalized.append(result.value("step"));
            if (step.value("module") == "slides")
            {
                const auto args = result.value("step").toObject().value("args").toArray();
                if (step.value("action") == "setSlide")
                    page = args.first().toInt(-1);
                else if (step.value("action") != "semanticPage" && step.value("action") != "semanticTree")
                    page = -1; // A previous action may change selection; require an explicit query page.
            }
        }
        steps = normalized;
        for (const auto& value : steps)
        {
            const auto step = value.toObject();
            const QString module = step.value("module").toString();
            const QString action = step.value("action").toString();
            const auto args = step.value("args").toArray();
            const QString required_group = module == "app"
                ? (action == "open" ? office_ai_module_for_file(args.first().toString())
                                    : args.first().toString())
                : module;
            const QString group =
                required_group == "writer" || required_group == "markdown" ? "text" : required_group;
            if (!loaded_groups_.contains(group) && name != "office_home" && module != "app")
            {
                completion({{"ok", false}, {"error", "group_not_loaded"}, {"requiredGroup", group},
                    {"hint",
                        "Use office_load_group first. New documents: office_new(kind, "
                        "expectedRevision)."}});
                return;
            }
            if (!office_ai_permitted(module, action))
            {
                completion(
                    {{"ok", false}, {"error", "action_not_permitted"}, {"module", module}, {"action", action},
                        {"availableActions", capabilitiesFor(module, catalog).value("actions")},
                        {"hint", "Edit names belong in applyEdit(name,options), not action."}});
                return;
            }
        }
        QString revision = arguments.value("expectedRevision").toString();
        // Only chain revisions produced by our own synchronous calls in this model response.
        // Sequence still rejects any intervening external edit against that exact revision.
        if (revision == response_revision_)
            revision = chained_revision_;
        sequence_->start(steps, revision, [this, name, completion = std::move(completion)](QJsonObject result)
        {
            recordCompletion(result, name);
            if (result.value("ok").toBool())
            {
                chained_revision_ = result.value("revision").toString();
            }
            if (expected_document_change_)
            {
                document_key_ = office_ai_document_key(read_runtime());
                expected_document_change_ = false;
            }
            if ((name == "office_action" || name == "office_new") && result.value("ok").toBool())
            {
                auto single =
                    result.value("results").toArray().first().toObject().value("response").toObject();
                const auto executed = result.value("results").toArray().first().toObject();
                single.insert("resolvedOperation",
                    QJsonObject{{"module", executed.value("module")}, {"action", executed.value("action")}});
                single.insert("timing", result.value("timing"));
                single.insert("revision", result.value("revision"));
                single.insert("settled", result.value("settled"));
                if (result.contains("file"))
                    single.insert("file", result.value("file"));
                if (result.value("settled").toBool())
                {
                    auto detail = single.value("result").toObject();
                    if (detail.value("status").toString() == "pending")
                        detail.insert("status", "settled");
                    single.insert("result", detail);
                }
                completion(single);
            }
            else
                completion(result);
        });
    }
}
