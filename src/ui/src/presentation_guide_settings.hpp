#pragma once

#include <QVariantMap>

#include <vector>

namespace mirrorfly
{
    struct PresentationGuideSettings
    {
        bool show_rulers = false;
        bool show_grid = false;
        bool show_guides = false;
        bool snap_to_grid = false;
        bool snap_to_guides = false;
        double grid_spacing_pt = 12;
        std::vector<double> vertical_guides_pt;
        std::vector<double> horizontal_guides_pt;

        QVariantMap toMap() const;
        bool update(const QVariantMap& patch, double slide_width_pt, double slide_height_pt);
    };
}
