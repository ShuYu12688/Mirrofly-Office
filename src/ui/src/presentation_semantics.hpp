#pragma once

#include <mirrorfly/presentation.hpp>

#include <QVariantMap>

#include <array>

namespace mirrorfly
{
    QVariantList presentation_capability_names(const PresentationShape& shape);
    QVariantMap presentation_table_style_options(const PresentationTableStyleOptions& options);
    QVariantMap presentation_table_structure_options(const PresentationTableStructureOptions& options);
    QVariantMap presentation_theme_state_map(const PresentationScene& scene, std::size_t slide_index);
    std::array<int, 2> presentation_group_targets(const PresentationSlide& slide, int selected_index);
    QVariantList presentation_selection_actions(const PresentationSlide& slide, int selected_index);
    QVariantMap presentation_semantic_tree(
        const PresentationScene& scene, int page, int offset, int limit = 64);
    QVariantMap presentation_edit_schema();
}
