#include "office_ai_agent.hpp"
#include "office_ai_request.hpp"
#include "office_ai_tools.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>

#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>

namespace
{
    bool active_document_unsaved(const QJsonObject& runtime)
    {
        QString module = mirrorfly::office_ai_workspace(runtime).value("currentModule").toString();
        if (module == "markdown")
            module = "text";
        if (!QStringList{"text", "word", "sheets", "slides", "mindmap", "pdf"}.contains(module))
            return false;
        const auto document = runtime.value("modules").toObject().value(module).toObject();
        if (!document.value("active").toBool())
            return false;
        QString path = document.value("documentPath").toString();
        if (path.isEmpty())
            path = QUrl(document.value("saveUrl").toString()).toLocalFile();
        return document.value("modified").toBool() || path.isEmpty() || !QFileInfo(path).isFile();
    }

    QString module_for_format(const QString& format)
    {
        if (format == "txt" || format == "md")
            return "text";
        if (format == "pptx")
            return "slides";
        if (format == "mfg")
            return "mindmap";
        if (format == "docx")
            return "word";
        if (format == "xlsx")
            return "sheets";
        return format;
    }

    QString format_for_kind(const QString& kind)
    {
        if (kind == "writer")
            return "txt";
        if (kind == "markdown")
            return "md";
        if (kind == "slides")
            return "pptx";
        if (kind == "mindmap")
            return "mfg";
        if (kind == "word")
            return "docx";
        if (kind == "sheets")
            return "xlsx";
        return kind;
    }

    QStringList saved_formats(const QJsonArray& receipts, QSet<QString>* paths = nullptr)
    {
        QStringList saved;
        for (const auto& value : receipts)
        {
            const auto receipt = value.toObject();
            const QFileInfo file(receipt.value("path").toString());
            if (!receipt.value("saved").toBool() || !file.isFile())
                continue;
            const QString format = file.suffix().toLower();
            if (receipt.value("module").toString() != module_for_format(format))
                continue;
            if (paths)
                paths->insert(file.absoluteFilePath().toCaseFolded());
            if (!saved.contains(format))
                saved.append(format);
        }
        return saved;
    }

    QStringList missing_saved_files(
        const QJsonArray& receipts, const QStringList& required_formats, int required_count)
    {
        QSet<QString> paths;
        const auto saved = saved_formats(receipts, &paths);
        QStringList missing;
        for (const auto& format : required_formats)
            if (!saved.contains(format))
                missing.append(format == "md" ? "markdown" : module_for_format(format));
        if (paths.size() < required_count)
            missing.append(QStringLiteral("files:%1/%2").arg(paths.size()).arg(required_count));
        return missing;
    }
}

namespace mirrorfly
{
    QJsonObject OfficeAiAgent::checkNewDeliverable(const QString& kind) const
    {
        if (constraints_.save != OfficeAiTaskConstraints::Save::Required)
            return {{"ok", true}};
        const auto required = constraints_.save_formats;
        const int requested_count = constraints_.file_count;
        if (required.size() < 2 || requested_count > required.size())
            return {{"ok", true}};
        const QString format = format_for_kind(kind);
        if (!required.contains(format))
            return {{"ok", true}};
        const auto saved = saved_formats(context_.savedFiles());
        if (!saved.contains(format))
            return {{"ok", true}};
        QStringList remaining;
        QStringList remaining_formats;
        for (const auto& target : required)
            if (!saved.contains(target))
            {
                remaining_formats.append(target);
                remaining.append(target == "md" ? "markdown" : module_for_format(target));
            }
        if (remaining.isEmpty())
            return {{"ok", true}};
        const QString next_format = remaining_formats.front();
        const QString next_group = module_for_format(next_format);
        QString next_kind = next_group;
        if (next_format == "txt")
            next_kind = "writer";
        else if (next_format == "md")
            next_kind = "markdown";
        QString hint;
        if (next_format == "pdf")
        {
            hint = QStringLiteral("This format is already saved. Call ");
            hint += QStringLiteral("office_open on the existing PDF, then query office_schema.");
        }
        else
        {
            hint = QStringLiteral("This format is already saved. Next call office_new(kind=%2). ");
            hint += QStringLiteral("It loads group %1 and returns its guide. Edit and save that document; ");
            hint += QStringLiteral("do not create another copy of the saved format.");
            hint = hint.arg(next_group, next_kind);
        }
        return {{"ok", false}, {"error", "deliverable_already_saved"}, {"completedFormat", format},
            {"remainingModules", QJsonArray::fromStringList(remaining)}, {"nextGroup", next_group},
            {"nextKind", next_kind}, {"hint", hint}};
    }

