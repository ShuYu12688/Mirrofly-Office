#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mirrorfly
{
    constexpr std::size_t maximum_mindmap_xml_bytes = 2 * 1024 * 1024;
    constexpr std::size_t maximum_mindmap_document_bytes = 512 * 1024;
    constexpr std::size_t maximum_mindmap_nodes = 1000;
    constexpr std::size_t maximum_mindmap_depth = 32;
    constexpr std::size_t maximum_mindmap_node_text_bytes = 2048;
    constexpr std::size_t maximum_mindmap_id_bytes = 256;

    enum class MindMapError
    {
        None,
        UnsupportedType,
        Unsupported,
        InvalidXml,
        TooLarge,
        InvalidDocument,
        NotFound,
        DuplicateId,
        RootProtected,
        Cycle,
        ReadOnly,
        InvalidCommand,
        ReadFailed,
        WriteFailed,
        ChangedOnDisk
    };

    struct MindMapNode
    {
        std::string id;
        std::string parent_id;
        std::string text;
        bool collapsed = false;
        std::vector<std::string> children;
        double x = 80;
        double y = 80;
        double width = 220;
        double height = 84;
        std::string border;
        std::string fill;
        std::string shape = "rounded";
        double border_width = 2;
    };

    struct MindMapEdge
    {
        std::string id;
        std::string from;
        std::string to;
        std::string label;
    };

    struct MindMapDocument
    {
        std::string root_id;
        std::vector<MindMapNode> nodes;
        bool read_only = false;
        std::string read_only_reason;
        bool free_layout = false;
        std::vector<MindMapEdge> edges;
    };

    enum class MindMapCommandType
    {
        AddChild,
        AddSibling,
        Rename,
        DeleteSubtree,
        MoveUp,
        MoveDown,
        Reparent,
        ToggleCollapse,
        CreateNode,
        MoveNode,
        StyleNode,
        Connect,
        Disconnect,
        AutoLayout
    };

    struct MindMapCommand
    {
        MindMapCommandType type = MindMapCommandType::Rename;
        std::string target_id;
        std::string new_id;
        std::string parent_id;
        std::string text;
        double x = 80;
        double y = 80;
        double width = 220;
        double height = 84;
        std::string border;
        std::string fill;
        std::string shape = "rounded";
        double border_width = 2;
    };

    struct MindMapStatus
    {
        MindMapError error = MindMapError::None;
        std::string message;
        bool changed = false;
    };

    struct MindMapParseResult
    {
        MindMapError error = MindMapError::None;
        std::string message;
        bool changed = false;
        MindMapDocument document;
    };

    struct MindMapTextResult
    {
        MindMapError error = MindMapError::None;
        std::string message;
        bool changed = false;
        std::string text;
    };

    bool is_mindmap_path(const std::string& path);
    MindMapDocument make_mindmap(const std::string& root_text = "中心主题");
    MindMapDocument make_free_mindmap(const std::string& root_text = "开始");
    MindMapStatus validate_mindmap(const MindMapDocument& document);
    MindMapParseResult parse_mindmap(const std::string& xml);
    MindMapTextResult serialize_mindmap(const MindMapDocument& document);
    MindMapTextResult export_mindmap_outline(const MindMapDocument& document);
    MindMapStatus apply_mindmap_command(MindMapDocument& document, const MindMapCommand& command);
}
