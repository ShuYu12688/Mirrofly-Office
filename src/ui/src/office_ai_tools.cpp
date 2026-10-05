#include "office_ai_tools.hpp"
#include "office_ai_tool_definitions.hpp"

#include <mirrorfly/automation.hpp>

#include <QJsonDocument>

namespace mirrorfly
{
    using namespace ai_tools;
    QJsonObject office_ai_groups()
    {
        QJsonArray groups;
        groups.append(
            QJsonObject{{"name", "slides"}, {"description", "PPTX：页面布局、配色、字体、图形与排版指导"}});
        groups.append(QJsonObject{{"name", "word"}, {"description", "Word：正文、段落、标题与格式"}});
        groups.append(QJsonObject{{"name", "sheets"}, {"description", "表格：数据、公式与样式"}});
        groups.append(
            QJsonObject{{"name", "mindmap"}, {"description", "原生思维导图：节点、层级、连接与样式"}});
        groups.append(QJsonObject{{"name", "text"}, {"description", "纯文本和 Markdown 编辑"}});
        groups.append(QJsonObject{{"name", "pdf"}, {"description", "当前 PDF 的页面操作与批注"}});
        groups.append(QJsonObject{{"name", "compose"}, {"description", "当前Word/表格/PPT的批量内容生成"}});
        groups.append(QJsonObject{{"name", "media"}, {"description", "在线开放授权配图"}});
        groups.append(QJsonObject{{"name", "export"}, {"description", "当前文档导出 PDF"}});
        groups.append(QJsonObject{{"name", "images"}, {"description", "当前 PPT 导出 PNG/JPG"}});
        return {{"ok", true}, {"groups", groups}};
    }

