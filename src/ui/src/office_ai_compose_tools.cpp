#include "office_ai_tool_definitions.hpp"
#include "office_ai_tools.hpp"

namespace mirrorfly
{
    using namespace ai_tools;

    QJsonArray office_ai_compose_tools(const QSet<QString>& groups)
    {
        QJsonArray tools;
        if (groups.contains("word"))
        {
            const QJsonObject text{{"type", "string"}, {"minLength", 1}, {"maxLength", 450}};
            const QJsonObject section{{"type", "object"},
                {"properties",
                    QJsonObject{{"heading", QJsonObject{{"type", "string"}, {"maxLength", 80}}},
                        {"paragraphs", QJsonObject{{"type", "array"}, {"items", text}, {"maxItems", 8}}},
                        {"bullets",
                            QJsonObject{{"type", "array"},
                                {"items",
                                    QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 200}}},
                                {"maxItems", 8}}}}},
                {"required", QJsonArray{"heading", "paragraphs"}}, {"additionalProperties", false}};
            tools.append(definition("office_compose_word",
                "Compose one fresh Word document in a single call: title, optional subtitle and 1..8 "
                "sections with short paragraphs or bullets. Local preflight computes UTF-16 positions "
                "and applies heading/body styles through public Word actions. Save after the receipt; "
                "for existing files use office_read and precise edits.",
                {{"title", QJsonObject{{"type", "string"}, {"maxLength", 100}}},
                    {"subtitle", QJsonObject{{"type", "string"}, {"maxLength", 180}}},
                    {"sections",
                        QJsonObject{
                            {"type", "array"}, {"items", section}, {"minItems", 1}, {"maxItems", 8}}}},
                {"title", "sections"}));
        }
        if (groups.contains("sheets"))
        {
            const QJsonObject cell{{"type", QJsonArray{"string", "number", "boolean"}}};
            tools.append(definition("office_compose_table",
                "Compose one fresh worksheet in a single call. title names the worksheet tab; "
                "column headings occupy row 1, data starts in row 2. Use those rows in formulas. "
                "Local preflight checks formulas and widths, then pastes and styles through public "
                "sheet actions. Set totalRow when the last row is a total. Save after the receipt; "
                "for existing files use office_read and precise edits.",
                {{"title", QJsonObject{{"type", "string"}, {"maxLength", 31}}},
                    {"columns",
                        QJsonObject{{"type", "array"},
                            {"items", QJsonObject{{"type", "string"}, {"maxLength", 40}}}, {"minItems", 1},
                            {"maxItems", 12}}},
                    {"rows",
                        QJsonObject{{"type", "array"},
                            {"items",
                                QJsonObject{
                                    {"type", "array"}, {"items", cell}, {"minItems", 1}, {"maxItems", 12}}},
                            {"minItems", 1}, {"maxItems", 100}}},
                    {"totalRow", QJsonObject{{"type", "boolean"}}},
                    {"columnWidth", QJsonObject{{"type", "number"}, {"minimum", 8}, {"maximum", 40}}}},
                {"title", "columns", "rows"}));
        }
        if (groups.contains("slides"))
        {
            const QJsonObject block{{"type", "object"},
                {"properties",
                    QJsonObject{{"heading", QJsonObject{{"type", "string"}, {"maxLength", 60}}},
                        {"text", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 600}}},
                        {"example", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}}}},
                {"required", QJsonArray{"text"}}, {"additionalProperties", false}};
            const QJsonObject page{{"type", "object"},
                {"properties",
                    QJsonObject{
                        {"title", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 100}}},
                        {"subtitle", QJsonObject{{"type", "string"}, {"maxLength", 180}}},
                        {"eyebrow", QJsonObject{{"type", "string"}, {"maxLength", 40}}},
                        {"layout",
                            QJsonObject{{"type", "string"},
                                {"enum",
                                    QJsonArray{"cover", "visual", "columns", "grid", "steps", "flow",
                                        "comparison", "statement", "hub", "code", "factcheck"}},
                                {"description",
                                    "Blocks: cover 0..1; statement 0..1 (subtitle if empty); "
                                    "grid/columns/steps 1..4; flow/hub 2..4; comparison 2; factcheck 2..3; "
                                    "visual 1..2 + imagePath; code 0..2 + code. Hub needs short subtitle. "
                                    "Omit layout for automatic choice."}}},
                        {"code", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 600}}},
                        {"coverWord", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 8}}},
                        {"imagePath", QJsonObject{{"type", "string"}, {"minLength", 1}}},
                        {"blocks",
                            QJsonObject{{"type", "array"}, {"items", block}, {"maxItems", 4},
                                {"description",
                                    "Required for content pages; may be omitted on cover, statement and code "
                                    "pages when there are no supporting blocks."}}}}},
                {"required", QJsonArray{"title"}}, {"additionalProperties", false}};
            const QJsonObject color{{"type", "string"}, {"pattern", "^#[0-9a-fA-F]{6}$"}};
            const QJsonObject theme{{"type", "object"},
                {"properties", QJsonObject{{"ink", color}, {"paper", color}, {"accent", color}}},
                {"additionalProperties", false}};
            tools.append(definition("office_compose_slides",
                "Append 1..8 pages. First call requires total targetPages; each batchId is unique. "
                "Save when deckProgress is complete. Omit layout for automatic fitting or query "
                "office_layout. "
                "Keep text concise; overflow is checked before editing. Choose styleId via office_style.",
                {{"batchId", QJsonObject{{"type", "string"}, {"maxLength", 64}}},
                    {"targetPages", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}},
                    {"styleId",
                        QJsonObject{{"type", "string"},
                            {"enum", QJsonArray{"editorial", "modern", "natural", "research"}}}},
                    {"theme", theme},
                    {"pages",
                        QJsonObject{{"type", "array"}, {"items", page}, {"minItems", 1}, {"maxItems", 8}}}},
                {"batchId", "pages"}));
            tools.append(definition("office_continue_slides",
                "Continue a retained partial page batch after settlement. Never regenerates completed steps; "
                "refuses external state changes.",
                {{"batchId", string_type()}}, {"batchId"}));
        }
        return tools;
    }
}
