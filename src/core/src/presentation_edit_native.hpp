#pragma once

#include <mirrorfly/presentation.hpp>

#include <map>
#include <optional>
#include <string>

namespace mirrorfly::presentation_edit_native
{
    struct DecomposedTransform
    {
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
        double angle = 0;
        bool flip_vertical = false;
    };

    struct TransformedBounds
    {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
    };

    bool valid_color(const std::string& color);
    std::optional<TransformedBounds> transformed_bounds(const PresentationShape& shape);
    std::optional<DecomposedTransform> decompose_transform(const PresentationShape& shape);
    void compose_transform(PresentationShape& shape, const DecomposedTransform& transform);
    long long emu(double points);
    std::string solid_fill(const PresentationFill& fill);
    std::string shape_xml(const PresentationShape& shape, std::size_t shape_index,
        const std::map<std::string, std::string>& image_relations);
}
