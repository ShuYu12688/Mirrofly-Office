#include "automation_contract.hpp"

#include <mirrorfly/office_ai.hpp>

#include <QJsonArray>

namespace mirrorfly
{
    const std::vector<ActionSpec>& action_specs()
    {
        using A = ArgumentKind;
        static const std::vector<ActionSpec> specs = {
            {"text", "readContent", "readContent", {A::Object}, ActionEffect::Read, true, false},
            {"word", "readContent", "readContent", {A::Object}, ActionEffect::Read, true, false},
            {"sheets", "readContent", "readContent", {A::Object}, ActionEffect::Read, true, false},
            {"slides", "readContent", "readContent", {A::Object}, ActionEffect::Read, true, false},
            {"pdf", "readContent", "readContent", {A::Object}, ActionEffect::Read, true, false},
            {"mindmap", "readContent", "readContent", {A::Object}, ActionEffect::Read, true, false},
            {"text", "setZoom", "setZoom", {A::Number}, ActionEffect::Session, false, false},
            {"word", "setZoom", "setZoom", {A::Number}, ActionEffect::Session, false, false},
            {"sheets", "setZoom", "setZoom", {A::Number}, ActionEffect::Session, false, false},
            {"pdf", "setZoom", "setZoom", {A::Number}, ActionEffect::Session, false, false},
            {"mindmap", "setZoom", "setZoom", {A::Number}, ActionEffect::Session, false, false},

            {"export", "start", "start", {A::String, A::Url, A::Object}, ActionEffect::Export, true, true},
            {"export", "cancel", "cancel", {}, ActionEffect::Session, true, false},
            {"export", "clearMessage", "clearMessage", {}, ActionEffect::Session, false, false},
            {"export", "snapshot", "snapshot", {}, ActionEffect::Read, true, false},
            {"app", "new", "requestCreate", {A::String}, ActionEffect::Create, true, true},
            {"app", "open", "selectFile", {A::Url}, ActionEffect::Navigation, true, true},
            {"app", "inspectFile", "inspectFile", {A::String}, ActionEffect::Session, false, true},
            {"app", "toggleStar", "toggleStar", {A::String}, ActionEffect::Session, false, false},
            {"app", "chooseFile", "chooseFile", {}, ActionEffect::Session, false, true},
            {"app", "filterFiles", "filterFiles", {A::String, A::String}, ActionEffect::Read, false, false},
            {"app", "clearNotice", "clearNotice", {}, ActionEffect::Session, false, false},
            {"app", "replayLoading", "replayLoading", {}, ActionEffect::Session, false, true},
            {"app", "setHomeView", "automationHomeView", {A::String, A::String, A::String},
                ActionEffect::Session, false, false, true},
            {"text", "requestHome", "requestHome", {}, ActionEffect::Navigation, true, true},
            {"text", "requestWindowClose", "requestWindowClose", {}, ActionEffect::Session, false, false},
            {"text", "save", "save", {}, ActionEffect::Save, true, true},
            {"text", "saveAs", "saveAs", {}, ActionEffect::Save, false, true},
            {"text", "saveTo", "saveTo", {A::Url}, ActionEffect::Save, true, true},
            {"text", "selectSaveFile", "selectSaveFile", {A::Url}, ActionEffect::Save, false, true},
            {"text", "cancelSaveDialog", "cancelSaveDialog", {}, ActionEffect::Session, false, false},
            {"text", "resolveUnsaved", "resolveUnsaved", {A::String}, ActionEffect::Session, false, true},
            {"text", "replaceContent", "replaceContent", {A::String}, ActionEffect::Document, true, false},
            {"text", "formatMarkdown", "formatMarkdown", {A::Integer, A::Integer, A::String, A::Object},
                ActionEffect::Document, true, false},
            {"text", "clearMessage", "clearMessage", {}, ActionEffect::Session, false, false},
            {"word", "requestHome", "requestHome", {}, ActionEffect::Navigation, true, true},
            {"word", "requestWindowClose", "requestWindowClose", {}, ActionEffect::Session, false, false},
            {"word", "resolveUnsaved", "resolveUnsaved", {A::String}, ActionEffect::Session, false, true},
            {"word", "requestEditableCopy", "requestEditableCopy", {}, ActionEffect::Save, false, true},
            {"word", "createEditableCopyTo", "createEditableCopyTo", {A::Url}, ActionEffect::Save, true,
                true},
            {"word", "save", "save", {}, ActionEffect::Save, true, true},
            {"word", "saveAs", "saveAs", {}, ActionEffect::Save, false, true},
            {"word", "saveTo", "saveTo", {A::Url}, ActionEffect::Save, true, true},
            {"word", "selectSaveFile", "selectSaveFile", {A::Url}, ActionEffect::Save, false, true},
            {"word", "cancelSaveDialog", "cancelSaveDialog", {}, ActionEffect::Session, false, false},
            {"word", "clearMessage", "clearMessage", {}, ActionEffect::Session, false, false},
            {"word", "snapshot", "snapshot", {}, ActionEffect::Read, true, false},
            {"word", "inspect", "inspect", {A::Integer}, ActionEffect::Read, true, false},
            {"word", "editSchema", "editSchema", {}, ActionEffect::Read, false, false},
            {"word", "copyFormat", "copyFormat", {A::Integer}, ActionEffect::Session, false, false},
            {"word", "pasteFormat", "pasteFormat", {A::Integer, A::Integer}, ActionEffect::Document, false,
                false},
            {"word", "copySelection", "copySelection", {A::Integer, A::Integer, A::Boolean},
                ActionEffect::Session, false, false},
            {"word", "replaceAll", "replaceAll", {A::String, A::String}, ActionEffect::Document, false,
                false},
            {"word", "format", "format", {A::Integer, A::Integer, A::String, A::Value},
                ActionEffect::Document, true, false},
            {"word", "undo", "undo", {}, ActionEffect::Document, true, false},
            {"word", "redo", "redo", {}, ActionEffect::Document, true, false},
            {"word", "find", "find", {A::String, A::Integer, A::Boolean}, ActionEffect::Read, true, false},
            {"word", "replace", "replace", {A::Integer, A::Integer, A::String, A::String},
                ActionEffect::Document, false, false},
            {"word", "paste", "paste", {A::Integer, A::Integer}, ActionEffect::Document, false, false},
            {"word", "pastePlain", "pastePlain", {A::Integer, A::Integer}, ActionEffect::Document, false,
                false},
            {"word", "insertParagraph", "insertParagraph", {A::Integer, A::Integer}, ActionEffect::Document,
                true, false},
            {"word", "insertText", "insertText", {A::Integer, A::Integer, A::String}, ActionEffect::Document,
                true, false},
            {"word", "insertTemplate", "insertTemplate", {A::Integer, A::String}, ActionEffect::Document,
                true, false},
            {"sheets", "requestHome", "requestHome", {}, ActionEffect::Navigation, true, true},
            {"sheets", "requestWindowClose", "requestWindowClose", {}, ActionEffect::Session, false, false},
            {"sheets", "save", "save", {}, ActionEffect::Save, true, true},
            {"sheets", "saveAs", "saveAs", {}, ActionEffect::Save, false, true},
            {"sheets", "saveTo", "saveTo", {A::Url}, ActionEffect::Save, true, true},
            {"sheets", "selectSaveFile", "selectSaveFile", {A::Url}, ActionEffect::Save, false, true},
            {"sheets", "cancelSaveDialog", "cancelSaveDialog", {}, ActionEffect::Session, false, false},
            {"sheets", "resolveUnsaved", "resolveUnsaved", {A::String}, ActionEffect::Session, false, true},
            {"sheets", "clearError", "clearError", {}, ActionEffect::Session, false, false},
            {"sheets", "selectSheet", "selectSheet", {A::Integer}, ActionEffect::Session, true, false},
            {"sheets", "addSheet", "addSheet", {A::String}, ActionEffect::Document, true, false},
            {"sheets", "renameSheet", "renameSheet", {A::String}, ActionEffect::Document, true, false},
            {"sheets", "selectBand", "selectBand", {A::Integer, A::Integer, A::Boolean},
                ActionEffect::Session, false, false},
            {"sheets", "selectCell", "selectCell", {A::Integer, A::Integer, A::Boolean},
                ActionEffect::Session, true, false},
            {"sheets", "selectAddress", "selectAddress", {A::String}, ActionEffect::Session, true, false},
            {"sheets", "findCell", "findCell", {A::String, A::Boolean}, ActionEffect::Session, false, false},
            {"sheets", "copyCell", "copyCell", {}, ActionEffect::Session, false, false},
            {"sheets", "pasteCell", "pasteCell", {}, ActionEffect::Document, false, false},
            {"sheets", "selectionText", "selectionText", {}, ActionEffect::Read, true, false},
            {"sheets", "pasteText", "pasteText", {A::String}, ActionEffect::Document, true, false},
            {"sheets", "clearSelection", "clearSelection", {}, ActionEffect::Document, false, false},
            {"sheets", "setCellValue", "setCellValue", {A::Integer, A::Integer, A::String, A::String},
                ActionEffect::Document, true, false},
            {"sheets", "commitTextInput", "commitTextInput", {}, ActionEffect::Document, false, false},
            {"sheets", "undo", "undo", {}, ActionEffect::Document, true, false},
            {"sheets", "redo", "redo", {}, ActionEffect::Document, true, false},
            {"sheets", "startTool", "startTool", {A::String, A::Object}, ActionEffect::Document, true, false},
            {"sheets", "formatSelection", "formatSelection", {A::Object}, ActionEffect::Document, true,
                false},
            {"sheets", "styleSelection", "styleSelection", {A::String, A::Object}, ActionEffect::Document,
                true, false},
            {"sheets", "resizeSelection", "resizeSelection", {A::Boolean, A::Number}, ActionEffect::Document,
                true, false},
            {"sheets", "columnWidth", "columnWidth", {A::Integer}, ActionEffect::Read, false, false},
            {"sheets", "rowHeight", "rowHeight", {A::Integer}, ActionEffect::Read, false, false},
            {"sheets", "sortSelection", "sortSelection", {A::Boolean, A::Boolean}, ActionEffect::Document,
                false, false},
            {"sheets", "insertTemplate", "insertTemplate", {A::String}, ActionEffect::Document, true, false},
            {"sheets", "snapshot", "snapshot", {}, ActionEffect::Read, true, false},
            {"sheets", "editSchema", "editSchema", {}, ActionEffect::Read, false, false},
            {"slides", "templatePreviews", "templateDescriptions", {A::Object}, ActionEffect::Read, true,
                false},
            {"slides", "media", "automationSlideMedia", {A::Integer, A::String, A::Number},
                ActionEffect::Session, false, false, true},
            {"slides", "animation", "automationSlideAnimation", {A::String, A::Number}, ActionEffect::Session,
                false, false, true},
            {"slides", "playbackSnapshot", "automationSlidePlayback", {}, ActionEffect::Read, false, false,
                true},
            {"slides", "rehearsal", "automationSlideRehearsal", {A::String}, ActionEffect::Session, false,
                false, true},
            {"slides", "presenter", "automationSlidePresenter", {A::String}, ActionEffect::Session, false,
                false, true},
            {"slides", "createEditableCopy", "createEditableCopy", {}, ActionEffect::Save, false, true},
            {"slides", "createEditableCopyTo", "createEditableCopyTo", {A::Url}, ActionEffect::Save, true,
                true},
            {"slides", "showHome", "showHome", {}, ActionEffect::Navigation, true, true},
            {"slides", "setSlide", "setSlide", {A::Integer}, ActionEffect::Session, true, false},
            {"slides", "nextSlide", "nextSlide", {}, ActionEffect::Session, false, false},
            {"slides", "previousSlide", "previousSlide", {}, ActionEffect::Session, false, false},
            {"slides", "selectShape", "selectShape", {A::Integer}, ActionEffect::Session, true, false},
            {"slides", "selectObject", "selectObject", {A::String}, ActionEffect::Session, true, false},
            {"slides", "snapshot", "snapshot", {}, ActionEffect::Read, true, false},
            {"slides", "semanticTree", "semanticTree", {A::Integer, A::Integer}, ActionEffect::Read, true,
                false},
            {"slides", "semanticPage", "semanticPage", {A::Integer, A::Integer, A::Integer},
                ActionEffect::Read, true, false},
            {"slides", "paragraphInfo", "paragraphInfo", {A::Integer}, ActionEffect::Read, true, false},
            {"slides", "speakerNotes", "speakerNotes", {A::Integer}, ActionEffect::Read, true, false},
            {"slides", "findText", "findText", {A::String, A::Boolean, A::Integer}, ActionEffect::Read, true,
                false},
            {"slides", "setGuideSettings", "setGuideSettings", {A::Object}, ActionEffect::Session, true,
                false},
            {"slides", "editSchema", "editSchema", {}, ActionEffect::Read, false, false},
            {"slides", "applyEdit", "applyEdit", {A::String, A::Object}, ActionEffect::Document, true, false},
            {"slides", "addImage", "addImage", {A::Url}, ActionEffect::Document, true, true},
            {"slides", "replaceImage", "replaceImage", {A::Url}, ActionEffect::Document, true, true},
            {"slides", "undo", "undo", {}, ActionEffect::Document, true, false},
            {"slides", "redo", "redo", {}, ActionEffect::Document, true, false},
            {"slides", "setZoom", "setZoom", {A::Number}, ActionEffect::Session, false, false},
            {"slides", "save", "save", {}, ActionEffect::Save, true, true},
            {"slides", "saveAs", "saveAs", {}, ActionEffect::Save, false, true},
            {"slides", "saveTo", "saveTo", {A::Url}, ActionEffect::Save, true, true},
            {"slides", "selectSaveFile", "selectSaveFile", {A::Url}, ActionEffect::Save, false, true},
            {"slides", "cancelSaveDialog", "cancelSaveDialog", {}, ActionEffect::Session, false, false},
            {"slides", "resolveUnsaved", "resolveUnsaved", {A::String}, ActionEffect::Session, false, true},
            {"slides", "requestWindowClose", "requestWindowClose", {}, ActionEffect::Session, false, false},
            {"slides", "clearError", "clearError", {}, ActionEffect::Session, false, false},
            {"slides", "clearMessage", "clearMessage", {}, ActionEffect::Session, false, false},
            {"images", "start", "start", {A::Url, A::Object}, ActionEffect::Export, true, true},
            {"images", "cancel", "cancel", {}, ActionEffect::Session, true, false},
            {"images", "clearMessage", "clearMessage", {}, ActionEffect::Session, false, false},
            {"images", "snapshot", "snapshot", {}, ActionEffect::Read, true, false},
            {"pdf", "requestHome", "requestHome", {}, ActionEffect::Navigation, true, true},
            {"pdf", "requestWindowClose", "requestWindowClose", {}, ActionEffect::Session, false, false},
            {"pdf", "resolveUnsaved", "resolveUnsaved", {A::String}, ActionEffect::Session, false, true},
            {"pdf", "save", "save", {}, ActionEffect::Save, true, true},
            {"pdf", "saveAs", "saveAs", {}, ActionEffect::Save, false, true},
            {"pdf", "saveTo", "saveTo", {A::Url}, ActionEffect::Save, true, true},
            {"pdf", "selectSaveFile", "selectSaveFile", {A::Url}, ActionEffect::Save, false, true},
            {"pdf", "cancelSaveDialog", "cancelSaveDialog", {}, ActionEffect::Session, false, false},
            {"pdf", "clearMessage", "clearMessage", {}, ActionEffect::Session, false, false},
            {"pdf", "undo", "undo", {}, ActionEffect::Document, true, false},
            {"pdf", "redo", "redo", {}, ActionEffect::Document, true, false},
            {"pdf", "execute", "execute", {A::String, A::Object}, ActionEffect::Document, true, false},
            {"pdf", "snapshot", "snapshot", {}, ActionEffect::Read, true, false},
            {"pdf", "editSchema", "editSchema", {}, ActionEffect::Read, false, false},
            {"pdf", "selectPage", "selectPage", {A::Integer}, ActionEffect::Session, true, false},
            {"mindmap", "requestHome", "requestHome", {}, ActionEffect::Navigation, true, true},
            {"mindmap", "requestWindowClose", "requestWindowClose", {}, ActionEffect::Session, false, false},
            {"mindmap", "resolveUnsaved", "resolveUnsaved", {A::String}, ActionEffect::Session, false, true},
            {"mindmap", "save", "save", {}, ActionEffect::Save, true, true},
            {"mindmap", "saveAs", "saveAs", {}, ActionEffect::Save, false, true},
            {"mindmap", "saveTo", "saveTo", {A::Url}, ActionEffect::Save, true, true},
            {"mindmap", "selectSaveFile", "selectSaveFile", {A::Url}, ActionEffect::Save, false, true},
            {"mindmap", "cancelSaveDialog", "cancelSaveDialog", {}, ActionEffect::Session, false, false},
            {"mindmap", "clearMessage", "clearMessage", {}, ActionEffect::Session, false, false},
            {"mindmap", "undo", "undo", {}, ActionEffect::Document, true, false},
            {"mindmap", "redo", "redo", {}, ActionEffect::Document, true, false},
            {"mindmap", "execute", "execute", {A::String, A::Object}, ActionEffect::Document, true, false},
            {"mindmap", "snapshot", "snapshot", {}, ActionEffect::Read, true, false},
            {"mindmap", "editSchema", "editSchema", {}, ActionEffect::Read, false, false},
            {"mindmap", "beginConnection", "beginConnection", {A::String, A::String}, ActionEffect::Session,
                false, false},
            {"mindmap", "connectNode", "connectNode", {A::String}, ActionEffect::Document, false, false},
            {"mindmap", "selectNode", "selectNode", {A::String}, ActionEffect::Session, true, false},
            {"mindmap", "outline", "outline", {}, ActionEffect::Read, true, false},
            {"mindmap", "copyOutline", "copyOutline", {}, ActionEffect::Session, false, false}};
        return specs;
    }

