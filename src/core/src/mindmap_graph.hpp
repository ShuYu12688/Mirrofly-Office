#pragma once
#include <mirrorfly/mindmap.hpp>

namespace mirrorfly
{
    MindMapStatus validate_mindmap_graph(const MindMapDocument& document);
    MindMapParseResult parse_mindmap_graph(const std::string& xml);
    MindMapTextResult serialize_mindmap_graph(const MindMapDocument& document);
    MindMapStatus edit_mindmap_graph(MindMapDocument& document, const MindMapCommand& command);
    MindMapTextResult outline_mindmap_graph(const MindMapDocument& document);
}
