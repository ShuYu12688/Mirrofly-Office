#include "office_ai_file_policy.hpp"
#include "automation_contract.hpp"
#include "document_path.hpp"
#include "office_ai_request.hpp"

#include <QRegularExpression>

namespace
{
    QString local_path(const QString& value)
    {
        const QUrl url(value);
        return url.isLocalFile() ? url.toLocalFile() : value;
    }

    bool contains_path(const QStringList& paths, const QString& path)
    {
        for (const auto& candidate : paths)
            if (mirrorfly::same_document_path(candidate, path))
                return true;
        return false;
    }
}

namespace mirrorfly
{
    void OfficeAiFilePolicy::beginTask(const QJsonObject& runtime)
    {
        QStringList active_copies;
        const auto modules = runtime.value("modules").toObject();
        for (auto it = modules.begin(); it != modules.end(); ++it)
        {
            const auto document = it.value().toObject();
            if (!document.value("active").toBool())
                continue;
            const auto path = local_path(document.value("documentPath").toString());
            if (contains_path(copies_, path) && QFileInfo(path).isFile() &&
                !contains_path(active_copies, path))
                active_copies.append(path);
        }
        copies_ = active_copies;
        originals_.clear();
        preserve_original_ = false;
        save_forbidden_ = false;
    }

    void OfficeAiFilePolicy::update(const QString& request, const QJsonObject& runtime)
    {
        const auto constraints = office_ai_task_constraints(request);
        preserve_original_ = preserve_original_ || constraints.preserve_original;
        if (constraints.save != OfficeAiTaskConstraints::Save::Unspecified)
            save_forbidden_ = constraints.save == OfficeAiTaskConstraints::Save::Forbidden;
        const bool protect_current = constraints.preserve_current;
        const auto modules = runtime.value("modules").toObject();
        for (auto it = modules.begin(); it != modules.end(); ++it)
        {
            const auto document = it.value().toObject();
            if (!document.value("active").toBool())
                continue;
            const auto path = local_path(document.value("documentPath").toString());
            if (protect_current && !path.isEmpty() && QFileInfo(path).isFile() &&
                !contains_path(originals_, path))
                originals_.append(path);
            opened(path);
            opened(document.value("saveUrl").toString());
        }
    }

    void OfficeAiFilePolicy::opened(const QString& value)
    {
        const auto path = local_path(value);
        if (!path.isEmpty() && QFileInfo(path).isFile() && !contains_path(copies_, path) &&
            !contains_path(originals_, path))
            originals_.append(path);
    }

    void OfficeAiFilePolicy::saved(const QString& value)
    {
        const auto path = local_path(value);
        if (!path.isEmpty() && QFileInfo(path).isFile() && !contains_path(originals_, path) &&
            !contains_path(copies_, path))
            copies_.append(path);
    }

    QJsonObject OfficeAiFilePolicy::check(const QString& module, const QString& action,
        const QJsonArray& arguments, const QJsonObject& document) const
    {
        const auto* spec = office_action_spec(module, action);
        if (save_forbidden_ && spec &&
            (spec->effect == ActionEffect::Save || spec->effect == ActionEffect::Export))
            return {{"ok", false}, {"error", "save_forbidden_by_user"},
                {"hint", "The direct human request forbids saving. No file write was executed."}};
        QString destination;
        if (action == "save")
        {
            destination = document.value("documentPath").toString();
            if (destination.isEmpty())
                destination = document.value("saveUrl").toString();
        }
        else if ((action == "saveTo" || action == "createEditableCopyTo") && !arguments.isEmpty())
            destination = arguments.first().toString();
        else if (module == "export" && action == "start" && arguments.size() >= 2)
            destination = arguments.at(1).toString();
        destination = local_path(destination);
        if (preserve_original_ && !destination.isEmpty() && contains_path(originals_, destination))
            return {{"ok", false}, {"error", "source_file_protected"}, {"path", destination},
                {"hint",
                    "The user requested an intact original. This write did not run. Use saveTo "
                    "with a complete distinct new file path, or office_save(title) for a new copy. "
                    "current=true writes the source and is not allowed."}};
        return {{"ok", true}};
    }
}