    const ActionSpec* office_action_spec(const QString& module, const QString& action)
    {
        for (const auto& spec : action_specs())
            if (module == QLatin1String(spec.module) && action == QLatin1String(spec.action))
                return &spec;
        return nullptr;
    }

    QString office_action_effect_name(ActionEffect effect)
    {
        switch (effect)
        {
        case ActionEffect::Read:
            return "read";
        case ActionEffect::Session:
            return "session";
        case ActionEffect::Document:
            return "document";
        case ActionEffect::Navigation:
            return "navigation";
        case ActionEffect::Create:
            return "create";
        case ActionEffect::Save:
            return "save";
        case ActionEffect::Export:
            return "export";
        }
        return "unknown";
    }

    QJsonObject office_action_metadata(const ActionSpec& spec)
    {
        const auto effect = office_action_effect_name(spec.effect);
        return {{"effect", effect}, {"aiPermitted", spec.ai_permitted},
            {"completion", spec.pending ? "observeState" : "immediate"},
            {"evidence",
                spec.effect == ActionEffect::Read             ? "observation"
                    : spec.effect == ActionEffect::Document   ? "acceptedEdit"
                    : spec.effect == ActionEffect::Create     ? "createdDocument"
                    : spec.effect == ActionEffect::Navigation ? "verifiedNavigation"
                    : spec.effect == ActionEffect::Save || spec.effect == ActionEffect::Export
                    ? "verifiedFile"
                    : "sessionState"}};
    }

