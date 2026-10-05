#include "presentation_guide_settings.hpp"

#include <QMetaType>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
    bool read_guides(const QVariant& source, double maximum, std::vector<double>& destination)
    {
        if (source.metaType().id() != QMetaType::QVariantList)
            return false;
        const auto values = source.toList();
        if (values.size() > 16)
            return false;
        std::vector<double> guides;
        guides.reserve(static_cast<std::size_t>(values.size()));
        for (const auto& value : values)
        {
            if (value.metaType().id() != QMetaType::Double && value.metaType().id() != QMetaType::Int)
                return false;
            const double position = value.toDouble();
            if (!std::isfinite(position) || position < 0 || position > maximum)
                return false;
            guides.push_back(position);
        }
        std::sort(guides.begin(), guides.end());
        for (std::size_t index = 1; index < guides.size(); ++index)
            if (guides[index] - guides[index - 1] < 0.5)
                return false;
        destination = std::move(guides);
        return true;
    }
}

namespace mirrorfly
{
    QVariantMap PresentationGuideSettings::toMap() const
    {
        QVariantList vertical;
        QVariantList horizontal;
        for (double position : vertical_guides_pt)
            vertical.append(position);
        for (double position : horizontal_guides_pt)
            horizontal.append(position);
        return {{"showRulers", show_rulers}, {"showGrid", show_grid}, {"showGuides", show_guides},
            {"snapToGrid", snap_to_grid}, {"snapToGuides", snap_to_guides},
            {"gridSpacingPt", grid_spacing_pt}, {"verticalGuidesPt", vertical},
            {"horizontalGuidesPt", horizontal}, {"units", "pt"}, {"scope", "documentSession"}};
    }

    bool PresentationGuideSettings::update(
        const QVariantMap& patch, double slide_width_pt, double slide_height_pt)
    {
        static const std::set<QString> keys{"showRulers", "showGrid", "showGuides", "snapToGrid",
            "snapToGuides", "gridSpacingPt", "verticalGuidesPt", "horizontalGuidesPt"};
        if (patch.empty() || !std::isfinite(slide_width_pt) || !std::isfinite(slide_height_pt) ||
            slide_width_pt <= 0 || slide_height_pt <= 0)
            return false;
        for (auto item = patch.constBegin(); item != patch.constEnd(); ++item)
            if (!keys.count(item.key()))
                return false;
        auto candidate = *this;
        const auto boolean = [&patch](const QString& key, bool& target)
        {
            if (!patch.contains(key))
                return true;
            const auto value = patch.value(key);
            if (value.metaType().id() != QMetaType::Bool)
                return false;
            target = value.toBool();
            return true;
        };
        if (!boolean("showRulers", candidate.show_rulers) || !boolean("showGrid", candidate.show_grid) ||
            !boolean("showGuides", candidate.show_guides) || !boolean("snapToGrid", candidate.snap_to_grid) ||
            !boolean("snapToGuides", candidate.snap_to_guides))
            return false;
        if (patch.contains("gridSpacingPt"))
        {
            const auto value = patch.value("gridSpacingPt");
            if (value.metaType().id() != QMetaType::Double && value.metaType().id() != QMetaType::Int)
                return false;
            const double spacing = value.toDouble();
            if (!std::isfinite(spacing) || spacing < 2 || spacing > 100)
                return false;
            candidate.grid_spacing_pt = spacing;
        }
        if (patch.contains("verticalGuidesPt") &&
            !read_guides(patch.value("verticalGuidesPt"), slide_width_pt, candidate.vertical_guides_pt))
            return false;
        if (patch.contains("horizontalGuidesPt") &&
            !read_guides(patch.value("horizontalGuidesPt"), slide_height_pt, candidate.horizontal_guides_pt))
            return false;
        *this = std::move(candidate);
        return true;
    }
}
