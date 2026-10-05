#include "office_ai_tool_definitions.hpp"
#include "office_ai_tools.hpp"

namespace mirrorfly
{
    using namespace ai_tools;

    QJsonArray office_ai_document_tools(const QSet<QString>& groups)
    {
        QJsonArray tools;
        if (groups.contains("slides"))
        {
            tools.append(definition("office_layout", "Inspect layout constraints; empty name lists layouts.",
                {{"name", string_type()}}));
            tools.append(definition("office_style",
                "List selectable presentation styles, or inspect one styleId and its design direction. "
                "Choose for the subject and audience before the first page batch.",
                {{"styleId", string_type()}}));
            if (groups.contains("media"))
            {
                tools.append(definition("office_image_search",
                    "Search Openverse for relevant CC0 or public-domain still images. Returns "
                    "source page and license metadata; inspect before choosing an id. Search terms in "
                    "English often work best. No slide is changed.",
                    {{"query", QJsonObject{{"type", "string"}, {"minLength", 2}, {"maxLength", 100}}}},
                    {"query"}));
                tools.append(definition("office_image_fetch",
                    "Download one image id from searches in this task into the local verified image cache. "
                    "Use the returned imagePath on cover or visual pages. Do not invent an id or path.",
                    {{"id", string_type()}}, {"id"}));
            }
        }
        if (groups.contains("compose"))
            for (const auto& tool : office_ai_compose_tools(groups))
                tools.append(tool);
        return tools;
    }
}
