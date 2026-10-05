#pragma once

namespace mirrorfly
{
    // QTextDocument geometry uses 96-dpi logical pixels; the Word model and tools use points.
    constexpr double word_points_to_pixels(double value)
    {
        return value * 4 / 3;
    }

    constexpr double word_pixels_to_points(double value)
    {
        return value * 3 / 4;
    }
}
