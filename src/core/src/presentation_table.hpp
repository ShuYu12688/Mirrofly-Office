#pragma once

#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace mirrorfly
{
    struct PresentationTableReader
    {
        std::function<PresentationText(pugi::xml_node, const std::vector<pugi::xml_node>&)> text;
        std::function<PresentationFill(pugi::xml_node)> fill;
        std::function<std::pair<std::string, double>(pugi::xml_node)> color;
        PresentationFill header_fill;
        PresentationFill band_fill;
    };

    using PresentationTableCellGrid = std::vector<std::vector<pugi::xml_node>>;

    struct PresentationTableMerge
    {
        std::size_t row_span = 1;
        std::size_t column_span = 1;
        std::vector<pugi::xml_node> cells;
    };

    struct PresentationTableStructure
    {
        bool valid = false;
        std::size_t rows = 0;
        std::size_t columns = 0;
        std::vector<bool> merged_cells;
        std::vector<bool> merged_rows;
        std::vector<bool> merged_columns;
        std::vector<bool> blocked_row_insertions;
        std::vector<bool> blocked_column_insertions;
        std::vector<bool> text_cells;

        PresentationTableStructureOptions options(std::size_t row, std::size_t column) const;
    };

    PresentationTableCellGrid presentation_table_cell_grid(pugi::xml_node table, std::size_t column_count);
    std::optional<PresentationTableMerge> presentation_table_merge(
        const PresentationTableCellGrid& grid, std::size_t row, std::size_t column);
    bool presentation_table_cell_has_text(pugi::xml_node cell);
    PresentationTableStructure presentation_table_structure(const PresentationTableCellGrid& grid);

    std::vector<PresentationShape> presentation_table_shapes(pugi::xml_node table,
        const PresentationShape& frame, pugi::xml_node styles, const PresentationTableReader& reader,
        std::vector<std::string>& warnings);
}
