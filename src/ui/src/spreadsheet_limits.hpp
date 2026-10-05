#pragma once

#include <QtTypes>
#include <cstddef>

namespace mirrorfly
{
    inline constexpr qsizetype maximum_cell_input_units = 32767;
    inline constexpr qsizetype maximum_cell_preview_units = 256;
    inline constexpr std::size_t maximum_spreadsheet_history_bytes = 32 * 1024 * 1024;
}