    QJsonObject make_office_ai_contract(const QJsonObject& catalog, const QJsonObject& schemas)
    {
        QJsonObject modules;
        const auto available = catalog.value("modules").toObject();
        for (const auto* name :
            {"app", "text", "word", "sheets", "slides", "pdf", "mindmap", "export", "images"})
            modules.insert(name, available.value(name));
        const QJsonObject word{{"positions", "zero-based UTF-16 editor positions; end-exclusive"},
            {"positionSource",
                "readContent or find result; plainText offsets may differ at table/frame boundaries"},
            {"formatSchema", "word.editSchema"}, {"state", "modules.word + modules.word.snapshot"},
            {"selection", "ui.state.selection"}, {"zoom", "ratio [0.25,4]"},
            {"templateKinds", QJsonArray{"source", "meeting", "weekly"}}, {"requiresAttachedEditor", true},
            {"unsupported",
                QJsonArray{"image movement/insertion/deletion", "table structural edits", "exact pagination",
                    "arbitrary OOXML replacement"}}};
        const QJsonObject slides{{"indices", "zeroBased"}, {"geometry", "pt (1/72 inch)"},
            {"objectIdentity", "opaque string, document-session scoped, not an index"},
            {"selection", "selectObject(id); refresh global revision; applyEdit(action, options)"},
            {"capabilities",
                "semanticPage(page,offset,limit): limit 1..16, byte bounded; follow nextOffset. "
                "semanticTree(page,offset): legacy 64 objects. previewTruncated marks omitted details."},
            {"files",
                "addImage(fileUrl), replaceImage(fileUrl) are separate actions, not applyEdit names. "
                "saveTo(fileUrl): editable PPTX to a new absolute .pptx path, never overwrite; "
                "save(): existing editable path or user dialog. Await completion and check error/path."},
            {"paragraphs",
                "selectObject(id), paragraphInfo(index) for a zero-based paragraph; "
                "formatParagraph options.paragraphIndex targets one paragraph, omission targets all"},
            {"editSchema", "slides.editSchema"}, {"state", "modules.slides + modules.slides.snapshot"},
            {"playbackState", "slides.playbackSnapshot"},
            {"presenter",
                QJsonObject{{"action", "slides.presenter('start'|'stop')"},
                    {"requires", "two screens and an active unlocked slide document"},
                    {"state", "ui.state.presentation.mode='presenter'; playbackSnapshot.presenter"},
                    {"progress", "poll playbackSnapshot for audience media, animation and transition"}}},
            {"templatePreviews", "serializable metadata only; native preview rendering stays in GUI"},
            {"templateOptions", "{layoutKey:{palette:{ink,paper,card,muted,accent,soft}}}; colors #RRGGBB"},
            {"playbackNotifications",
                "poll playbackSnapshot for timeline ticks; not a document invalidation"},
            {"zoom", "0: fit; other finite numbers clamp to [0.25,3]"},
            {"guideSettings",
                QJsonObject{{"state", "slides.snapshot.guideSettings"},
                    {"action", "slides.setGuideSettings(patch); immediate UI-session state"},
                    {"scope", "documentSession; never written to PPTX"},
                    {"flags", "showRulers|showGrid|showGuides|snapToGrid|snapToGuides: bool"},
                    {"gridSpacingPt", "number [2,100]"},
                    {"verticalGuidesPt", "0..16 distinct numbers in [0,slideWidth]"},
                    {"horizontalGuidesPt", "0..16 distinct numbers in [0,slideHeight]"},
                    {"progress", "synchronous; refresh office revision and slides.snapshot"}}},
            {"animation",
                QJsonObject{{"actions", QJsonArray{"advance", "trigger", "restart", "seek"}},
                    {"value", "trigger: object index; seek: seconds [0,86400]; other actions ignore"}}},
            {"media",
                QJsonObject{{"shape", "current slide zero-based object index"},
                    {"actions", QJsonArray{"play", "pause", "stop", "seek", "volume", "loop", "slides"}},
                    {"value", "seek: seconds; volume: [0,1]; loop: 0|1; slides: positive integer"}}},
            {"textEditScope", "wholeObject; consult per-object capabilities before writing"}};
        const QJsonObject app{{"state", "modules.app; ui.state.home"},
            {"newKinds", QJsonArray{"writer", "markdown", "word", "sheets", "slides", "pdf", "mindmap"}},
            {"newPdf", "opens an existing PDF chooser, not an empty PDF document"},
            {"homeRoutes", QJsonArray{"home", "create", "recent", "local", "ai"}},
            {"categories",
                QJsonArray{
                    "all", "recent", "starred", "writer", "sheets", "slides", "pdf", "mindmap", "other"}},
            {"wordCategory", "writer includes DOCX/text; use the same category keys as the home GUI"},
            {"filterFiles", "query only; setHomeView also updates the visible search/category/route"},
            {"filePaths", "local file paths or file URLs; inspectFile/toggleStar use native path strings"}};
        const QJsonObject pdf_export{{"state", "modules.export.snapshot"},
            {"start", "start(module, destination, options); asynchronous atomic PDF output"},
            {"modulesInScope", QJsonArray{"text", "word", "sheets", "slides", "pdf", "mindmap"}},
            {"options",
                QJsonObject{{"scope", "all; sheets/slides/pdf also current; sheets also selection"},
                    {"layout", "fit; mindmap also tiles"},
                    {"compression", "structure; pdf also screen|print"}, {"landscape", "bool"},
                    {"overwrite", "bool; default false; caller must obtain authorization"}}}};
        const QJsonObject image_export{{"state", "modules.images.snapshot"},
            {"start", "start(parentFolderUrl, options); asynchronous export to a new result folder"},
            {"options",
                QJsonObject{{"format", "png|jpg"}, {"scope", "all|current"},
                    {"longEdge", "1280|1920 pixels; aspect ratio preserved"}}},
            {"progress", "completed/total updates during render; success/path are final only"},
            {"atomicity",
                "pages are staged in one temporary folder; a finished folder appears only on success"}};
        return {{"ok", true}, {"contractVersion", office_ai_contract_version}, {"protocol", 1},
            {"revision", catalog.value("revision")}, {"modules", modules}, {"schemas", schemas},
            {"availability",
                "registered method and signature only; current readiness/capability may still reject"},
            {"mutationMeaning",
                "session state including clipboard/selection/view, not necessarily document bytes"},
            {"semantics",
                QJsonObject{{"app", app}, {"word", word}, {"slides", slides}, {"export", pdf_export},
                    {"images", image_export}}},
            {"execution",
                QJsonObject{{"entry", "office_execute"}, {"thread", "GUI owner thread only"},
                    {"requestFields", QJsonArray{"version", "module", "action", "args", "expectedRevision"}},
                    {"freshRevisionRequired", true}, {"stateEntry", "office_snapshot"},
                    {"responseFields", QJsonArray{"ok", "protocol", "revision", "error", "result"}},
                    {"resultOnError", "optional; action_rejected carries false, validation errors omit it"},
                    {"pending", "initiated only; observe state/dialogs for success or failure"},
                    {"dialogDecisions", QJsonArray{"save", "discard", "cancel"}},
                    {"sourceOverwrite", "forbidden for imported Word/PPTX; use editable copy"},
                    {"errors",
                        QJsonArray{"unavailable", "wrong_thread", "stale_revision", "not_ready",
                            "inactive_module", "module_unavailable", "method_unavailable", "invalid_json",
                            "invalid_schema", "invalid_arguments", "unknown_action", "action_rejected",
                            "request_too_large", "unsupported_version", "ui_unavailable", "invoke_failed",
                            "response_too_large"}}}},
            {"synchronization",
                QJsonObject{{"model", "same live session and undo history as GUI"},
                    {"notifications", "office_subscribe_changes; queued/coalesced invalidations"},
                    {"payload", "revision only; explicitly pull snapshot/schema as needed"},
                    {"frameCompletion", false}, {"operationLog", false},
                    {"guiOnlyState", "selection/home/playback depend on UI composition"}}},
            {"maximumRequestBytes", catalog.value("maximumRequestBytes")},
            {"maximumSnapshotBytes", catalog.value("maximumSnapshotBytes")},
            {"contentTrust", "untrustedDocumentData"},
            {"notImplemented",
                QJsonArray{"batch transactions", "headless document session", "AI overwrite via saveTo"}}};
    }
}
