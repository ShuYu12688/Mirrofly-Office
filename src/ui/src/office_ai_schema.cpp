#include "office_ai_contract_adapter.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_tools.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/office_ai.hpp>

#include <QJsonDocument>

namespace
{
    QJsonObject parse_object(const std::string& json)
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
    }

    QJsonObject module_schema(const QString& module, const QJsonObject& catalog)
    {
        // Read only this module through the public interface. A schema lookup must not
        // rebuild every module's contract for each edit in a page batch.
        const QJsonObject request{{"module", module}, {"action", "editSchema"}, {"args", QJsonArray{}},
            {"expectedRevision", catalog.value("revision")}};
        return parse_object(
            mirrorfly::office_execute(QJsonDocument(request).toJson(QJsonDocument::Compact).toStdString()))
            .value("result")
            .toObject();
    }
}

namespace mirrorfly
{
    QJsonObject OfficeAiToolbox::validateEdit(
        const QString& module, const QString& name, const QJsonValue& revision) const
    {
        const auto schema = module_schema(module, {{"revision", revision}});
        if (schema.value(name).isObject())
            return {{"ok", true}};
        // Keep detailed discovery errors for unknown names without rebuilding the full
        // action catalog and invocation description on every known edit.
        return schemaFor(module, name);
    }

    QJsonObject OfficeAiToolbox::capabilitiesFor(const QString& module, const QJsonObject& catalog) const
    {
        if (!QStringList{"app", "text", "word", "slides", "sheets", "mindmap", "pdf", "export", "images"}
                .contains(module))
            return {{"ok", false}, {"error", "unsupported_module"}};
        if (!catalog.value("ok").toBool())
            return catalog;
        QJsonArray actions;
        const auto entries = catalog.value("modules").toObject().value(module).toArray();
        for (const auto& value : entries)
        {
            const auto action = value.toObject();
            const auto name = action.value("name").toString();
            if (office_ai_permitted(module, name) && action.value("available").toBool())
                actions.append(QJsonObject{{"name", name}, {"parameters", action.value("parameters")},
                    {"mutating", action.value("mutating")}, {"completion", action.value("completion")},
                    {"pending", action.value("pending")}});
        }
        return {{"ok", true}, {"module", module}, {"actions", actions}};
    }

