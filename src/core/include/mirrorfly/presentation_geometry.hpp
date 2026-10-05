#pragma once

#include <array>
#include <string>
#include <vector>

namespace mirrorfly
{
    enum class PresentationPathAction
    {
        Move,
        Line,
        Quadratic,
        Cubic,
        Close
    };

    struct PresentationPathCommand
    {
        PresentationPathAction action = PresentationPathAction::Move;
        std::array<double, 6> values{};
    };

    struct PresentationPath
    {
        double width = 1;
        double height = 1;
        std::string fill = "norm";
        bool stroke = true;
        std::vector<PresentationPathCommand> commands;
    };

    struct PresentationGeometry
    {
        double width = 0;
        double height = 0;
        std::array<double, 4> text_rect{};
        std::vector<PresentationPath> paths;
        std::string error;
    };

    // Dimensions are points; definition optionally supplies prstGeom adjustments or custGeom XML.
    PresentationGeometry presentation_geometry(
        const std::string& preset, double width, double height, const std::string& definition = {});
    std::vector<std::string> presentation_geometry_presets();
}
