#include "automation_contract.hpp"
#include "office_ai_tools.hpp"

namespace mirrorfly
{
    bool office_ai_permitted(const QString& module, const QString& action)
    {
        const auto* spec = office_action_spec(module, action);
        return spec && spec->ai_permitted;
    }

    OfficeAiToolTraits office_ai_tool_traits(const QString& name, const QJsonObject& arguments)
    {
        OfficeAiToolTraits traits;
        if (name == "office_action" || name == "office_batch")
        {
            const auto steps =
                name == "office_batch" ? arguments.value("steps").toArray() : QJsonArray{arguments};
            traits.known = !steps.isEmpty();
            traits.observation = traits.known;
            for (const auto& value : steps)
            {
                const auto step = value.toObject();
                const auto op = step.value("op").toString();
                const auto* spec = office_action_spec(step.value("module").toString(op.section('.', 0, 0)),
                    step.value("action").toString(op.section('.', 1)));
                if (!spec || !spec->ai_permitted)
                    return {false, false, false, true, false, false};
                traits.observation = traits.observation && spec->effect == ActionEffect::Read;
                traits.document_work = traits.document_work || spec->effect == ActionEffect::Document ||
                    spec->effect == ActionEffect::Create;
                traits.saving = traits.saving || spec->effect == ActionEffect::Save ||
                    spec->effect == ActionEffect::Export;
            }
            traits.independent = traits.observation;
            traits.asynchronous = true; // The sequence owns readiness and completion even for a read batch.
            return traits;
        }
        if (QStringList{"office_workspace", "office_groups", "office_load_group", "office_style",
                "office_layout", "office_schema", "office_read", "office_state"}
                .contains(name))
            return {true, true, true, false, false, false};
        if (name == "office_image_search" || name == "office_image_fetch")
            return {true, true, true, true, false, false};
        if (name == "office_task")
            return {true, false, false, false, false, false};
        if (name == "office_save" || name == "office_editable_copy")
            return {true, false, false, true, false, true};
        if (name == "office_new")
            return {true, false, false, true, true, false};
        if (name == "office_open" || name == "office_home")
            return {true, false, false, true, false, false};
        if (QStringList{"office_compose_slide", "office_compose_slides", "office_continue_slides",
                "office_compose_word", "office_compose_table"}
                .contains(name))
            return {true, false, false, true, true, false};
        return {};
    }
}
