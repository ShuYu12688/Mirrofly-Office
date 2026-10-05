#include <mirrorfly/presentation.hpp>

#include "presentation_theme_package.hpp"

#include <algorithm>

namespace mirrorfly
{
    PresentationThemeState presentation_theme_state(const PresentationScene& scene, std::size_t slide_index)
    {
        PresentationThemeState state;
        if (slide_index >= scene.slides.size())
            return state;
        const auto& slide = scene.slides[slide_index];
        if (!slide.theme_available)
            return state;
        if (slide.theme_index >= 0 && static_cast<std::size_t>(slide.theme_index) < scene.themes.size())
        {
            const auto& definition = scene.themes[static_cast<std::size_t>(slide.theme_index)];
            state.available = true;
            state.name = definition.name;
            state.colors = definition.colors;
            state.fonts = definition.fonts;
            state.editable_color_slots = definition.editable_color_slots;
            state.editable_font_slots = definition.editable_font_slots;
            state.linked_slide_count = static_cast<std::size_t>(
                std::count_if(scene.slides.begin(), scene.slides.end(), [&](const auto& candidate)
            {
                return candidate.theme_index == slide.theme_index;
            }));
            return state;
        }
        if (!slide.authored_theme)
            return state;
        const auto definition = authored_theme_definition(slide.authored_palette);
        state.available = true;
        state.authored = true;
        state.name = definition.name;
        state.colors = definition.colors;
        state.fonts = definition.fonts;
        state.editable_color_slots = definition.editable_color_slots;
        state.editable_font_slots = definition.editable_font_slots;
        state.linked_slide_count = static_cast<std::size_t>(
            std::count_if(scene.slides.begin(), scene.slides.end(), [&](const auto& candidate)
        {
            return candidate.authored_theme && candidate.authored_palette == slide.authored_palette;
        }));
        return state;
    }
}
