#include <mirrorfly/presentation.hpp>

namespace mirrorfly
{
    PresentationNavigationResult resolve_presentation_click(const PresentationScene& scene,
        std::size_t current_slide, std::size_t shape_index, int last_viewed_slide)
    {
        PresentationNavigationResult result;
        if (current_slide >= scene.slides.size() || shape_index >= scene.slides[current_slide].shapes.size())
            return result;
        const auto& action = scene.slides[current_slide].shapes[shape_index].click_action;
        if (action.kind == "endshow")
        {
            result.handled = true;
            result.end_show = true;
            return result;
        }
        if (action.kind == "slide")
            result.target_slide = action.target_slide;
        else if (action.kind == "firstslide")
        {
            result.target_slide = 0;
            while (result.target_slide < static_cast<int>(scene.slides.size()) &&
                scene.slides[static_cast<std::size_t>(result.target_slide)].hidden)
                ++result.target_slide;
        }
        else if (action.kind == "lastslide")
        {
            result.target_slide = static_cast<int>(scene.slides.size()) - 1;
            while (result.target_slide >= 0 &&
                scene.slides[static_cast<std::size_t>(result.target_slide)].hidden)
                --result.target_slide;
        }
        else if (action.kind == "lastslideviewed")
            result.target_slide = last_viewed_slide;
        else if (action.kind == "nextslide" || action.kind == "previousslide")
        {
            const int step = action.kind == "nextslide" ? 1 : -1;
            result.target_slide = static_cast<int>(current_slide) + step;
            while (result.target_slide >= 0 && result.target_slide < static_cast<int>(scene.slides.size()) &&
                scene.slides[static_cast<std::size_t>(result.target_slide)].hidden)
                result.target_slide += step;
        }
        if (result.target_slide < 0 || result.target_slide >= static_cast<int>(scene.slides.size()) ||
            result.target_slide == static_cast<int>(current_slide))
        {
            result.target_slide = -1;
            return result;
        }
        result.handled = true;
        return result;
    }
}
