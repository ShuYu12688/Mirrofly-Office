#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace mirrorfly
{
    struct PresentationMediaCue
    {
        std::string target;
        std::string action = "play";
        int click = 0;
        double delay = 0;
        double position = -1;
        double volume = 1;
        bool loop = false;
        std::string trigger;
        int slide_count = 1;
    };

    struct PresentationAnimation
    {
        std::string target;
        std::string category;
        std::string filter = "appear";
        int click = 0;
        double delay = 0;
        double duration = 0.5;
        double repeat = 1;
        bool reverse = false;
        bool hold = true;
        double rotation = 0;
        std::array<double, 2> scale{1, 1};
        std::array<double, 2> direction{0, 1};
        std::vector<std::array<double, 2>> motion;
        std::string color;
        std::string trigger;
        int paragraph_start = -1;
        int paragraph_end = -1;
        int character_start = -1;
        int character_end = -1;
        std::string iterate_type;
        double iterate_interval = 0;
        bool iterate_interval_percent = false;
        bool iterate_backwards = false;
        int iterate_count = 0;
    };

    struct PresentationAnimationState
    {
        double opacity = 1;
        double x = 0;
        double y = 0;
        double scale_x = 1;
        double scale_y = 1;
        double rotation = 0;
        std::string clip;
        double reveal = 1;
        std::string color;
    };

    struct PresentationAnimationTextPosition
    {
        int character = -1;
        int letter = -1;
        int letter_count = 0;
        int word = -1;
        int word_count = 0;
        int element = -1;
        int element_count = 0;
    };

    // Click zero starts on entering the slide. Missing later click times mean not triggered.
    PresentationAnimationState presentation_animation_state(
        const std::vector<PresentationAnimation>& animations, const std::string& target,
        const std::vector<std::string>& groups, double time, const std::vector<double>& click_times,
        double slide_width, double slide_height,
        const std::map<std::string, std::vector<double>>& trigger_times = {}, int paragraph = -1,
        const PresentationAnimationTextPosition& text_position = {});
}