    QJsonArray office_ai_tools(const QSet<QString>& groups)
    {
        const QJsonObject strings{{"type", "array"}, {"items", string_type()}};
        const QJsonObject task_mode{
            {"type", "string"}, {"enum", QJsonArray{"create", "append", "modify", "query"}}};
        QJsonArray tools{definition("office_workspace", "Read current document, selection and blockers."),
            definition("office_groups", "List document groups."),
            definition("office_load_group",
                "Inspect current module; compose loads bulk document creation, media loads online images, "
                "export/images load file export. Other pages require navigation first.",
                {{"group",
                    QJsonObject{{"type", "string"},
                        {"enum",
                            QJsonArray{"slides", "word", "sheets", "mindmap", "text", "pdf", "export",
                                "images", "media", "compose"}}}}},
                {"group"}),
            definition("office_home", "Return to home after saving. Refuses unsaved changes."),
            definition("office_open",
                "Open an existing absolute local path; its tools load after entering the page.",
                {{"path", string_type()}}, {"path"}),
            definition("office_new",
                "Create the next requested native document. This tool loads its document group and "
                "makes its page instructions available. Save the current work before switching; "
                "after creation, edit this document instead of creating it again.",
                {{"kind",
                    QJsonObject{{"type", "string"},
                        {"enum", QJsonArray{"writer", "markdown", "word", "sheets", "slides", "mindmap"}}}}},
                {"kind"}),
            definition("office_save",
                "Save with destination for an exact absolute file path, title for a new Desktop filename, "
                "or current=true to update the current editable file/copy. Choose exactly one. "
                "After editable_copy use current=true; other existing files cannot be overwritten.",
                {{"destination", string_type()}, {"title", string_type()},
                    {"current", QJsonObject{{"type", "boolean"}}}}),
            definition("office_task",
                "Keep a short checklist for multi-document work. Results, not this checklist, prove "
                "completion.",
                {{"mode", task_mode},
                    {"phase",
                        QJsonObject{{"type", "string"},
                            {"enum", QJsonArray{"inspect", "edit", "verify", "done", "blocked"}}}},
                    {"goal", string_type()}, {"targets", strings}, {"completed", strings},
                    {"remaining", strings}},
                {"mode", "phase", "goal", "targets", "completed", "remaining"})};
        QJsonArray modules;
        for (const auto* group : {"word", "slides", "sheets", "mindmap", "text", "pdf", "export", "images"})
        {
            if (!groups.contains(group))
                continue;
            modules.append(group);
        }
        if (groups.contains("word") || groups.contains("slides"))
            tools.append(definition("office_editable_copy",
                "For imported read-only Word/PPT, create an editable copy. Use destination for an explicit "
                "new absolute file path, or title for a Desktop copy; never both. Wait for file.saved and "
                "editable=true, edit it, then office_save(current=true). Original stays intact.",
                {{"title", string_type()}, {"destination", string_type()}}));
        if (!modules.isEmpty())
        {
            const QJsonObject module{{"type", "string"}, {"enum", modules}};
            QJsonArray readable;
            for (const auto& item : modules)
                if (item != "export" && item != "images")
                    readable.append(item);
            const QJsonObject op{{"type", "string"}, {"description", "module.action from office_schema"}};
            const QJsonObject args{{"type", QJsonArray{"object", "array"}}};
            const QJsonObject step{{"type", "object"},
                {"properties", QJsonObject{{"op", op}, {"args", args}}},
                {"required", QJsonArray{"op", "args"}}, {"additionalProperties", false}};
            if (!readable.isEmpty())
                tools.append(definition("office_read",
                    "Bounded content. overview lists pages/sheets or counts; content lists "
                    "objects/paragraphs/cells/nodes. "
                    "For text/Markdown edits, find with text=exact literal returns source UTF-16 start/end; "
                    "use these positions with options.expectedText, never count characters. Re-find after "
                    "edits. "
                    "text reads UTF-16 chunks; index is zero-based and selects page/sheet/Word paragraph "
                    "(the second slide is index 1), id selects "
                    "object/cell/node. text requires id for slides and mindmap. "
                    "Follow nextOffset; -1 ends. PDF also annotations; mindmap edges; slides notes or format "
                    "(id=object ID; paint and first text run, not uniform text); sheets "
                    "formula or format (id=cell address; resolved base format and displayed conditional "
                    "color). Word format uses paragraph index and UTF-16 offset for one position, not "
                    "a uniform range.",
                    {{"module", QJsonObject{{"type", "string"}, {"enum", readable}}},
                        {"view",
                            QJsonObject{{"type", "string"},
                                {"enum",
                                    QJsonArray{"overview", "content", "text", "notes", "annotations", "edges",
                                        "formula", "format", "find"}}}},
                        {"index", QJsonObject{{"type", "integer"}, {"minimum", -1}}},
                        {"offset", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
                        {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 2000}}},
                        {"id", string_type()},
                        {"text", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}}},
                    {"module", "view"}));
            tools.append(definition("office_state", "Read module metadata. Use office_read for content.",
                {{"module", module}}, {"module"}));
            tools.append(definition("office_schema",
                "Empty name lists action/edit names and help topics; query one name for details. Enables "
                "office_action and office_batch.",
                {{"module", module}, {"name", string_type()}}, {"module", "name"}));
            if (groups.contains("actions"))
            {
                tools.append(definition("office_action",
                    "Call module.action with exact named args or ordered parameters from office_schema. "
                    "Runtime checks the observed revision; external edits invalidate it.",
                    {{"op", op}, {"args", args}}, {"op", "args"}));
                tools.append(definition("office_batch",
                    "Call known operations in order. Stops on failure or asynchronous settlement; "
                    "completed steps remain applied. Creation selects the new object. Use returned "
                    "remaining/nextStep. Each step's args follows office_action: ordered parameters or "
                    "exact named parameters; do not nest action/args inside args.",
                    {{"steps",
                        QJsonObject{{"type", "array"}, {"items", step}, {"minItems", 1}, {"maxItems", 256}}}},
                    {"steps"}));
            }
        }
        for (const auto& tool : office_ai_document_tools(groups))
            tools.append(tool);
        return tools;
    }
}
