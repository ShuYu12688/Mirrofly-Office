#pragma once

#include <mirrorfly/office_progress.hpp>
#include <mirrorfly/presentation.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace mirrorfly
{
    enum class PresentationLoadStage
    {
        Reading,
        Validating,
        Extracting,
        Parsing
    };

    struct PresentationLoadProgress
    {
        PresentationLoadStage stage = PresentationLoadStage::Reading;
        std::size_t completed = 0;
        std::size_t total = 0;
    };

    using PresentationLoadProgressCallback = std::function<void(const PresentationLoadProgress& progress)>;

    struct PresentationImageFileResult
    {
        PresentationError error = PresentationError::None;
        std::string path;
        std::string mime_type;
        std::string bytes;
        int width = 0;
        int height = 0;
    };

    // Reads only the requested local package; archive members are never extracted to the filesystem.
    PresentationResult load_presentation_file(
        const std::string& path, const PresentationLoadProgressCallback& progress = {});

    // A nonempty revision must match the file bytes before the atomic replacement.
    // The reserved revision "missing" requires the destination to remain absent.
    PresentationResult save_presentation_file(const std::string& path,
        const std::vector<PresentationPart>& parts, const std::string& expected_revision,
        const OfficeSaveProgress& progress = {});
    PresentationResult save_presentation_bytes(
        const std::string& path, const std::string& bytes, const std::string& expected_revision);
    PresentationImageFileResult load_presentation_image_file(const std::string& path);

}