    QJsonObject OfficeAiToolbox::schemaFor(const QString& module, const QString& name) const
    {
        if (name.startsWith(module + '.') && name.count('.') == 1)
            return schemaFor(module, name.mid(module.size() + 1));
        const auto catalog = parse_object(office_action_catalog());
        if (!name.isEmpty() && office_ai_permitted(module, name))
        {
            const auto signature = office_ai_action_signature(catalog, module, name);
            if (signature.value("ok").toBool())
            {
                auto result = signature;
                if (name == "saveTo")
                    result.insert("destination",
                        "Use a new complete file path; another existing file cannot be overwritten. "
                        "For the current editable saved file/copy, prefer office_save(current=true). "
                        "saveTo of that same file also saves current; it does not create another copy.");
                if (module == "word" && name == "createEditableCopyTo")
                    result.insert("destination",
                        "Complete new .docx file path in an existing directory, not a directory path. "
                        "Example: C:/existing-folder/edited-copy.docx; the file must not already exist.");
                if (module == "text" && name == "formatMarkdown")
                {
                    result.insert("positions",
                        "Source UTF-16 offsets, end exclusive. First call office_read(module=text, "
                        "view=find,text=exact literal), choose the correct returned range, and pass "
                        "options.expectedText. Never manually count or reuse positions after editing. "
                        "options is required (an object, even for bold/italic/strike).");
                    result.insert("actions",
                        QJsonArray{"bold", "italic", "strike", "inlineCode", "removeInlineCode", "heading",
                            "paragraph", "bullet", "ordered", "task", "taskSet", "listIndent", "listOutdent",
                            "quote", "quoteSet", "code", "table", "tableAlign", "link", "unlink", "image",
                            "removeImage", "hardBreak", "thematicBreak"});
                    result.insert("options",
                        QJsonObject{{"expectedText",
                                        "Exact source guard: must equal the selected source, or begin "
                                        "at a caret without consuming text. Mismatch rejects atomically; "
                                        "re-find, do not retry stale offsets."},
                            {"headingLevel", "integer 1..6, default 2"},
                            {"quoteLevel",
                                "quoteSet requires an integer 0..8; 0 removes the selected outer quote "
                                "depth"},
                            {"rows", "table body rows 1..20, default 3"},
                            {"columns", "table columns 1..12, default 3"},
                            {"column", "tableAlign requires a zero-based column integer 0..31"},
                            {"alignment", "tableAlign requires default, left, center or right"},
                            {"checked",
                                "taskSet requires a boolean; true checks existing tasks, false unchecks"},
                            {"language", "code language string, default empty"}});
                    auto link_options = result.value("options").toObject();
                    link_options.insert("url",
                        "link/image: single-line string up to 4096 UTF-8 bytes; absolute URI, relative "
                        "path or #fragment");
                    link_options.insert("title",
                        "link/image: optional single-line tooltip string up to 1024 UTF-8 bytes; empty "
                        "clears the "
                        "title");
                    link_options.insert("alt",
                        "image: optional single-line alternative text; omitted preserves an existing image "
                        "caption; empty is allowed");
                    result.insert("options", link_options);
                    result.insert("semantics",
                        "image inserts at a caret or replaces selected text with an image using url, alt and "
                        "title. Inside an existing image it updates that occurrence, preserving outer links "
                        "and shared reference definitions. removeImage replaces the image with its plain "
                        "alternative text. Empty image destinations are valid; links require nonempty URLs. "
                        "Visual editing resolves local PNG/JPEG/BMP/GIF/WebP relative to the document file; "
                        "missing or remote resources show an alternative-text placeholder, without network "
                        "fetches. Dimensions are display-only and are not written as nonstandard Markdown. "
                        "Re-read source after edits; reference images may normalize to inline form. "
                        "thematicBreak inserts a block separator after the entire current paragraph or "
                        "heading; start=end is a caret. Inside a list it follows the complete list, "
                        "retaining descendants and starting a plain continuation at the current quote "
                        "depth. Code and table cells reject; existing text is preserved. "
                        "hardBreak inserts a paragraph-internal hard line break at a caret or converts an "
                        "existing soft newline; use start=end within existing paragraph text. It preserves "
                        "quote/list continuation and link labels. Code, headings, table cells, link "
                        "destinations and terminal blank breaks reject. "
                        "heading requires integer headingLevel 1..6 (default 2); paragraph removes only the "
                        "heading style. Existing empty ATX headings can change level or reset without "
                        "placeholder "
                        "text; resetting removes the empty heading node while retaining its parent "
                        "containers. "
                        "Both operate on whole parsed paragraphs, preserve quote/list "
                        "parents, "
                        "explicit character styles and links, and normalize Setext headings to ATX. Soft "
                        "multiline text becomes one heading line. Hard breaks and opaque multiline objects "
                        "cannot become headings. A GFM checkbox head stays a paragraph; use an independent "
                        "continued paragraph for a heading without changing task state. Re-read source "
                        "offsets after conversion. "
                        "Code HTML/footnote literals, escaped tags and ordinary pipe text remain literal; "
                        "preserve their source. Actual raw HTML, footnote definitions and front matter "
                        "retain source but pause visual editing. "
                        "Paragraph and heading character styles remain independent, including crossing "
                        "bold/italic/strike spans and links. Saved source may use numeric entities; read "
                        "the latest source before computing further UTF-16 offsets. Heading appearance is "
                        "not an explicit bold mark. Inline actions add source markers, not a toggle. "
                        "Bold/italic/strike retain leading, trailing and all-whitespace selections, "
                        "including "
                        "Unicode spaces and tabs, using character references at boundaries; inner source "
                        "markup stays intact. Re-read UTF-16 positions after each edit instead of trimming "
                        "the selection or reusing old offsets. "
                        "Multi-paragraph list items retain their continuation ownership and ordered "
                        "quote/list nesting during character style edits and save-reopen. Read the entire "
                        "item including continuation paragraphs before changing container structure. "
                        "Preserve ordered starts, dot/parenthesis delimiters and loose-list spacing during "
                        "local character edits. Subsequent source numerals do not restart a list. Changing "
                        "the bullet character or ordered delimiter creates a separate CommonMark list. "
                        "Owned code blocks, tables and thematic breaks keep their quote/list order during "
                        "local character edits, including an empty list head. Preserve code literals and "
                        "languages, table alignment and explicit cell emphasis; a header's appearance does "
                        "not imply bold source markup. "
                        "List style conversion preserves "
                        "nesting and existing task states. taskSet explicitly sets existing task markers "
                        "and is idempotent. listIndent moves selected list items with their descendants "
                        "including their continued paragraphs, owned quotes, code, tables and rules under "
                        "the preceding sibling; listOutdent moves them one level toward the root. "
                        "Code-only empty heads remain visible list items; source may use a fence on the "
                        "marker line or normalize indented code to safe fences to retain literal tabs. "
                        "Read the entire subtree again after moving because offsets and prefixes change. "
                        "A selection starts on a list item; maximum nesting is 8. quote and quoteSet set "
                        "container depth explicitly, preserving nested relative depth and character styles. "
                        "quote sets depth 1; quoteSet uses quoteLevel. A caret inside a table/code block "
                        "styles that whole container. A list item's descendants follow its quote depth. "
                        "Tables cannot be set below their nonempty list parent's quote depth. Root outdent "
                        "and "
                        "first-sibling "
                        "indent reject. tableAlign sets one existing column's GFM "
                        "delimiter without changing table text or other columns; start/end must be within "
                        "that table. Use source positions from readContent. link uses options.url and "
                        "optional title. "
                        "A caret or selection inside an existing inline, reference or automatic link updates "
                        "that occurrence, "
                        "preserving its label; reference definitions and other uses remain unchanged. "
                        "Adjacent links with equal targets remain separate occurrences; do not span two "
                        "occurrences in one selection. "
                        "unlink removes the current link while retaining label markup or literal "
                        "automatic-link text. New links use "
                        "a single-line selection; "
                        "an empty selection inserts a label. Do not wrap another link or edit code literals. "
                        "inlineCode quotes literal source with safe backticks inside one parsed paragraph, "
                        "including its soft multiline continuation and list/quote prefixes. Line endings "
                        "render as spaces; interior spaces are retained. Blank paragraph boundaries, code "
                        "blocks and link destinations reject. removeInlineCode accepts a caret or range "
                        "inside one existing span and replaces that entire span with escaped decoded "
                        "literal text, keeping parent containers and outer links/emphasis. Re-read source "
                        "offsets after normalization. Use code when actual line breaks must be retained. "
                        "It is different from the code block action. Plain-text documents reject this "
                        "action.");
                }
                if (module == "sheets")
                    result.insert("options", module_schema(module, catalog).value(name));
                return result;
            }
        }
        const auto capabilities = capabilitiesFor(module, catalog);
        if (!capabilities.value("ok").toBool())
            return {{"ok", false}, {"error", "unsupported_module"}, {"module", module}, {"requested", name},
                {"validModules",
                    QJsonArray{
                        "app", "text", "word", "slides", "sheets", "mindmap", "pdf", "export", "images"}}};
        QJsonArray actions;
        for (const auto& value : capabilities.value("actions").toArray())
            actions.append(value.toObject().value("name"));
        QJsonObject schema;
        if (QStringList{"word", "slides", "sheets", "mindmap", "pdf"}.contains(module))
            schema = module_schema(module, catalog);
        const auto values = module == "word" ? schema.value("formats").toObject() : schema;
        QJsonArray names;
        QJsonObject metadata;
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            if (it.value().isObject())
                names.append(it.key());
            else
                metadata.insert(it.key(), it.value());
        }
        QJsonArray topics;
        for (const auto& key : metadata.keys())
            topics.append("help:" + key);
        if (name.isEmpty())
            return {{"ok", true}, {"module", module}, {"actions", actions}, {"names", names},
                {"topics", topics},
                {"hint",
                    "actions are public methods; names are edit/format operations passed to applyEdit, "
                    "execute or format; topics are explanatory notes. Query one exact name for details."}};
        const QString topic = name.startsWith("help:") ? name.mid(5) : name;
        if (metadata.contains(topic))
            return {
                {"ok", true}, {"module", module}, {"topic", topic}, {"description", metadata.value(topic)}};
        if (!values.value(name).isObject())
            return {{"ok", false}, {"error", "unknown_edit_name"}, {"module", module}, {"requested", name},
                {"validNames", names}, {"topics", topics}, {"availableActions", actions},
                {"hint", "Query one available action or valid edit name; an empty name lists both."}};
        if (module == "word")
        {
            const auto formats = schema.value("formats").toObject();
            return {{"ok", true}, {"module", module}, {"name", name}, {"options", formats.value(name)},
                {"positions", schema.value("positions")},
                {"invocation",
                    QJsonObject{{"tool", "office_action"}, {"op", "word.format"},
                        {"args",
                            QJsonObject{{"start", "<start UTF-16>"}, {"end", "<end UTF-16>"},
                                {"action", name}, {"value", "<value matching options>"}}}}}};
        }
        QJsonObject result{{"ok", true}, {"module", module}, {"name", name}, {"options", schema.value(name)}};
        if (module == "slides")
        {
            result.insert("scope", schema.value("scope"));
            result.insert("selection",
                "For an existing object, use office_batch with two steps: "
                "{op:slides.selectObject,args:{id:<observed ID>}}, then the invocation below. "
                "No extra workspace read between these steps; runtime checks revision. "
                "Pass only declared option keys; do not add id or range.");
        }
        if (QStringList{"slides", "mindmap", "pdf"}.contains(module))
        {
            const QString wrapper = module == "slides" ? "applyEdit" : "execute";
            const auto signature = office_ai_action_signature(catalog, module, wrapper);
            const auto parameters = signature.value("parameters").toArray();
            if (parameters.size() == 2)
                result.insert("invocation",
                    QJsonObject{{"tool", "office_action"}, {"op", module + '.' + wrapper},
                        {"args",
                            QJsonObject{{parameters.at(0).toObject().value("name").toString(), name},
                                {parameters.at(1).toObject().value("name").toString(),
                                    "<options object matching the schema above>"}}}});
        }
        return result;
    }

}
