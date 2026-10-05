#include "presentation_table_fixture.hpp"

#include <algorithm>
#include <iostream>

namespace
{
    int failures = 0;
    void check(bool value, const char* label)
    {
        if (!value)
        {
            std::cerr << label << '\n';
            ++failures;
        }
    }

    int run_table_tests()
    {
        using namespace mirrorfly;
        const auto original = test_fixture::table_package();
        auto parsed = parse_presentation(original);
        check(parsed.error == PresentationError::None, "table package parses");
        const auto& shapes = parsed.scene.slides[0].shapes;
        check(shapes.size() >= 4, "table produces content and borders");
        if (shapes.size() < 4)
            return 1;
        check(shapes[0].width == 200 && shapes[0].height == 40 && shapes[0].transform[4] == 10 &&
                shapes[0].transform[5] == 20,
            "grid and merged header respect frame coordinates");
        check(shapes[0].fill.color == "#2244AA" && shapes[0].text.paragraphs[0].runs[0].color == "#FFFFFF" &&
                shapes[0].text.paragraphs[0].runs[0].bold,
            "header style overrides whole-table fill and font");
        check(shapes[1].height == 80 && shapes[1].fill.color == "#FF0000" && shapes[1].text.inset_left == 2 &&
                shapes[1].text.vertical_alignment == "center",
            "row span and explicit cell format override table style");
        check(shapes[2].text.paragraphs[0].runs[0].color == "#112233" &&
                shapes[3].text.paragraphs[0].runs[0].text == "B3",
            "table body style inherited and merge continuation skipped");
        check(std::all_of(shapes.begin(), shapes.end(),
                  [](const auto& shape)
        {
            return shape.editable == shape.table_cell.has_value() && shape.source_groups[0] == "22";
        }),
            "cells editable, derived borders locked and linked to animation target");
        auto styled = parse_presentation(original);
        check(styled.error == PresentationError::None, "whole-table style fixture parses");
        if (styled.error == PresentationError::None)
        {
            styled.scene.native_editable = true;
            const auto& initial = *styled.scene.slides[0].shapes[0].table_cell;
            check(
                initial.style_available && initial.style_options.first_row && initial.style_options.band_rows,
                "selected merged table cell exposes whole-table style options");
            PresentationEditCommand table_style;
            table_style.action = PresentationEditAction::FormatTableStyle;
            table_style.shape_index = 0;
            check(apply_presentation_edit(styled.scene, table_style).error ==
                    PresentationEditError::InvalidValue,
                "whole-table style requires a complete option set");
            table_style.table_style_options = PresentationTableStyleOptions{};
            check(apply_presentation_edit(styled.scene, table_style).error == PresentationEditError::None &&
                    styled.scene.slides[0].shapes[0].fill.color == "#EFEFEF" &&
                    styled.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].color == "#112233" &&
                    styled.scene.slides[0].shapes[1].fill.color == "#FF0000",
                "turning off title and band regions reveals whole-table style without changing direct cell "
                "fill");
            table_style.table_style_options->first_row = true;
            table_style.table_style_options->band_rows = true;
            check(apply_presentation_edit(styled.scene, table_style).error == PresentationEditError::None &&
                    styled.scene.slides[0].shapes[0].fill.color == "#2244AA" &&
                    styled.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].color == "#FFFFFF",
                "whole-table title style can be restored in one transaction");
            const auto styled_saved = serialize_presentation(styled.scene);
            const auto styled_readback = parse_presentation(styled_saved.parts);
            check(styled_saved.error == PresentationError::None &&
                    styled_readback.error == PresentationError::None &&
                    styled_readback.scene.slides[0].shapes[0].table_cell->style_options.first_row,
                "whole-table style survives save and reopen");
            for (const auto& part : original)
            {
                if (part.path == "ppt/slides/slide1.xml")
                    continue;
                const auto found = std::find_if(styled_saved.parts.begin(), styled_saved.parts.end(),
                    [&](const auto& candidate)
                {
                    return candidate.path == part.path;
                });
                check(found != styled_saved.parts.end() && found->bytes == part.bytes,
                    "whole-table style preserves unrelated package parts byte exact");
            }
        }
        auto inline_parts = original;
        std::string inline_style;
        for (const auto& part : inline_parts)
            if (part.path == "ppt/tableStyles.xml")
            {
                const auto first = part.bytes.find("<a:tblStyle styleId=");
                const auto last = part.bytes.find("</a:tblStyle>", first);
                if (first != std::string::npos && last != std::string::npos)
                    inline_style = part.bytes.substr(first, last + 13 - first);
            }
        check(!inline_style.empty(), "inline table style fixture locates source style");
        if (!inline_style.empty())
        {
            inline_style.replace(1, 10, "a:tableStyle");
            const auto closing = inline_style.rfind("</a:tblStyle>");
            if (closing != std::string::npos)
                inline_style.replace(closing + 2, 10, "a:tableStyle");
            for (auto& part : inline_parts)
                if (part.path == "ppt/slides/slide1.xml")
                {
                    const std::string reference = "<a:tableStyleId>{TEST}</a:tableStyleId>";
                    const auto position = part.bytes.find(reference);
                    if (position != std::string::npos)
                        part.bytes.replace(position, reference.size(), inline_style);
                }
            auto inlined = parse_presentation(inline_parts);
            check(inlined.error == PresentationError::None &&
                    inlined.scene.slides[0].shapes[0].table_cell->style_available &&
                    inlined.scene.slides[0].shapes[0].fill.color == "#2244AA",
                "inline table style is read without a tableStyleId reference");
            if (inlined.error == PresentationError::None)
            {
                inlined.scene.native_editable = true;
                PresentationEditCommand inline_edit;
                inline_edit.action = PresentationEditAction::FormatTableStyle;
                inline_edit.table_style_options = PresentationTableStyleOptions{};
                check(apply_presentation_edit(inlined.scene, inline_edit).error ==
                            PresentationEditError::None &&
                        inlined.scene.slides[0].shapes[0].fill.color == "#EFEFEF",
                    "inline table style option changes preserve its embedded definition");
            }
        }
        auto authored = make_presentation(PresentationSlideLayout::Blank);
        authored.native_editable = true;
        PresentationEditCommand insert_style_table;
        insert_style_table.action = PresentationEditAction::InsertTable;
        insert_style_table.table_rows = 3;
        insert_style_table.table_columns = 3;
        check(apply_presentation_edit(authored, insert_style_table).error == PresentationEditError::None &&
                authored.slides[0].shapes.size() >= 9 &&
                authored.slides[0].shapes[0].table_cell->style_available &&
                authored.slides[0].shapes[0].fill.color != authored.slides[0].shapes[3].fill.color,
            "new table links an authored theme style with a distinct title row");
        PresentationEditCommand columns_style;
        columns_style.action = PresentationEditAction::FormatTableStyle;
        columns_style.table_style_options = PresentationTableStyleOptions{};
        columns_style.table_style_options->first_column = true;
        columns_style.table_style_options->band_columns = true;
        check(apply_presentation_edit(authored, columns_style).error == PresentationEditError::None &&
                authored.slides[0].shapes[3].fill.color != authored.slides[0].shapes[4].fill.color &&
                authored.slides[0].shapes[4].fill.color != authored.slides[0].shapes[5].fill.color &&
                authored.slides[0].shapes[3].table_cell->style_options.first_column &&
                authored.slides[0].shapes[5].table_cell->style_options.band_columns,
            "new table uses distinct first-column and alternating-column theme regions");
        auto structured = make_presentation(PresentationSlideLayout::Blank);
        structured.native_editable = true;
        PresentationEditCommand insert_structure;
        insert_structure.action = PresentationEditAction::InsertTable;
        insert_structure.table_rows = 3;
        insert_structure.table_columns = 3;
        check(apply_presentation_edit(structured, insert_structure).error == PresentationEditError::None,
            "structure fixture creates a three by three table");
        const auto cell_index = [](const PresentationScene& scene, std::size_t row, std::size_t column)
        {
            const auto& cells = scene.slides[0].shapes;
            for (std::size_t index = 0; index < cells.size(); ++index)
                if (cells[index].table_cell && cells[index].table_cell->row == row &&
                    cells[index].table_cell->column == column)
                    return index;
            return cells.size();
        };
        PresentationEditCommand structure;
        structure.action = PresentationEditAction::MergeTableCell;
        structure.table_direction = "right";
        structure.shape_index = cell_index(structured, 0, 0);
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::None,
            "structure fixture merges the first row");
        const auto& merged_options =
            structured.slides[0].shapes[cell_index(structured, 0, 0)].table_cell->structure_options;
        const auto& clear_options =
            structured.slides[0].shapes[cell_index(structured, 1, 2)].table_cell->structure_options;
        check(merged_options.insert_row && !merged_options.insert_column && !merged_options.delete_row &&
                !merged_options.delete_column && !merged_options.merge_right && !merged_options.merge_down &&
                clear_options.insert_row && clear_options.insert_column && clear_options.delete_row &&
                clear_options.delete_column && clear_options.merge_down,
            "structure capabilities distinguish merged and unaffected rows and columns");
        structure.action = PresentationEditAction::InsertTableColumn;
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::ReadOnly,
            "inserting through a merged region is rejected before package mutation");
        structure.action = PresentationEditAction::DeleteTableRow;
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::ReadOnly,
            "deleting a merged row is rejected before package mutation");
        structure.action = PresentationEditAction::InsertTableRow;
        structure.shape_index = cell_index(structured, 1, 2);
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::None &&
                structured.slides[0].shapes[cell_index(structured, 0, 0)].table_cell->column_span == 2 &&
                structured.slides[0].shapes[cell_index(structured, 2, 2)].table_cell->row_count == 4,
            "row insertion outside a merge preserves the merged header");
        structure.action = PresentationEditAction::DeleteTableRow;
        structure.shape_index = cell_index(structured, 2, 2);
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::None &&
                structured.slides[0].shapes[cell_index(structured, 0, 0)].table_cell->column_span == 2 &&
                structured.slides[0].shapes[cell_index(structured, 1, 2)].table_cell->row_count == 3,
            "row deletion outside a merge preserves the merged header");
        structure.action = PresentationEditAction::InsertTableColumn;
        structure.shape_index = cell_index(structured, 1, 2);
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::None &&
                structured.slides[0].shapes[cell_index(structured, 1, 3)].table_cell->column_count == 4,
            "column insertion outside a merge preserves the merged header");
        structure.action = PresentationEditAction::DeleteTableColumn;
        structure.shape_index = cell_index(structured, 1, 3);
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::None &&
                structured.slides[0].shapes[cell_index(structured, 0, 0)].table_cell->column_span == 2 &&
                structured.slides[0].shapes[cell_index(structured, 1, 2)].table_cell->column_count == 3,
            "column deletion outside a merge preserves the merged header");
        structure.action = PresentationEditAction::MergeTableCell;
        structure.table_direction = "right";
        structure.shape_index = cell_index(structured, 1, 0);
        check(apply_presentation_edit(structured, structure).error == PresentationEditError::None &&
                structured.slides[0].shapes[cell_index(structured, 1, 0)].table_cell->column_span == 2 &&
                structured.slides[0].shapes[cell_index(structured, 0, 0)].table_cell->column_span == 2,
            "a second nonoverlapping merge is allowed");
        const auto structured_saved = serialize_presentation(structured);
        const auto structured_readback = parse_presentation(structured_saved.parts);
        check(structured_saved.error == PresentationError::None &&
                structured_readback.error == PresentationError::None &&
                structured_readback.scene.slides[0]
                        .shapes[cell_index(structured_readback.scene, 0, 0)]
                        .table_cell->column_span == 2 &&
                structured_readback.scene.slides[0]
                        .shapes[cell_index(structured_readback.scene, 1, 0)]
                        .table_cell->column_span == 2,
            "independent merges and structural edits survive save and reopen");
        auto vertical = make_presentation(PresentationSlideLayout::Blank);
        vertical.native_editable = true;
        check(apply_presentation_edit(vertical, insert_structure).error == PresentationEditError::None,
            "vertical structure fixture creates a three by three table");
        structure.action = PresentationEditAction::MergeTableCell;
        structure.table_direction = "down";
        structure.shape_index = cell_index(vertical, 0, 0);
        check(apply_presentation_edit(vertical, structure).error == PresentationEditError::None,
            "vertical structure fixture merges the first column");
        const auto vertical_options =
            vertical.slides[0].shapes[cell_index(vertical, 0, 0)].table_cell->structure_options;
        check(!vertical_options.insert_row && vertical_options.insert_column &&
                !vertical_options.delete_row && !vertical_options.delete_column,
            "vertical span blocks intersecting row edits but leaves a safe column insertion");
        structure.action = PresentationEditAction::InsertTableRow;
        check(apply_presentation_edit(vertical, structure).error == PresentationEditError::ReadOnly,
            "row insertion through a vertical span is rejected");
        structure.action = PresentationEditAction::InsertTableColumn;
        check(apply_presentation_edit(vertical, structure).error == PresentationEditError::None &&
                vertical.slides[0].shapes[cell_index(vertical, 0, 0)].table_cell->row_span == 2 &&
                vertical.slides[0].shapes[cell_index(vertical, 0, 2)].table_cell->column_count == 4,
            "column insertion beside a vertical span preserves its row merge");
        parsed.scene.native_editable = true;
        PresentationEditCommand command;
        command.action = PresentationEditAction::DeleteShape;
        check(apply_presentation_edit(parsed.scene, command).error != PresentationEditError::None,
            "table cannot be damaged through shape editing");
        const auto copy = serialize_presentation(parsed.scene);
        for (const auto& part : original)
        {
            const auto found = std::find_if(copy.parts.begin(), copy.parts.end(), [&](const auto& candidate)
            {
                return candidate.path == part.path;
            });
            check(found != copy.parts.end() && found->bytes == part.bytes,
                "table source package remains byte exact");
        }
        auto edited = parse_presentation(original);
        check(edited.error == PresentationError::None, "table edit fixture parses");
        edited.scene.native_editable = true;
        PresentationEditCommand fill;
        fill.action = PresentationEditAction::FormatTableCell;
        fill.fill_color = "#3377AA";
        fill.fill_opacity = 0.6;
        check(apply_presentation_edit(edited.scene, fill).error == PresentationEditError::None,
            "table cell fill is editable");
        check(edited.scene.slides[0].shapes[0].fill.color == "#3377AA" &&
                edited.scene.slides[0].shapes[0].fill.opacity == 0.6,
            "table cell fill updates the model");
        fill.fill_color = "invalid";
        check(apply_presentation_edit(edited.scene, fill).error == PresentationEditError::InvalidValue,
            "invalid table cell color is rejected");
        const auto saved = serialize_presentation(edited.scene);
        check(saved.error == PresentationError::None, "table cell edit serializes");
        const auto reopened = parse_presentation(saved.parts);
        check(reopened.error == PresentationError::None &&
                reopened.scene.slides[0].shapes[0].fill.color == "#3377AA" &&
                reopened.scene.slides[0].shapes[0].fill.opacity == 0.6,
            "table cell fill survives save and reopen");
        for (const auto& part : original)
        {
            if (part.path == "ppt/slides/slide1.xml")
                continue;
            const auto found = std::find_if(saved.parts.begin(), saved.parts.end(), [&](const auto& candidate)
            {
                return candidate.path == part.path;
            });
            check(found != saved.parts.end() && found->bytes == part.bytes,
                "table edit preserves unrelated package parts");
        }
        auto restored = parse_presentation(original);
        check(restored.error == PresentationError::None, "table fill reset fixture parses");
        restored.scene.native_editable = true;
        PresentationEditCommand reset;
        reset.action = PresentationEditAction::ResetTableCellFill;
        reset.shape_index = 1;
        check(restored.scene.slides[0].shapes[1].table_cell->local_fill_override &&
                restored.scene.slides[0].shapes[1].table_cell->inherited_fill.color == "#EFEFEF",
            "table cell tracks direct fill and inherited table style");
        check(apply_presentation_edit(restored.scene, reset).error == PresentationEditError::None,
            "table cell can restore inherited fill");
        check(restored.scene.slides[0].shapes[1].fill.color == "#EFEFEF" &&
                !restored.scene.slides[0].shapes[1].table_cell->local_fill_override &&
                restored.scene.slides[0].shapes[0].fill.color == "#2244AA",
            "restoring one cell preserves other cells and reveals table style");
        check(apply_presentation_edit(restored.scene, reset).error == PresentationEditError::ReadOnly,
            "table cell without direct fill cannot restore again");
        const auto restored_package = serialize_presentation(restored.scene);
        const auto restored_readback = parse_presentation(restored_package.parts);
        check(restored_package.error == PresentationError::None &&
                restored_readback.error == PresentationError::None &&
                restored_readback.scene.slides[0].shapes[1].fill.color == "#EFEFEF",
            "restored table style survives save and reopen");
        for (const auto& part : original)
        {
            if (part.path == "ppt/slides/slide1.xml")
                continue;
            const auto found = std::find_if(restored_package.parts.begin(), restored_package.parts.end(),
                [&](const auto& candidate)
            {
                return candidate.path == part.path;
            });
            check(found != restored_package.parts.end() && found->bytes == part.bytes,
                "table fill reset preserves unrelated package parts");
        }
        auto borders = parse_presentation(original);
        check(borders.error == PresentationError::None, "table border reset fixture parses");
        borders.scene.native_editable = true;
        PresentationEditCommand border;
        border.action = PresentationEditAction::FormatTableBorder;
        border.shape_index = 1;
        border.outline_color = "#123456";
        border.outline_width = 2;
        PresentationEditCommand reset_border;
        reset_border.action = PresentationEditAction::ResetTableBorder;
        reset_border.shape_index = 1;
        check(apply_presentation_edit(borders.scene, reset_border).error == PresentationEditError::ReadOnly,
            "table border without direct override cannot reset");
        check(apply_presentation_edit(borders.scene, border).error == PresentationEditError::None &&
                borders.scene.slides[0].shapes[1].table_cell->local_border_override &&
                borders.scene.slides[0].shapes[1].table_cell->border_color == "#123456" &&
                borders.scene.slides[0].shapes[1].table_cell->border_width == 2,
            "table border edit records a direct uniform override");
        check(apply_presentation_edit(borders.scene, reset_border).error == PresentationEditError::None &&
                !borders.scene.slides[0].shapes[1].table_cell->local_border_override &&
                borders.scene.slides[0].shapes[1].fill.color == "#FF0000",
            "table border reset preserves selected cell fill");
        check(std::any_of(borders.scene.slides[0].shapes.begin(), borders.scene.slides[0].shapes.end(),
                  [](const auto& shape)
        {
            return shape.geometry == "line" && shape.outline_color == "#000000";
        }) &&
                std::none_of(borders.scene.slides[0].shapes.begin(), borders.scene.slides[0].shapes.end(),
                    [](const auto& shape)
        {
            return shape.geometry == "line" && shape.outline_color == "#123456";
        }),
            "table style borders reappear after direct lines are removed");
        check(apply_presentation_edit(borders.scene, reset_border).error == PresentationEditError::ReadOnly,
            "restored table border cannot reset again");
        const auto border_package = serialize_presentation(borders.scene);
        const auto border_readback = parse_presentation(border_package.parts);
        check(border_package.error == PresentationError::None &&
                border_readback.error == PresentationError::None &&
                !border_readback.scene.slides[0].shapes[1].table_cell->local_border_override,
            "table border restoration survives save and reopen");
        for (const auto& part : original)
        {
            if (part.path == "ppt/slides/slide1.xml")
                continue;
            const auto found = std::find_if(border_package.parts.begin(), border_package.parts.end(),
                [&](const auto& candidate)
            {
                return candidate.path == part.path;
            });
            check(found != border_package.parts.end() && found->bytes == part.bytes,
                "table border reset preserves unrelated package parts");
        }
        auto sides = parse_presentation(original);
        check(sides.error == PresentationError::None, "table edge fixture parses");
        sides.scene.native_editable = true;
        border.table_edge = "diagonal";
        const auto unchanged = serialize_presentation(sides.scene).parts;
        const auto rejected = apply_presentation_edit(sides.scene, border);
        const auto after_rejected = serialize_presentation(sides.scene).parts;
        check(rejected.error == PresentationEditError::InvalidValue &&
                after_rejected.size() == unchanged.size() &&
                std::equal(unchanged.begin(), unchanged.end(), after_rejected.begin(),
                    [](const auto& left, const auto& right)
        {
            return left.path == right.path && left.bytes == right.bytes;
        }),
            "unknown table edge is rejected without package mutation");
        border.table_edge = "left";
        check(apply_presentation_edit(sides.scene, border).error == PresentationEditError::None &&
                sides.scene.slides[0].shapes[1].table_cell->border_edges[0].local_override &&
                !sides.scene.slides[0].shapes[1].table_cell->border_edges[1].local_override &&
                sides.scene.slides[0].shapes[1].table_cell->border_edges[0].color == "#123456",
            "editing left table edge leaves top edge inherited");
        border.table_edge = "top";
        border.outline_color = "#654321";
        check(apply_presentation_edit(sides.scene, border).error == PresentationEditError::None &&
                sides.scene.slides[0].shapes[1].table_cell->border_edges[0].color == "#123456" &&
                sides.scene.slides[0].shapes[1].table_cell->border_edges[1].color == "#654321" &&
                sides.scene.slides[0].shapes[1].table_cell->border_color.empty(),
            "different table edges keep independent colors");
        reset_border.table_edge = "right";
        check(apply_presentation_edit(sides.scene, reset_border).error == PresentationEditError::ReadOnly,
            "inherited right edge cannot reset while other edges are direct");
        reset_border.table_edge = "left";
        check(apply_presentation_edit(sides.scene, reset_border).error == PresentationEditError::None &&
                !sides.scene.slides[0].shapes[1].table_cell->border_edges[0].local_override &&
                sides.scene.slides[0].shapes[1].table_cell->border_edges[1].local_override &&
                sides.scene.slides[0].shapes[1].table_cell->local_border_override,
            "resetting left edge preserves direct top edge");
        const auto side_package = serialize_presentation(sides.scene);
        const auto side_readback = parse_presentation(side_package.parts);
        check(side_package.error == PresentationError::None &&
                side_readback.error == PresentationError::None &&
                side_readback.scene.slides[0].shapes[1].table_cell->border_edges[1].color == "#654321" &&
                !side_readback.scene.slides[0].shapes[1].table_cell->border_edges[0].local_override,
            "individual table edge edits survive save and reopen");
        reset_border.table_edge = "all";
        check(apply_presentation_edit(sides.scene, reset_border).error == PresentationEditError::None &&
                !sides.scene.slides[0].shapes[1].table_cell->local_border_override,
            "all-edge reset removes remaining direct lines");

        auto text_parts = test_fixture::table_package();
        for (auto& part : text_parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            const std::string old_run = "<a:rPr sz='1200'/><a:t>B2";
            const auto position = part.bytes.find(old_run);
            check(position != std::string::npos, "table text fixture locates target cell");
            if (position != std::string::npos)
                part.bytes.replace(position, old_run.size(),
                    "<a:rPr sz='2400'><a:latin typeface='Arial'/><a:solidFill>"
                    "<a:srgbClr val='AA5500'/></a:solidFill></a:rPr><a:t>B2");
        }
        auto text_scene = parse_presentation(text_parts);
        check(text_scene.error == PresentationError::None, "table text inheritance fixture parses");
        if (text_scene.error != PresentationError::None)
            return 1;
        text_scene.scene.native_editable = true;
        const auto& direct = text_scene.scene.slides[0].shapes[2].text.paragraphs[0].runs[0];
        const auto overrides = presentation_text_local_overrides(text_scene.scene.slides[0].shapes[2]);
        const auto actions = presentation_edit_capabilities(text_scene.scene.slides[0].shapes[2]);
        check(direct.font_family == "Arial" && direct.font_size == 24 && direct.color == "#AA5500" &&
                overrides.font_family && overrides.font_size && overrides.color &&
                std::find(actions.begin(), actions.end(), PresentationEditAction::ResetTextInheritance) !=
                    actions.end(),
            "table cell exposes its three direct text overrides");
        PresentationEditCommand reset_text;
        reset_text.action = PresentationEditAction::ResetTextInheritance;
        reset_text.shape_index = 2;
        reset_text.reset_text_property = PresentationTextProperty::Color;
        check(apply_presentation_edit(text_scene.scene, reset_text).error == PresentationEditError::None,
            "table cell restores inherited text color");
        const auto& color_run = text_scene.scene.slides[0].shapes[2].text.paragraphs[0].runs[0];
        check(color_run.color == "#112233" && color_run.color_source == "tableStyle" &&
                color_run.font_family == "Arial" && color_run.font_size == 24 &&
                text_scene.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].color == "#FFFFFF",
            "color reset changes only the selected cell and preserves font and size");
        reset_text.reset_text_property = PresentationTextProperty::FontFamily;
        check(apply_presentation_edit(text_scene.scene, reset_text).error == PresentationEditError::None &&
                !presentation_text_local_overrides(text_scene.scene.slides[0].shapes[2]).font_family,
            "table cell restores inherited font family");
        reset_text.reset_text_property = PresentationTextProperty::FontSize;
        check(apply_presentation_edit(text_scene.scene, reset_text).error == PresentationEditError::None &&
                !presentation_text_local_overrides(text_scene.scene.slides[0].shapes[2]).font_size,
            "table cell restores inherited font size");
        const auto text_saved = serialize_presentation(text_scene.scene);
        const auto text_readback = parse_presentation(text_saved.parts);
        check(text_saved.error == PresentationError::None && text_readback.error == PresentationError::None &&
                text_readback.scene.slides[0].shapes[2].text.paragraphs[0].runs[0].color == "#112233",
            "table text inheritance survives save and reopen");
        check(apply_presentation_edit(text_scene.scene, reset_text).error == PresentationEditError::ReadOnly,
            "table cell refuses reset without direct size override");
        for (const auto& part : text_parts)
        {
            if (part.path == "ppt/slides/slide1.xml")
                continue;
            const auto found =
                std::find_if(text_saved.parts.begin(), text_saved.parts.end(), [&](const auto& candidate)
            {
                return candidate.path == part.path;
            });
            check(found != text_saved.parts.end() && found->bytes == part.bytes,
                "table text reset preserves unrelated package parts");
        }

        auto linked_parts = test_fixture::table_package();
        for (auto& part : linked_parts)
        {
            if (part.path != "ppt/tableStyles.xml")
                continue;
            const auto replace_once = [&](const std::string& before, const std::string& after)
            {
                const auto position = part.bytes.find(before);
                check(position != std::string::npos, "linked table style fixture locates color");
                if (position != std::string::npos)
                    part.bytes.replace(position, before.size(), after);
            };
            replace_once("<a:srgbClr val='112233'/>", "<a:fontRef idx='major'/><a:schemeClr val='accent1'/>");
            replace_once("<a:srgbClr val='EFEFEF'/>", "<a:schemeClr val='accent2'/>");
            replace_once("<a:srgbClr val='2244AA'/>", "<a:schemeClr val='accent4'/>");
            replace_once("<a:srgbClr val='000000'/>", "<a:schemeClr val='accent3'/>");
        }
        auto linked = parse_presentation(linked_parts);
        check(linked.error == PresentationError::None, "theme-linked table style parses");
        if (linked.error == PresentationError::None)
        {
            linked.scene.native_editable = true;
            const auto original_parts = serialize_presentation(linked.scene).parts;
            PresentationEditCommand theme;
            theme.action = PresentationEditAction::ApplyTheme;
            theme.theme_colors = {{"accent1", "#123456"}, {"accent2", "#E2E3E4"}, {"accent3", "#334455"},
                {"accent4", "#667788"}};
            theme.theme_fonts = {{"majorLatin", "Georgia"}};
            check(apply_presentation_edit(linked.scene, theme).error == PresentationEditError::None,
                "theme edit updates linked table style");
            const auto& themed = linked.scene.slides[0].shapes;
            check(themed[0].fill.color == "#667788" && themed[1].fill.color == "#FF0000" &&
                    themed[2].fill.color == "#E2E3E4" &&
                    themed[2].text.paragraphs[0].runs[0].color == "#123456" &&
                    themed[2].text.paragraphs[0].runs[0].font_family == "Georgia" &&
                    themed[2].table_cell->border_edges[1].color == "#334455",
                "table fill, text, font and border follow theme while direct cell fill stays fixed");
            const auto linked_saved = serialize_presentation(linked.scene);
            const auto readback = parse_presentation(linked_saved.parts);
            check(linked_saved.error == PresentationError::None &&
                    readback.error == PresentationError::None &&
                    readback.scene.slides[0].shapes[2].fill.color == "#E2E3E4" &&
                    readback.scene.slides[0].shapes[2].text.paragraphs[0].runs[0].color == "#123456",
                "theme-linked table survives save and reopen");
            for (const auto& part : original_parts)
            {
                if (part.path == "ppt/theme/theme1.xml")
                    continue;
                const auto found = std::find_if(linked_saved.parts.begin(), linked_saved.parts.end(),
                    [&](const auto& candidate)
                {
                    return candidate.path == part.path;
                });
                check(found != linked_saved.parts.end() && found->bytes == part.bytes,
                    "theme edit preserves non-theme table and slide parts byte exact");
            }
        }
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_table_tests();
}
