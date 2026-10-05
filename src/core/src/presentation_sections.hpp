#pragma once

#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <string>
#include <vector>

namespace mirrorfly
{
    std::vector<std::string> read_presentation_sections(
        pugi::xml_node presentation, const std::vector<std::string>& slide_ids, PresentationScene& scene);
    void patch_presentation_sections(pugi::xml_node presentation, const PresentationScene& scene,
        const std::vector<std::string>& slide_ids);
    PresentationEditResult edit_presentation_section(
        PresentationScene& scene, const PresentationEditCommand& command, PresentationEditResult result);
    void assign_inserted_slide_section(PresentationScene& scene, std::size_t slide_index);
    void reconcile_deleted_slide_sections(PresentationScene& scene);
    void reconcile_moved_slide_section(PresentationScene& scene, std::size_t source, std::size_t target);
}
