#include "presentation_table.hpp"

namespace mirrorfly
{
    PresentationTableStructureOptions PresentationTableStructure::options(
        std::size_t row, std::size_t column) const
    {
        PresentationTableStructureOptions result;
        if (!valid || row >= rows || column >= columns)
            return result;
        const auto index = row * columns + column;
        result.insert_row = rows < 512 && (rows + 1) * columns <= 2000 && !blocked_row_insertions[row];
        result.insert_column =
            columns < 256 && (columns + 1) * rows <= 2000 && !blocked_column_insertions[column];
        result.delete_row = rows > 1 && !merged_rows[row];
        result.delete_column = columns > 1 && !merged_columns[column];
        result.merge_right = column + 1 < columns && !merged_cells[index] && !merged_cells[index + 1] &&
            !text_cells[index + 1];
        result.merge_down = row + 1 < rows && !merged_cells[index] && !merged_cells[index + columns] &&
            !text_cells[index + columns];
        return result;
    }

    PresentationTableStructure presentation_table_structure(const PresentationTableCellGrid& grid)
    {
        PresentationTableStructure result;
        if (grid.empty() || grid.size() > 512 || grid.front().empty() || grid.front().size() > 256 ||
            grid.size() > 2000 / grid.front().size())
            return result;
        result.rows = grid.size();
        result.columns = grid.front().size();
        const auto size = result.rows * result.columns;
        result.merged_cells.resize(size);
        result.merged_rows.resize(result.rows);
        result.merged_columns.resize(result.columns);
        result.blocked_row_insertions.resize(result.rows);
        result.blocked_column_insertions.resize(result.columns);
        result.text_cells.resize(size);
        for (std::size_t row = 0; row < result.rows; ++row)
        {
            if (grid[row].size() != result.columns)
                return result;
            for (std::size_t column = 0; column < result.columns; ++column)
            {
                const auto cell = grid[row][column];
                result.text_cells[row * result.columns + column] = presentation_table_cell_has_text(cell);
                const auto grid_span = cell.attribute("gridSpan");
                const auto row_span = cell.attribute("rowSpan");
                const int width = grid_span ? grid_span.as_int(0) : 1;
                const int height = row_span ? row_span.as_int(0) : 1;
                if (width < 1 || height < 1)
                    return result;
                if (width == 1 && height == 1)
                    continue;
                if (cell.attribute("hMerge").as_bool() || cell.attribute("vMerge").as_bool())
                    return result;
                const auto merge = presentation_table_merge(grid, row, column);
                if (!merge)
                    return result;
                for (std::size_t dy = 0; dy < merge->row_span; ++dy)
                    for (std::size_t dx = 0; dx < merge->column_span; ++dx)
                    {
                        const auto current_row = row + dy;
                        const auto current_column = column + dx;
                        const auto index = current_row * result.columns + current_column;
                        if (result.merged_cells[index])
                            return result;
                        result.merged_cells[index] = true;
                        result.merged_rows[current_row] = true;
                        result.merged_columns[current_column] = true;
                    }
                for (std::size_t current = row; current + 1 < row + merge->row_span; ++current)
                    result.blocked_row_insertions[current] = true;
                for (std::size_t current = column; current + 1 < column + merge->column_span; ++current)
                    result.blocked_column_insertions[current] = true;
            }
        }
        for (std::size_t row = 0; row < result.rows; ++row)
            for (std::size_t column = 0; column < result.columns; ++column)
            {
                const auto cell = grid[row][column];
                const bool continuation =
                    cell.attribute("hMerge").as_bool() || cell.attribute("vMerge").as_bool();
                const bool origin =
                    cell.attribute("gridSpan").as_int(1) > 1 || cell.attribute("rowSpan").as_int(1) > 1;
                if (continuation && !result.merged_cells[row * result.columns + column])
                    return result;
                if (!continuation && !origin && result.merged_cells[row * result.columns + column])
                    return result;
            }
        result.valid = true;
        return result;
    }
}
