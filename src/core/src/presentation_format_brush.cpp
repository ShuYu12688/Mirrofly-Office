#include "presentation_format_brush.hpp"

namespace mirrorfly
{
    namespace
    {
        bool has_resource_style(const PresentationShape& shape)
        {
            if (!shape.fill.image_path.empty() || !shape.outline_fill.image_path.empty() ||
                !shape.effects.outline_fill.image_path.empty() || shape.approximate_3d)
                return true;
            for (const auto& paragraph : shape.text.paragraphs)
            {
                if (!paragraph.bullet_image_path.empty())
                    return true;
                for (const auto& run : paragraph.runs)
                    if (!run.fill.image_path.empty() || !run.effects.outline_fill.image_path.empty())
                        return true;
            }
            return false;
        }
    }

    bool presentation_format_brush_supported(const PresentationShape& shape)
    {
        return shape.editable && !shape.table_cell && shape.source_groups.empty() &&
            !has_resource_style(shape);
    }

    bool apply_presentation_format_brush(PresentationShape& target, const PresentationShape& source)
    {
        if (!presentation_format_brush_supported(source) || !presentation_format_brush_supported(target) ||
            source.image_path.empty() != target.image_path.empty())
            return false;

        if (source.image_path.empty())
            target.fill = source.fill;
        target.outline_color = source.outline_color;
        target.outline_fill = source.outline_fill;
        target.outline_opacity = source.outline_opacity;
        target.outline_width = source.outline_width;
        target.line_style = source.line_style;
        target.effects = source.effects;
        target.soft_edge_radius = source.soft_edge_radius;
        target.inner_shadow = source.inner_shadow;
        if (!source.image_path.empty())
        {
            target.image_opacity = source.image_opacity;
            return true;
        }

        auto paragraphs = std::move(target.text.paragraphs);
        target.text = source.text;
        target.text.paragraphs = std::move(paragraphs);
        if (source.text.paragraphs.empty())
            return true;
        const auto& source_paragraph = source.text.paragraphs.front();
        for (auto& paragraph : target.text.paragraphs)
        {
            auto runs = std::move(paragraph.runs);
            paragraph = source_paragraph;
            paragraph.runs = std::move(runs);
            if (source_paragraph.runs.empty())
                continue;
            const auto& source_run = source_paragraph.runs.front();
            for (auto& run : paragraph.runs)
            {
                auto text = std::move(run.text);
                auto click_action = std::move(run.click_action);
                run = source_run;
                run.text = std::move(text);
                run.click_action = std::move(click_action);
            }
        }
        return true;
    }
}
