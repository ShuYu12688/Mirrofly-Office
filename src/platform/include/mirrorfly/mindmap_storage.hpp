#pragma once

#include <mirrorfly/mindmap.hpp>

#include <string>

namespace mirrorfly
{
    struct MindMapFileResult
    {
        MindMapError error = MindMapError::None;
        std::string message;
        std::string path;
        std::string revision;
        MindMapDocument document;
    };

    MindMapFileResult load_mindmap_file(const std::string& path);
    // expected_revision="missing" requires a new destination.
    MindMapFileResult save_mindmap_file(
        const std::string& path, const MindMapDocument& document, const std::string& expected_revision = {});
}
