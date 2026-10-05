#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace mirrorfly
{
    struct ImageDimensions
    {
        int width = 0;
        int height = 0;
    };

    struct DecodedRasterImage
    {
        ImageDimensions source_size;
        ImageDimensions size;
        std::vector<std::uint8_t> rgba;
    };

    enum class MetafileFormat
    {
        Emf,
        Wmf
    };

    // First TIFF frame, with orientation applied; 24 MiB input, 16M source pixels, 2048 output side.
    ImageDimensions inspect_tiff_image(std::string_view bytes);
    DecodedRasterImage decode_tiff_image(std::string_view bytes);

    // Windows Media Photo / JPEG XR, decoded through WIC with a bounded output side.
    ImageDimensions inspect_wdp_image(std::string_view bytes);
    DecodedRasterImage decode_wdp_image(std::string_view bytes, int maximum_side = 2048);

    // Static package-local SVG only; external references and active content are rejected.
    ImageDimensions inspect_svg_image(std::string_view bytes);
    DecodedRasterImage decode_svg_image(std::string_view bytes);

    // Bounded package-local Windows metafiles; records are validated before Windows playback.
    ImageDimensions inspect_metafile_image(std::string_view bytes, MetafileFormat format);
    DecodedRasterImage decode_metafile_image(std::string_view bytes, MetafileFormat format);
}