    void OfficeAiAgent::completeTask(const OfficeAiModelReply& reply)
    {
        const auto current_runtime =
            QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
        context_.reconcileSlideTarget(current_runtime.value("modules").toObject().value("slides").toObject());
        if (context_.unfinished())
        {
            if (completion_checks_++ == 0)
            {
                context_.notice("A task checklist or verified slide target is still unfinished. "
                                "Finish missing verified pages or remaining work. If tool results already "
                                "prove all requested work complete, call office_task now with phase=done, "
                                "remaining=[], keep mode/goal/targets and list verified items in completed. "
                                "Do not repeat completed edits or saves just to close the checklist.");
                sendCompletion();
                return;
            }
            finish(QStringLiteral("任务清单或演示页数仍未完成，已保留进度，可继续。"), {}, true);
            return;
        }
        if (constraints_.document_work && !document_work_)
        {
            if (completion_checks_++ == 0)
            {
                context_.notice("The user explicitly requested document work, but no document creation "
                                "or accepted edit has been verified in this task. Text and office_task "
                                "alone are not execution evidence. Perform the requested work with "
                                "the listed tools, then verify the result before finishing.");
                sendCompletion();
                return;
            }
            finish(QStringLiteral("尚未核验文档制作或编辑结果，已保留任务，可继续。"), {}, true);
            return;
        }
        if (constraints_.save == OfficeAiTaskConstraints::Save::Required)
        {
            const auto runtime =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
            QString module = office_ai_workspace(runtime).value("currentModule").toString();
            if (module == "markdown")
                module = "text";
            const auto required_formats = constraints_.save_formats;
            const int required_count = qMax(1, qMax(constraints_.file_count, required_formats.size()));
            bool current_required = false;
            for (const auto& format : required_formats)
                current_required = current_required || module_for_format(format) == module;
            if ((!saved_by_agent_ || saved_module_ == module || current_required) &&
                active_document_unsaved(runtime))
            {
                if (completion_checks_++ == 0)
                {
                    const auto document = runtime.value("modules").toObject().value(module).toObject();
                    const QString path = document.value("documentPath").toString();
                    const bool existing = !path.isEmpty() && QFileInfo(path).isFile();
                    if (existing)
                        context_.notice(
                            "The current document has edits still unsaved. Earlier file.saved only "
                            "proved the previous disk version. Call office_save(current=true) now, "
                            "wait for file.saved, then report. Do not create another file.");
                    else
                        context_.notice(
                            "The current document is still unsaved. Call office_save with a "
                            "descriptive title, wait for file.saved and path, then report. Do not "
                            "call dialog-only save on an unnamed document.");
                    sendCompletion();
                    return;
                }
                finish(
                    QStringLiteral("用户要求保存文件，但当前文档尚未保存；已保留任务，可继续。"), {}, true);
                return;
            }
            if (context_.savedFiles().isEmpty())
            {
                if (completion_checks_++ == 0)
                {
                    context_.notice("The user requested a saved file, but this task has no verified "
                                    "file.saved receipt. An existing file or a claimed path is not proof "
                                    "that this task saved it. Use office_save, confirm file.saved and "
                                    "path, then finish. Do not overwrite a protected source.");
                    sendCompletion();
                    return;
                }
                finish(
                    QStringLiteral("用户要求保存文件，但尚未取得保存凭据；已保留任务，可继续。"), {}, true);
                return;
            }
            const auto missing = missing_saved_files(context_.savedFiles(), required_formats, required_count);
            if (!missing.isEmpty())
            {
                if (completion_checks_++ == 0)
                {
                    context_.notice(
                        "The request needs saved deliverables in the requested formats, but verified "
                        "file receipts are missing: " +
                        missing.join(", ") +
                        ". Create and save each remaining document with office_save, then "
                        "finish with every saved path. A task checklist alone is not proof.");
                    sendCompletion();
                    return;
                }
                finish(QStringLiteral("交付文件的格式或数量尚未验证完整；已保留任务，可继续。"), {}, true);
                return;
            }
        }
        QString message = QStringLiteral("本轮完成。");
        if (reply.text.trimmed().isEmpty())
            message = QStringLiteral("模型没有返回文字结果。");
        finish(message, reply.text.trimmed(), false, "completed");
        return;
    }
}
