#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mirrorfly
{
    struct PresentationMediaPlayer;
    using PresentationMediaPlayerPtr = std::shared_ptr<PresentationMediaPlayer>;

    struct PresentationMediaState
    {
        bool ready = false;
        bool playing = false;
        bool ended = false;
        bool has_video = false;
        double position = 0;
        double duration = 0;
        int width = 0;
        int height = 0;
        std::string error;
    };

    struct PresentationMediaFrame
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> bgra;
    };

    struct PresentationMediaOpenResult
    {
        PresentationMediaPlayerPtr player;
        std::string error;
    };

    // Local files only. Create, control, poll and destroy a player on the same thread.
    PresentationMediaOpenResult open_presentation_media(const std::string& path);
    PresentationMediaState presentation_media_state(const PresentationMediaPlayerPtr& player);
    PresentationMediaFrame presentation_media_frame(const PresentationMediaPlayerPtr& player);
    bool play_presentation_media(const PresentationMediaPlayerPtr& player, bool playing);
    bool seek_presentation_media(const PresentationMediaPlayerPtr& player, double seconds);
    bool configure_presentation_media(const PresentationMediaPlayerPtr& player, double volume, bool loop);
}
