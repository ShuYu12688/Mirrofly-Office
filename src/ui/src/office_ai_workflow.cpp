#include "office_ai_slide_style.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_workspace.hpp"
#include "spreadsheet_input.hpp"

#include <mirrorfly/presentation_geometry.hpp>

#include <mirrorfly/automation.hpp>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace
{
    QJsonObject state()
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(mirrorfly::office_runtime_snapshot()))
            .object();
    }

    QJsonObject error(const QString& code, const QString& hint)
    {
        return {{"ok", false}, {"error", code}, {"hint", hint}};
    }

    QJsonObject step(const QString& module, const QString& action, const QJsonArray& args = {})
    {
        return {{"module", module}, {"action", action}, {"args", args}};
    }
}

namespace mirrorfly
{
    QJsonObject OfficeAiToolbox::prepareWorkflow(const QString& name, const QJsonObject& arguments) const
    {
        const auto runtime = state();
        const auto workspace = office_ai_workspace(runtime);
        QString module = workspace.value("currentModule").toString();
        if (module == "markdown")
            module = "text";
        const auto document = runtime.value("modules").toObject().value(module).toObject();
        if (!workspace.value("ok").toBool())
            return error("ui_state_unavailable", "The runtime state contract is unavailable.");
        if (!arguments.value("expectedRevision").isString())
            return error("invalid_workflow", "Read the current workspace revision.");
        if (name == "office_home")
        {
            if (!QStringList{"slides", "text", "word", "sheets", "mindmap", "pdf"}.contains(module))
                return error(
                    "already_home", "Workspace is already home; create the next requested document.");
            if (document.value("modified").toBool())
                return error("unsaved_changes", "Save first; do not discard user changes.");
            const QString action = module == "slides" ? "showHome" : "requestHome";
            return {{"ok", true}, {"steps", QJsonArray{step(module, action)}},
                {"receipt", QJsonObject{{"operation", "home"}}}};
        }
        if (name == "office_save")
            return prepareSave(module, document, arguments);
        if (name == "office_editable_copy")
        {
            const bool copy_not_required =
                module == "word" ? !document.value("readOnly").toBool() : document.value("editable").toBool();
            if (!QStringList{"word", "slides"}.contains(module) || !document.value("active").toBool() ||
                copy_not_required)
                return error("editable_copy_not_required", "Open a read-only Word or PPT first.");
            const QString extension = module == "word" ? "docx" : "pptx";
            if (arguments.contains("destination"))
            {
                if (!arguments.value("destination").isString() || arguments.contains("title"))
                    return error("invalid_copy_destination", "Use destination or title, never both.");
                const QString value = arguments.value("destination").toString();
                const QUrl url(value);
                const QFileInfo destination(url.isLocalFile() ? url.toLocalFile() : value);
                if (!destination.isAbsolute() ||
                    destination.suffix().compare(extension, Qt::CaseInsensitive) ||
                    !destination.dir().exists() || destination.exists())
                    return error("invalid_copy_destination",
                        "Provide a new absolute file path with the current document extension in an existing "
                        "folder.");
                const QString path = destination.absoluteFilePath();
                return {{"ok", true},
                    {"steps",
                        QJsonArray{
                            step(module, "createEditableCopyTo", {QUrl::fromLocalFile(path).toString()})}},
                    {"receipt", QJsonObject{{"operation", "editable_copy"}, {"path", path}}}};
            }
            QString title = arguments.value("title").toString().trimmed();
            if (title.isEmpty())
                title = document.value("documentName").toString() + QStringLiteral("-编辑副本");
            if (title.endsWith('.' + extension, Qt::CaseInsensitive))
                title.chop(extension.size() + 1);
            title.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
            title = title.left(80);
            while (title.endsWith('.') || title.endsWith(' '))
                title.chop(1);
            if (title.isEmpty())
                return error("descriptive_title_required", "Give the editable copy a short title.");
            if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                    QRegularExpression::CaseInsensitiveOption)
                    .match(title)
                    .hasMatch())
                title.prepend('_');
            QString folder = desktop_directory_;
            if (folder.isEmpty())
                folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
            const QDir desktop(folder);
            if (!desktop.exists())
                return error("desktop_unavailable", "Use createEditableCopyTo with an explicit path.");
            QString destination = desktop.filePath(title + '.' + extension);
            int suffix = 2;
            while (QFileInfo::exists(destination))
                destination = desktop.filePath(title + QStringLiteral(" (%1).").arg(suffix++) + extension);
            return {{"ok", true},
                {"steps",
                    QJsonArray{
                        step(module, "createEditableCopyTo", {QUrl::fromLocalFile(destination).toString()})}},
                {"receipt", QJsonObject{{"operation", "editable_copy"}, {"path", destination}}}};
        }
        if (name == "office_open")
        {
            const QString path = arguments.value("path").toString();
            const QUrl url(path);
            const QFileInfo file(url.isLocalFile() ? url.toLocalFile() : path);
            const QString target = office_ai_module_for_file(path);
            if (!file.isAbsolute() || !file.isFile() || target.isEmpty())
                return error("invalid_document_path",
                    "Provide an existing absolute local path: txt, md, docx, xlsx, pptx, mfg or pdf.");
            return {{"ok", true},
                {"steps",
                    QJsonArray{
                        step("app", "open", {QUrl::fromLocalFile(file.absoluteFilePath()).toString()})}},
                {"receipt", QJsonObject{{"operation", "open"}, {"module", target}}}};
        }
        if (module != "slides" || !document.value("active").toBool() || !document.value("editable").toBool())
            return {{"ok", false}, {"error", "editable_slides_required"}, {"currentModule", module},
                {"active", document.value("active")}, {"editable", document.value("editable")},
                {"hint", "Read workspace and correct this precondition before generating slide content."}};
        const int page = arguments.value("page").toInt(-2);
        const auto elements = arguments.value("elements").toArray();
        if ((page != 0 && page != -1) || elements.isEmpty() || elements.size() > 200 ||
            !QRegularExpression("^#[0-9a-fA-F]{6}$")
                .match(arguments.value("background").toString())
                .hasMatch())
            return error(
                "invalid_page_recipe", "Provide page=0 or -1, background #RRGGBB and 1..200 elements.");
        QJsonArray steps;
        if (page == 0)
        {
            if (!document.value("documentPath").toString().isEmpty() ||
                document.value("editGeneration") != "0" || document.value("slideCount").toInt() != 1)
                return error(
                    "initial_page_already_edited", "Do not replay a completed page. Use page=-1 to append.");
            const auto snapshot =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot())).object();
            const auto detail = snapshot.value("modules").toObject().value("slides").toObject();
            const int count = detail.value("snapshot").toObject().value("totalObjects").toInt();
            for (int index = count - 1; index >= 0; --index)
            {
                steps.append(step("slides", "selectShape", {index}));
                steps.append(step("slides", "applyEdit", {"deleteShape", QJsonObject{}}));
            }
        }
        else
        {
            steps.append(step("slides", "setSlide", {document.value("slideCount").toInt() - 1}));
            steps.append(step("slides", "applyEdit", {"addSlide", QJsonObject{{"layout", "blank"}}}));
        }
        steps.append(step(
            "slides", "applyEdit", {"background", QJsonObject{{"color", arguments.value("background")}}}));
        QString title;
        for (int element_index = 0; element_index < elements.size(); ++element_index)
        {
            const auto value = elements.at(element_index);
            const auto element = value.toObject();
            const auto type = element.value("type").toString();
            if (type != "text" && type != "shape" && type != "image")
                return error("invalid_element", "Element type must be text, shape or image.");
            QJsonObject geometry;
            for (const auto* field : {"x", "y", "width", "height"})
            {
                const auto number = element.value(field);
                if (!number.isDouble() || !std::isfinite(number.toDouble()) ||
                    std::abs(number.toDouble()) > 20000)
                    return error("invalid_geometry", "Use finite point coordinates and dimensions.");
                geometry.insert(field, number);
            }
            if (geometry.value("width").toDouble() < 1 || geometry.value("height").toDouble() < 1)
                return error("invalid_geometry", "Width and height must be positive.");
            if (type == "image")
            {
                const auto path = element.value("path").toString();
                const QFileInfo file(path);
                if (!element.value("path").isString() || !file.isAbsolute() || !file.isFile() ||
                    file.isSymLink() || file.isJunction() || element.contains("style"))
                    return {{"ok", false}, {"error", "invalid_page_image"}, {"elementIndex", element_index},
                        {"hint", "Use an existing direct local image path without a style object."}};
                steps.append(step("slides", "addImage", {QUrl::fromLocalFile(path).toString()}));
                steps.append(step("slides", "applyEdit", {"transformShape", geometry}));
                continue;
            }
            if (element.contains("style") && !element.value("style").isObject())
                return error("invalid_style", "style must be an object.");
            const auto styles =
                office_ai_validate_slide_style(element.value("style").toObject(), type == "text");
            if (!styles.value("ok").toBool())
            {
                auto failure = styles;
                failure.insert("elementIndex", element_index);
                return failure;
            }
            const auto text_style = styles.value("text").toObject();
            const auto shape_style = styles.value("shape").toObject();
            if (type == "text")
            {
                if (!element.value("text").isString())
                    return error("missing_text", "Text elements need text.");
                geometry.insert("text", element.value("text"));
                if (title.isEmpty())
                    title = element.value("text").toString().left(100);
                steps.append(step("slides", "applyEdit", {"addText", geometry}));
            }
            else
            {
                const QString preset = element.value("geometry").toString("rect");
                const auto available = presentation_geometry_presets();
                if (std::find(available.begin(), available.end(), preset.toStdString()) == available.end())
                    return {{"ok", false}, {"error", "invalid_geometry"}, {"elementIndex", element_index},
                        {"hint",
                            "Use a preset in office_schema(slides,addShape). No page actions have "
                            "executed."}};
                geometry.insert("geometry", preset);
                steps.append(step("slides", "applyEdit", {"addShape", geometry}));
            }
            if (!text_style.isEmpty())
                steps.append(step("slides", "applyEdit", {"formatText", text_style}));
            if (!shape_style.isEmpty())
                steps.append(step("slides", "applyEdit", {"formatShape", shape_style}));
        }
        const int target = page == 0 ? 0 : document.value("slideCount").toInt();
        return {{"ok", true}, {"steps", steps},
            {"receipt",
                QJsonObject{{"operation", "compose"}, {"page", target}, {"title", title},
                    {"elementCount", elements.size()}}}};
    }

    void OfficeAiToolbox::recordCompletion(QJsonObject& result, const QString& tool)
    {
        if (!result.value("ok").toBool() || result.value("remaining").toInt() > 0)
        {
            if (tool == "office_compose_slide")
                result.insert("recovery",
                    "Some page actions may already have succeeded. Inspect only the "
                    "current page and finish remaining edits with office_batch; do not replay "
                    "compose_slide.");
            return;
        }
        const auto runtime = state();
        const auto slides = runtime.value("modules").toObject().value("slides").toObject();
        for (const auto& value : result.value("results").toArray())
        {
            const auto action = value.toObject();
            if (action.value("action") == "saveTo" || action.value("action") == "save" ||
                action.value("action") == "createEditableCopyTo")
            {
                const QString module = action.value("module").toString();
                const auto document = runtime.value("modules").toObject().value(module).toObject();
                const QString saved_url = QUrl(document.value("saveUrl").toString()).toLocalFile();
                const QString path = document.value("documentPath").toString(saved_url);
                file_policy_.saved(path);
                const QJsonObject receipt{{"saved", true}, {"path", path}, {"module", module},
                    {"name", document.value("documentName")},
                    {"generation", document.value("editGeneration")}};
                emit milestone("file:" + document.value("documentSession").toString(), receipt);
                result.insert("file", receipt);
            }
            if (action.value("action") == "showHome" || action.value("action") == "requestHome")
                focusWorkspace();
            if (action.value("module") == "app" &&
                (action.value("action") == "new" || action.value("action") == "open"))
            {
                page_theme_ = {};
                page_batches_.clear();
                deck_target_pages_ = 0;
                deck_document_session_.clear();
                loaded_groups_.clear();
                focusWorkspace();
            }
        }
        if (tool == "office_compose_slide")
        {
            auto receipt = workflow_receipt_;
            const auto snapshot =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot())).object();
            const auto current = snapshot.value("modules").toObject().value("slides").toObject();
            const int objects = current.value("snapshot").toObject().value("totalObjects").toInt(-1);
            if (current.value("currentSlide").toInt(-1) != receipt.value("page").toInt() ||
                objects != receipt.value("elementCount").toInt())
            {
                result.insert("ok", false);
                result.insert("error", "page_verification_mismatch");
                result.insert("hint", "Edits ran; inspect the current page instead of recreating it.");
                return;
            }
            receipt.insert("verifiedObjectCount", objects);
            receipt.insert("generation", slides.value("editGeneration"));
            receipt.insert("documentSession", slides.value("documentSession"));
            receipt.insert("status", "all_public_edits_completed");
            receipt.insert("verification", "Structure only; visual acceptance remains with the user.");
            emit milestone("presentation",
                QJsonObject{{"documentSession", slides.value("documentSession")},
                    {"slideCount", slides.value("slideCount")}, {"lastCompletedPage", receipt.value("page")},
                    {"generation", receipt.value("generation")}});
            result.remove("results");
            result.insert("pageReceipt", receipt);
            result.insert("next",
                "Page edits completed. Compose the next requested page or finish; do not recreate it.");
        }
        if (tool == "office_compose_word" || tool == "office_compose_table")
        {
            auto receipt = workflow_receipt_;
            const auto snapshot =
                QJsonDocument::fromJson(QByteArray::fromStdString(office_snapshot())).object();
            const QString module = tool == "office_compose_word" ? "word" : "sheets";
            const auto detail = snapshot.value("modules").toObject().value(module).toObject();
            const auto content = detail.value("snapshot").toObject();
            bool verified = false;
            if (module == "word")
            {
                verified = content.value("plainText") == receipt.value("expectedText") &&
                    content.value("paragraphs").toInt() == receipt.value("paragraphCount").toInt();
                receipt.remove("expectedText");
            }
            else
            {
                const auto cells = content.value("cells").toArray();
                const int expected = receipt.value("rowCount").toInt() * receipt.value("columnCount").toInt();
                const QString last = receipt.value("lastValue").toString();
                bool last_matches = false;
                if (!cells.isEmpty())
                {
                    const auto last_cell = cells.last().toObject();
                    if (last.startsWith('='))
                        last_matches = last_cell.value("formula").toString() == last.mid(1);
                    else
                    {
                        SpreadsheetValue normalized;
                        QString error;
                        last_matches = spreadsheet_input_value(last, "auto", normalized, error) &&
                            last_cell.value("text").toString() == QString::fromStdString(normalized.text);
                    }
                }
                verified = cells.size() == expected && !cells.isEmpty() &&
                    detail.value("sheetNames").toArray().first() == receipt.value("expectedTitle") &&
                    cells.first().toObject().value("text") == receipt.value("firstHeader") && last_matches;
                receipt.remove("firstHeader");
                receipt.remove("lastValue");
                receipt.remove("expectedTitle");
            }
            if (!verified)
            {
                result.insert("ok", false);
                result.insert("error", "document_verification_mismatch");
                result.insert("hint", "Edits ran; read the current document before correcting it.");
                return;
            }
            receipt.insert("status", "all_public_edits_completed");
            receipt.insert("documentSession", detail.value("documentSession"));
            result.remove("results");
            result.insert(module == "word" ? "wordReceipt" : "tableReceipt", receipt);
            result.insert("next", "The document content is complete. Save it; do not compose again.");
        }
        if (tool == "office_home")
            result.insert(
                "next", "At home. Create another document only if the user requested one; otherwise finish.");
    }
}
