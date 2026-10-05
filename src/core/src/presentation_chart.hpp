#pragma once

#include <mirrorfly/presentation.hpp>
#include <pugixml.hpp>

#include <functional>

namespace mirrorfly
{
    struct PresentationChartReader
    {
        std::function<PresentationFill(pugi::xml_node)> fill;
        std::function<PresentationText(pugi::xml_node)> text;
        std::vector<std::string> colors;
    };

    std::vector<PresentationShape> presentation_chart_shapes(pugi::xml_node chart_space,
        const PresentationShape& frame, const PresentationChartReader& reader,
        std::vector<std::string>& warnings);

    std::vector<PresentationShape> presentation_chart_ex_shapes(pugi::xml_node chart_space,
        const PresentationShape& frame, const PresentationChartReader& reader,
        std::vector<std::string>& warnings);
}
