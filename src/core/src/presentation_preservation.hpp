#pragma once

#include <mirrorfly/presentation.hpp>

#include <map>

namespace mirrorfly
{
    struct PresentationPackageState
    {
        std::map<std::string, std::shared_ptr<const std::string>> parts;
        std::string presentation_part;
    };

    PresentationEditResult preserve_presentation_edit(const PresentationScene& before,
        PresentationScene& after, const PresentationEditCommand& command, PresentationEditResult result);
    PresentationPackageResult preserved_presentation_package(const PresentationScene& scene);
}
