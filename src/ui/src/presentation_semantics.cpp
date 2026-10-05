#include "presentation_semantics.hpp"

#include <mirrorfly/presentation_geometry.hpp>

#include <QStringList>

#include <algorithm>
#include <map>

namespace
{
    using A = mirrorfly::PresentationEditAction;
    const std::map<A, QString> names{{A::UpdateText, "updateText"}, {A::FormatText, "formatText"},
        {A::FormatTextStyle, "formatTextStyle"}, {A::FormatParagraph, "formatParagraph"},
        {A::FormatTextBox, "formatTextBox"}, {A::FormatShape, "formatShape"},
        {A::ResetPlaceholderFill, "resetPlaceholderFill"}, {A::FormatTableCell, "formatTableCell"},
        {A::ResetTableCellFill, "resetTableCellFill"}, {A::FormatTableStyle, "formatTableStyle"},
        {A::ResetPlaceholderOutline, "resetPlaceholderOutline"},
        {A::ResetTextInheritance, "resetTextInheritance"}, {A::InsertTableRow, "insertTableRow"},
        {A::InsertTableColumn, "insertTableColumn"}, {A::DeleteTableRow, "deleteTableRow"},
        {A::DeleteTableColumn, "deleteTableColumn"}, {A::MergeTableCell, "mergeTableCell"},
        {A::UnmergeTableCell, "unmergeTableCell"}, {A::FormatTableBorder, "formatTableBorder"},
        {A::ResetTableBorder, "resetTableBorder"}, {A::FormatImage, "formatImage"},
        {A::ReplaceImage, "replaceImage"}, {A::TransformShape, "transformShape"},
        {A::AlignShape, "alignShape"}, {A::DuplicateShape, "duplicateShape"}, {A::DeleteShape, "deleteShape"},
        {A::MoveShape, "moveShape"}, {A::GroupAdjacent, "groupAdjacent"},
        {A::SetClickAction, "setClickAction"}, {A::ApplyFormat, "applyFormat"},
        {A::ReplaceTextMatches, "replaceTextMatches"}};

    QVariantList matrix(const std::array<double, 6>& source)
    {
        QVariantList result;
        for (double value : source)
            result.append(value);
        return result;
    }

    QVariantMap table_border_edges(const mirrorfly::PresentationTableCell& cell)
    {
        QVariantMap result;
        const char* edge_names[]{"left", "top", "right", "bottom"};
        for (std::size_t index = 0; index < cell.border_edges.size(); ++index)
        {
            const auto& edge = cell.border_edges[index];
            result.insert(QString::fromLatin1(edge_names[index]),
                QVariantMap{{"color", QString::fromStdString(edge.color)}, {"opacity", edge.opacity},
                    {"width", edge.width}, {"localOverride", edge.local_override}});
        }
        return result;
    }
}

namespace mirrorfly
{
    QVariantMap presentation_theme_state_map(const PresentationScene& scene, std::size_t slide_index)
    {
        const auto state = presentation_theme_state(scene, slide_index);
        QVariantMap colors;
        QVariantMap fonts;
        QVariantList editable_colors;
        QVariantList editable_fonts;
        for (const auto& [slot, color] : state.colors)
            colors.insert(QString::fromStdString(slot), QString::fromStdString(color));
        for (const auto& [slot, family] : state.fonts)
            fonts.insert(QString::fromStdString(slot), QString::fromStdString(family).left(128));
        for (const auto& slot : state.editable_color_slots)
            editable_colors.append(QString::fromStdString(slot));
        for (const auto& slot : state.editable_font_slots)
            editable_fonts.append(QString::fromStdString(slot));
        const bool editable = scene.native_editable &&
            (!state.editable_color_slots.empty() || !state.editable_font_slots.empty());
        return {{"available", state.available}, {"editable", editable}, {"authored", state.authored},
            {"name", QString::fromStdString(state.name).left(128)},
            {"linkedSlideCount", static_cast<int>(state.linked_slide_count)}, {"colors", colors},
            {"fonts", fonts}, {"editableColorSlots", editable_colors}, {"editableFontSlots", editable_fonts}};
    }

    QVariantMap presentation_table_style_options(const PresentationTableStyleOptions& options)
    {
        return {{"firstRow", options.first_row}, {"lastRow", options.last_row},
            {"firstColumn", options.first_column}, {"lastColumn", options.last_column},
            {"bandRows", options.band_rows}, {"bandColumns", options.band_columns}};
    }

    QVariantMap presentation_table_structure_options(const PresentationTableStructureOptions& options)
    {
        return {{"insertRow", options.insert_row}, {"insertColumn", options.insert_column},
            {"deleteRow", options.delete_row}, {"deleteColumn", options.delete_column},
            {"mergeRight", options.merge_right}, {"mergeDown", options.merge_down}};
    }

    QVariantList presentation_capability_names(const PresentationShape& shape)
    {
        QVariantList result;
        for (const auto action : presentation_edit_capabilities(shape))
            if (const auto found = names.find(action); found != names.end())
                result.append(found->second);
        return result;
    }

    std::array<int, 2> presentation_group_targets(const PresentationSlide& slide, int selected_index)
    {
        std::array<int, 2> result{-1, -1};
        if (selected_index < 0 || selected_index >= static_cast<int>(slide.shapes.size()))
            return result;
        const auto& selected = slide.shapes[static_cast<std::size_t>(selected_index)];
        if (!selected.editable || selected.table_cell || selected.source_part != slide.source_part)
            return result;
        const auto ordinary = [&](int index)
        {
            if (index < 0 || index >= static_cast<int>(slide.shapes.size()))
                return false;
            const auto& shape = slide.shapes[static_cast<std::size_t>(index)];
            return shape.editable && !shape.table_cell && shape.source_groups.empty() &&
                shape.source_part == slide.source_part;
        };
        int first = selected_index;
        int last = selected_index;
        if (!selected.source_groups.empty())
        {
            const auto& group_id = selected.source_groups.back();
            const auto frame = std::find_if(slide.groups.begin(), slide.groups.end(), [&](const auto& item)
            {
                return item.source_id == group_id && item.source_part == slide.source_part && item.editable &&
                    item.extensible;
            });
            if (frame == slide.groups.end())
                return result;
            for (int index = 0; index < static_cast<int>(slide.shapes.size()); ++index)
            {
                const auto& shape = slide.shapes[static_cast<std::size_t>(index)];
                if (shape.source_part == slide.source_part && !shape.source_groups.empty() &&
                    shape.source_groups.back() == group_id)
                {
                    first = std::min(first, index);
                    last = std::max(last, index);
                }
            }
        }
        if (ordinary(first - 1))
            result[0] = first - 1;
        if (ordinary(last + 1))
            result[1] = last + 1;
        return result;
    }

    QVariantList presentation_selection_actions(const PresentationSlide& slide, int selected_index)
    {
        if (selected_index < 0 || selected_index >= static_cast<int>(slide.shapes.size()))
            return {};
        const auto& shape = slide.shapes[static_cast<std::size_t>(selected_index)];
        auto actions = presentation_capability_names(shape);
        const auto targets = presentation_group_targets(slide, selected_index);
        const bool available = targets[0] >= 0 || targets[1] >= 0;
        if (!available)
            actions.removeAll(QStringLiteral("groupAdjacent"));
        if (available && !shape.source_groups.empty())
            actions.append(QStringLiteral("addToGroup"));
        return actions;
    }

    QVariantMap presentation_semantic_tree(const PresentationScene& scene, int page, int offset, int limit)
    {
        if (page < 0 || page >= static_cast<int>(scene.slides.size()) || offset < 0 || limit < 1 ||
            limit > 64)
            return {{"ok", false}, {"error", "invalid_index"}};
        const auto& slide = scene.slides[page];
        if (offset > static_cast<int>(slide.shapes.size()))
            return {{"ok", false}, {"error", "invalid_index"}};
        const QString root = "slide:" + QString::number(page);
        QVariantList nodes;
        std::map<QString, QVariantMap> groups;
        const int end = std::min(offset + limit, static_cast<int>(slide.shapes.size()));
        for (int index = offset; index < end; ++index)
        {
            const auto& shape = slide.shapes[index];
            QString parent = root;
            for (auto group = shape.source_groups.rbegin(); group != shape.source_groups.rend(); ++group)
            {
                const QString key = root + ":group:" + QString::fromStdString(shape.source_part) + ":" +
                    QString::fromStdString(*group);
                const auto frame =
                    std::find_if(slide.groups.begin(), slide.groups.end(), [&](const auto& item)
                {
                    return item.source_id == *group && item.source_part == slide.source_part;
                });
                const bool movable = frame != slide.groups.end() && frame->editable;
                const bool ungroupable = movable && frame->ungroupable;
                const bool extensible = movable && frame->extensible;
                PresentationGroupLayerOptions layer;
                if (frame != slide.groups.end())
                    layer = presentation_group_layer_options(*frame);
                QVariantList group_actions;
                if (movable)
                    group_actions.append("moveGroup");
                if (ungroupable)
                    group_actions.append("ungroup");
                if (layer.back || layer.forward)
                    group_actions.append("reorderGroup");
                const auto targets = !shape.source_groups.empty() && shape.source_groups.back() == *group
                    ? presentation_group_targets(slide, index)
                    : std::array<int, 2>{-1, -1};
                QVariantList adjacent_targets;
                for (const int target : targets)
                    if (target >= 0)
                        adjacent_targets.append(target);
                if (!adjacent_targets.isEmpty())
                    group_actions.append("addToGroup");
                groups.emplace(key,
                    QVariantMap{{"id", key}, {"parentId", parent},
                        {"type",
                            shape.table_cell && *group == shape.table_cell->frame_id ? "table" : "group"},
                        {"sourceId", QString::fromStdString(*group)},
                        {"groupId", QString::fromStdString(*group)}, {"actions", group_actions},
                        {"adjacentTargets", adjacent_targets}, {"extensible", extensible},
                        {"layerOptions",
                            QVariantMap{{"back", layer.back}, {"backward", layer.backward},
                                {"forward", layer.forward}, {"front", layer.front}}},
                        {"layerIndex", frame != slide.groups.end() ? frame->layer_index : -1},
                        {"layerCount", frame != slide.groups.end() ? frame->layer_count : 0},
                        {"writable", scene.native_editable && movable}});
                parent = key;
            }
            QString text;
            int runs = 0;
            const PresentationRun* first_run = nullptr;
            bool mixed_text_sources = false;
            for (const auto& paragraph : shape.text.paragraphs)
            {
                if (!text.isEmpty() && text.size() < 1024)
                    text += '\n';
                for (const auto& run : paragraph.runs)
                {
                    if (!first_run)
                        first_run = &run;
                    else if (run.font_family_source != first_run->font_family_source ||
                        run.east_asian_font_source != first_run->east_asian_font_source ||
                        run.font_size_source != first_run->font_size_source ||
                        run.color_source != first_run->color_source)
                        mixed_text_sources = true;
                    ++runs;
                    if (text.size() < 1024)
                        text += QString::fromStdString(run.text).left(1024 - text.size());
                }
            }
            const auto actions = presentation_selection_actions(slide, index);
            QString type = "shape";
            if (shape.table_cell)
                type = "tableCell";
            else if (!shape.media_path.empty())
                type = "media";
            else if (!shape.image_path.empty())
                type = "image";
            else if (!shape.text.paragraphs.empty())
                type = "text";
            QString reason;
            if (!shape.editable)
                reason = "unsupported_or_inherited_structure";
            else if (!scene.native_editable)
                reason = "editable_copy_required";
            QVariantMap node{{"id", QString::number(shape.id)}, {"parentId", parent}, {"type", type},
                {"index", index}, {"name", QString::fromStdString(shape.name).left(256)},
                {"sourceId", QString::fromStdString(shape.source_id)},
                {"sourcePart", QString::fromStdString(shape.source_part)},
                {"transform", matrix(shape.transform)}, {"width", shape.width}, {"height", shape.height},
                {"text", text}, {"textMayBeTruncated", text.size() >= 1024}, {"textRunCount", runs},
                {"actions", actions}, {"writable", scene.native_editable && !actions.isEmpty()},
                {"lockedReason", reason}};
            node.insert("effectStyle",
                QVariantMap{{"source", QString::fromStdString(shape.effects_source)},
                    {"themeIndex", static_cast<int>(shape.theme_effect_style_index)},
                    {"directOverride", shape.effects_source == "direct"}});
            QVariantMap style{{"fillColor", QString::fromStdString(shape.fill.color)},
                {"fillOpacity", shape.fill.opacity},
                {"fillPattern", QString::fromStdString(shape.fill.pattern)},
                {"patternForegroundColor", QString::fromStdString(shape.fill.pattern_foreground_color)},
                {"patternBackgroundColor", QString::fromStdString(shape.fill.color)},
                {"patternFillSupported", presentation_pattern_supported(shape.fill.pattern)},
                {"lineDash", QString::fromStdString(presentation_line_dash_name(shape.line_style.dashes))},
                {"outlineColor", QString::fromStdString(shape.outline_color)},
                {"outlineWidth", shape.outline_width}};
            if (first_run)
            {
                style.insert("fontSize", first_run->font_size);
                style.insert("fontFamily", QString::fromStdString(first_run->font_family));
                style.insert("textColor", QString::fromStdString(first_run->color));
                style.insert("bold", first_run->bold);
                style.insert("italic", first_run->italic);
                style.insert("textScope", "firstRun; paragraphInfo for other runs");
                node.insert("textStyleSources",
                    QVariantMap{{"fontFamily", QString::fromStdString(first_run->font_family_source)},
                        {"eastAsianFont", QString::fromStdString(first_run->east_asian_font_source)},
                        {"fontSize", QString::fromStdString(first_run->font_size_source)},
                        {"color", QString::fromStdString(first_run->color_source)}, {"scope", "firstRun"},
                        {"mixed", mixed_text_sources}});
                const auto local_text = presentation_text_local_overrides(shape);
                node.insert("textLocalOverrides",
                    QVariantMap{{"fontFamily", local_text.font_family}, {"fontSize", local_text.font_size},
                        {"color", local_text.color}, {"scope", "anyRun"}});
            }
            node.insert("style", style);
            if (shape.placeholder)
            {
                const auto& placeholder = *shape.placeholder;
                node.insert("placeholder",
                    QVariantMap{{"type", QString::fromStdString(placeholder.type)},
                        {"index", static_cast<int>(placeholder.index)}, {"hasLayout", placeholder.has_layout},
                        {"hasMaster", placeholder.has_master},
                        {"fillSource", QString::fromStdString(placeholder.fill_source)},
                        {"inheritedFillSource", QString::fromStdString(placeholder.inherited_fill_source)},
                        {"localFillOverride", placeholder.local_fill_override},
                        {"outlineSource", QString::fromStdString(placeholder.outline_source)},
                        {"inheritedOutlineSource",
                            QString::fromStdString(placeholder.inherited_outline_source)},
                        {"localOutlineOverride", placeholder.local_outline_override}});
            }
            if (shape.table_cell)
                node.insert("tableCellStyle",
                    QVariantMap{{"localFillOverride", shape.table_cell->local_fill_override},
                        {"inheritedFillColor",
                            QString::fromStdString(shape.table_cell->inherited_fill.color)},
                        {"localBorderOverride", shape.table_cell->local_border_override},
                        {"uniformBorderColor", QString::fromStdString(shape.table_cell->border_color)},
                        {"uniformBorderWidth", shape.table_cell->border_width},
                        {"edges", table_border_edges(*shape.table_cell)}});
            if (!shape.text.paragraphs.empty())
            {
                const int first_level = shape.text.paragraphs.front().list_level;
                const bool mixed_levels = std::any_of(shape.text.paragraphs.begin(),
                    shape.text.paragraphs.end(), [first_level](const auto& paragraph)
                {
                    return paragraph.list_level != first_level;
                });
                node.insert("paragraphCount", static_cast<int>(shape.text.paragraphs.size()));
                node.insert("listLevel", first_level);
                node.insert("mixedListLevels", mixed_levels);
                node.insert("paragraphQuery", "selectObject(id), then paragraphInfo(index)");
            }
            if (!shape.click_action.kind.empty())
            {
                QVariantMap click{{"kind", QString::fromStdString(shape.click_action.kind)}};
                if (shape.click_action.target_slide >= 0)
                    click.insert("targetSlide", shape.click_action.target_slide);
                node.insert("clickAction", click);
            }
            if (shape.table_cell)
            {
                node.insert("tableStyleAvailable", shape.table_cell->style_available);
                node.insert(
                    "tableStyleOptions", presentation_table_style_options(shape.table_cell->style_options));
                node.insert("tableStructureOptions",
                    presentation_table_structure_options(shape.table_cell->structure_options));
                node.insert("row", static_cast<int>(shape.table_cell->row));
                node.insert("column", static_cast<int>(shape.table_cell->column));
                node.insert("tableRowCount", static_cast<int>(shape.table_cell->row_count));
                node.insert("tableColumnCount", static_cast<int>(shape.table_cell->column_count));
                node.insert("tableHasMerges", shape.table_cell->has_merges);
                node.insert("tableRowSpan", static_cast<int>(shape.table_cell->row_span));
                node.insert("tableColumnSpan", static_cast<int>(shape.table_cell->column_span));
                node.insert("tableUnmergeable", shape.table_cell->unmergeable);
            }
            nodes.append(node);
        }
        const auto theme_info = presentation_theme_state_map(scene, static_cast<std::size_t>(page));
        QVariantList hierarchy;
        QVariantList document_actions;
        if (scene.native_editable)
        {
            document_actions.append("insertTable");
            document_actions.append("replaceTextMatches");
            if (theme_info.value("editable").toBool())
                document_actions.append("applyTheme");
            if (!scene.section_structure_locked)
                document_actions.append("createSection");
        }
        hierarchy.append(QVariantMap{{"id", "document"}, {"type", "presentation"}, {"parentId", ""},
            {"actions", document_actions}, {"writable", scene.native_editable}});
        for (const auto& section : scene.sections)
        {
            int first = -1;
            int count = 0;
            for (std::size_t index = 0; index < scene.slides.size(); ++index)
                if (scene.slides[index].section_id == section.id)
                {
                    if (first < 0)
                        first = static_cast<int>(index);
                    ++count;
                }
            hierarchy.append(
                QVariantMap{{"id", "section:" + QString::fromStdString(section.id)}, {"type", "section"},
                    {"parentId", "document"}, {"name", QString::fromStdString(section.name).left(128)},
                    {"firstSlide", first}, {"slideCount", count},
                    {"actions",
                        scene.native_editable && !scene.section_structure_locked
                            ? QVariantList{"renameSection", "removeSection"}
                            : QVariantList{}},
                    {"writable", scene.native_editable && !scene.section_structure_locked}});
        }
        const auto slide_parent = slide.section_id.empty()
            ? QStringLiteral("document")
            : "section:" + QString::fromStdString(slide.section_id);
        const QString speaker_notes = QString::fromStdString(slide.speaker_notes);
        const auto& transition = slide.transition;
        const QVariantMap transition_info{{"type", QString::fromStdString(transition.type)},
            {"direction", QString::fromStdString(transition.direction)},
            {"durationSeconds", transition.duration}, {"advanceOnClick", transition.advance_on_click},
            {"advanceAfterSeconds", transition.advance_after},
            {"editable", scene.native_editable && transition.editable},
            {"approximate", transition.approximate}};
        const QVariantList transition_actions = scene.native_editable && transition.editable
            ? QVariantList{"setSlideTransition"}
            : QVariantList{};
        hierarchy.append(QVariantMap{{"id", root}, {"type", "slide"}, {"parentId", slide_parent},
            {"title", QString::fromStdString(slide.title).left(256)}, {"width", scene.width},
            {"height", scene.height}, {"transition", transition_info}, {"theme", theme_info},
            {"actions", transition_actions}, {"writable", scene.native_editable && transition.editable},
            {"speakerNotes", speaker_notes.left(8192)},
            {"speakerNotesMayBeTruncated", speaker_notes.size() > 8192}});
        for (const auto& [id, node] : groups)
            hierarchy.append(node);
        hierarchy.append(nodes);
        return {{"ok", true}, {"schemaVersion", 1}, {"rootId", "document"},
            {"identityScope", "documentSession"}, {"units", "pt"}, {"indices", "zeroBased"},
            {"matrixConvention", "x=a*u+c*v+tx; y=b*u+d*v+ty"}, {"page", page},
            {"slideCount", static_cast<int>(scene.slides.size())}, {"offset", offset},
            {"nextOffset", end < static_cast<int>(slide.shapes.size()) ? end : -1},
            {"totalObjects", static_cast<int>(slide.shapes.size())}, {"nodes", hierarchy},
            {"textEditScope", "wholeObject"}, {"contentTrust", "untrustedDocumentData"},
            {"requiresRevision", true}, {"sectionStructureLocked", scene.section_structure_locked}};
    }

    QVariantMap presentation_edit_schema()
    {
        QStringList geometries;
        for (const auto& name : presentation_geometry_presets())
            geometries.append(QString::fromStdString(name));
        const QVariantMap fill{{"color", "#RRGGBB"}, {"opacity", "number [0,1]"},
            {"angle", "degrees [-360,360]"},
            {"stops", "empty or 2..16 sorted {position:[0,1],color:#RRGGBB,opacity:[0,1]}"}};
        return {{"schemaVersion", 1}, {"selection", "selectObject(id), then snapshot for a fresh revision"},
            {"execution", "applyEdit(action, options); replaceImage uses its dedicated file action"},
            {"search", "findText(query, caseSensitive, offset); 64 results per page with UTF-8 byte offsets"},
            {"speakerNotes",
                "speakerNotes(page) returns a read-only string; zero-based page index; "
                "semanticTree slide node includes speakerNotes and truncation flag"},
            {"effectStyle",
                "semanticTree object node has effectStyle.source=theme|direct|none, "
                "themeIndex=1-based effectStyleLst entry, directOverride=true for a local effect list; "
                "applyTheme changes theme-linked effects while direct effects remain fixed"},
            {"paragraphInfo",
                "after selectObject(id), paragraphInfo(index) returns one zero-based paragraph's "
                "listLevel, numbering, indentation and text preview; formatParagraph with "
                "paragraphIndex edits that paragraph, omitted paragraphIndex edits the whole object"},
            {"rehearsal",
                "rehearsal('start'|'stop') opens or closes the single-screen notes, "
                "clock and next-slide panel; UI state reports presentation.mode; "
                "playbackSnapshot.rehearsal reports elapsedSeconds and slide indices"},
            {"presenter",
                "presenter('start'|'stop') opens a second-screen audience window while the primary "
                "window shows a current-slide thumbnail, next slide, notes and clock; requires two "
                "screens; UI state presentation.mode='presenter' and playbackSnapshot.presenter "
                "reports active and audienceScreen; playbackSnapshot media, animation and transition "
                "belong to the audience window"},
            {"imageExport",
                "images.start(parentFolderUrl, {format:'png|jpg', scope:'all|current', "
                "longEdge:1280|1920}); watch modules.images.snapshot completed/total, busy, "
                "success and path; each run creates a new folder"},
            {"viewAids",
                "setGuideSettings(patch) updates snapshot.guideSettings immediately; pt units, session only; "
                "does not change PPTX generation or pendingEdits"},
            {"slideTransition",
                "semanticTree slide node.transition exposes type, direction, durationSeconds, "
                "advanceOnClick, advanceAfterSeconds and editable; setSlideTransition updates the "
                "current slide; wait for syncing=false and refresh the semantic tree"},
            {"themeState",
                "semanticTree slide node.theme and bridge themeState expose effective colors/fonts, "
                "editableColorSlots/editableFontSlots, shared slide count and editable flag; "
                "refresh after syncing=false"},
            {"progress",
                "snapshot returns generation, syncing, pendingEdits and loadingProgress; wait for "
                "syncing=false before save or undo"},
            {"scope",
                "wholeObject by default; formatParagraph.paragraphIndex targets one paragraph; "
                "omitted property groups are preserved"},
            {"coordinates", "slide points; transformShape x/y locate the transformed local origin"},
            {"addSlide",
                QVariantMap{{"layout",
                                "title|titleContent|twoColumns|blank|reportOutline|researchPlan|comparison|"
                                "references|conclusion|projectStatus|meetingSummary|milestones|"
                                "researchStudio|evidenceBoard|projectDashboard|deliveryRoadmap"},
                    {"before", "bool; default false"},
                    {"palette", "optional {ink,paper,card,muted,accent,soft}: #RRGGBB"}}},
            {"duplicateSlide", QVariantMap{}}, {"deleteSlide", QVariantMap{}},
            {"moveSlide", QVariantMap{{"offset", "-1|1 relative to current slide"}}},
            {"setClickAction",
                QVariantMap{{"kind",
                                "empty to clear | slide | firstslide | lastslide | nextslide | "
                                "previousslide | lastslideviewed | endshow"},
                    {"targetSlide", "zero-based index required when kind=slide"}}},
            {"applyFormat",
                QVariantMap{{"sourceId", "opaque document-session object ID from semanticTree"},
                    {"target", "currently selected editable object"},
                    {"scope",
                        "appearance and typography; preserves target text, geometry, links and image "
                        "bytes"}}},
            {"replaceTextMatches",
                QVariantMap{{"query", "required UTF-8 text, 1..256 bytes"},
                    {"replacement", "UTF-8 text, 0..4096 bytes; empty deletes matches"},
                    {"caseSensitive", "bool; default false; insensitive folding covers ASCII only"},
                    {"scope", "all|match; match uses a result from findText"},
                    {"expectedGeneration", "generation from findText; reject stale results"},
                    {"shapeId", "required for match; document-session ID from findText"},
                    {"slideIndex", "required for match; zero-based"},
                    {"shapeIndex", "required for match; zero-based"},
                    {"paragraphIndex", "required for match; zero-based"},
                    {"startByte", "required for match; UTF-8 byte offset within paragraph"},
                    {"progress", "snapshot.syncing and pendingEdits; refresh findText after completion"}}},
            {"createSection", QVariantMap{{"name", "UTF-8, 1..128 bytes; starts at current slide"}}},
            {"renameSection", QVariantMap{{"name", "UTF-8, 1..128 bytes; current slide's section"}}},
            {"removeSection", QVariantMap{{"scope", "merge current section into adjacent section"}}},
            {"addText",
                QVariantMap{{"text", "string"}, {"x", "pt"}, {"y", "pt"}, {"width", "pt"}, {"height", "pt"}}},
            {"addShape",
                QVariantMap{
                    {"geometry", geometries}, {"x", "pt"}, {"y", "pt"}, {"width", "pt"}, {"height", "pt"}}},
            {"slideHidden", QVariantMap{{"hidden", "bool"}}},
            {"setSlideTransition",
                QVariantMap{{"type", "cut|fade|push; required"},
                    {"direction", "l|r|u|d for push, empty otherwise; required"},
                    {"durationSeconds", "0.3|0.5|1.0; required"}, {"advanceOnClick", "bool; required"},
                    {"advanceAfterSeconds", "-1 disabled or 0..86400 seconds; required"},
                    {"availability", "current slide transition.editable must be true"},
                    {"progress", "asynchronous package sync; watch snapshot.syncing and pendingEdits"}}},
            {"background", QVariantMap{{"color", "#RRGGBB"}}},
            {"updateText",
                QVariantMap{{"text",
                    "UTF-8 text; whole-object replacement; hyperlinks and fields in replaced paragraphs are "
                    "replaced"}}},
            {"formatText",
                QVariantMap{{"fontFamily", "string"}, {"fontSize", "pt"}, {"bold", "bool"},
                    {"alignment", "left|center|right|justify"}, {"bullet", "bool"}, {"italic", "bool"},
                    {"underline", "bool"}, {"strike", "bool"}, {"textColor", "#RRGGBB"},
                    {"characterSpacing", "pt"}, {"baseline", "[-1,1]"}}},
            {"formatParagraph",
                QVariantMap{{"alignment", "left|center|right|justify"}, {"bullet", "bool"},
                    {"numbered", "bool"}, {"numberStart", "integer [1,32767]"},
                    {"listLevel", "zero-based level [0,8]; moves left margin by 24pt per level"},
                    {"paragraphIndex", "optional zero-based index; omitted edits all paragraphs"},
                    {"marginLeft", "pt; explicit value overrides level adjustment"},
                    {"firstLineIndent", "pt"}, {"lineSpacing", "ratio"}, {"spaceBefore", "pt"},
                    {"spaceAfter", "pt"}}},
            {"formatTextBox",
                QVariantMap{{"insetLeft", "pt"}, {"insetRight", "pt"}, {"insetTop", "pt"},
                    {"insetBottom", "pt"}, {"verticalAlignment", "top|center|bottom"}, {"wrap", "bool"},
                    {"autoFit", "bool"}}},
            {"transformShape",
                QVariantMap{{"x", "pt"}, {"y", "pt"}, {"width", "pt"}, {"height", "pt"},
                    {"rotation", "degrees"}, {"preserveAspect", "bool"}, {"flipHorizontal", "bool"},
                    {"flipVertical", "bool"}}},
            {"alignShape", QVariantMap{{"alignment", "left|center|right|top|middle|bottom"}}},
            {"formatImage",
                QVariantMap{{"cropLeft", "[0,1]"}, {"cropTop", "[0,1]"}, {"cropRight", "[0,1]"},
                    {"cropBottom", "[0,1]"}, {"opacity", "[0,1]"}}},
            {"formatTableCell",
                QVariantMap{{"fillColor", "#RRGGBB; required"},
                    {"fillOpacity", "[0,1]; optional; defaults to 1"},
                    {"scope", "selected editable table cell only; local fill override"}}},
            {"formatTableStyle",
                QVariantMap{{"style",
                                "required complete bool map: firstRow,lastRow,firstColumn,lastColumn,"
                                "bandRows,bandColumns"},
                    {"scope", "entire selected table with a linked style, including merged cells"},
                    {"effect",
                        "reparse style regions once; direct cell fill, border and text override remain"},
                    {"progress", "asynchronous reparse; wait for syncing=false"}}},
            {"moveGroup",
                QVariantMap{{"groupId", "source group ID from semantic tree"},
                    {"x", "horizontal delta in pt; one of x/y must be nonzero"},
                    {"y", "vertical delta in pt; one of x/y must be nonzero"},
                    {"scope", "top-level editable imported group; descendants move together"}}},
            {"reorderGroup",
                QVariantMap{{"groupId", "current top-level group ID from semantic tree; required"},
                    {"position", "back|backward|forward|front; required"},
                    {"availability", "group node.layerOptions uses the same four position names"},
                    {"effect", "move whole group as one layer; member order and content stay intact"},
                    {"progress", "asynchronous package sync; wait for syncing=false and refresh tree"}}},
            {"applyTheme",
                QVariantMap{{"colors", "optional map of theme slots to #RRGGBB; 1..12 entries"},
                    {"fonts", "optional map: majorLatin|minorLatin|majorEastAsian|minorEastAsian"},
                    {"slots", "dk1|lt1|dk2|lt2|accent1..accent6|hlink|folHlink"},
                    {"guiPath",
                        "page > theme > preset or custom color/font; each custom edit patches one slot"},
                    {"availability",
                        "use slide node.theme.editableColorSlots/editableFontSlots before editing"},
                    {"scope",
                        "current slide's linked theme; slide node.theme.linkedSlideCount previews scope"},
                    {"progress", "asynchronous reparse; wait until syncing=false"}}},
            {"insertTable",
                QVariantMap{{"rows", "integer 1..30"}, {"columns", "integer 1..20"}, {"x", "optional pt"},
                    {"y", "optional pt"}, {"width", "optional pt"}, {"height", "optional pt"},
                    {"scope", "new OOXML table on current slide"}}},
            {"insertTableRow",
                QVariantMap{{"scope", "insert below selected row when the boundary crosses no merge"},
                    {"availability", "selected tableStructureOptions.insertRow"},
                    {"progress", "asynchronous package sync; selection restored when still present"}}},
            {"insertTableColumn",
                QVariantMap{{"scope", "insert right of selected column when the boundary crosses no merge"},
                    {"availability", "selected tableStructureOptions.insertColumn"},
                    {"progress", "asynchronous package sync; selection restored when still present"}}},
            {"deleteTableRow",
                QVariantMap{{"scope", "remove selected row with no merged cell and at least two rows"},
                    {"availability", "selected tableStructureOptions.deleteRow"},
                    {"effect", "deletes row content and reduces table height; undoable"}}},
            {"deleteTableColumn",
                QVariantMap{{"scope", "remove selected column with no merged cell and at least two columns"},
                    {"availability", "selected tableStructureOptions.deleteColumn"},
                    {"effect", "deletes column content and reduces table width; undoable"}}},
            {"mergeTableCell",
                QVariantMap{{"direction", "right|down; adjacent cell must be empty"},
                    {"availability", "selected tableStructureOptions.mergeRight or mergeDown"},
                    {"scope", "selected and adjacent cells must be outside existing merge regions"},
                    {"progress", "asynchronous package sync; selected origin restored"}}},
            {"unmergeTableCell",
                QVariantMap{{"scope", "selected safe rectangular merged origin; preserves every cell"}}},
            {"formatTableBorder",
                QVariantMap{{"color", "#RRGGBB"}, {"width", "pt [0.25,12]"},
                    {"edge", "all|left|top|right|bottom; defaults to all"}}},
            {"resetTableBorder",
                QVariantMap{{"scope", "selected editable table cell with at least one direct edge override"},
                    {"edge", "all|left|top|right|bottom; defaults to all"},
                    {"effect", "remove selected direct edge lines and resume table style borders"}}},
            {"resetTableCellFill",
                QVariantMap{{"scope", "selected editable table cell with direct fill override"},
                    {"effect", "remove only direct cell fill and resume table style or default fill"}}},
            {"groupAdjacent",
                QVariantMap{{"targetIndex", "required zero-based adjacent shape index"},
                    {"scope", "two editable top-level objects; exact drawing transforms retained"}}},
            {"addToGroup",
                QVariantMap{{"groupId", "current top-level group ID from semantic tree; required"},
                    {"targetIndex", "required zero-based adjacent top-level ordinary shape index"},
                    {"scope", "selected member of an unrotated, unscaled, effect-free group"},
                    {"effect", "append or prepend one adjacent object without changing member transforms"}}},
            {"ungroup",
                QVariantMap{{"groupId", "top-level group ID from semantic tree"},
                    {"scope",
                        "top-level group; picture-only groups without effects may be scaled or rotated; "
                        "flipped and visually unsafe groups remain locked"}}},
            {"formatShape",
                QVariantMap{{"fillColor", "#RRGGBB"}, {"fillOpacity", "[0,1]"},
                    {"fillPattern",
                        "none|pct5|pct10|pct25|pct50|pct75|pct90|horz|vert|dnDiag|upDiag|cross|diagCross|"
                        "smGrid|lgGrid|smCheck|lgCheck"},
                    {"patternForegroundColor", "#RRGGBB; requires pattern"},
                    {"patternBackgroundColor", "#RRGGBB; requires pattern"},
                    {"gradientStartColor", "#RRGGBB"}, {"gradientEndColor", "#RRGGBB"},
                    {"gradientAngle", "degrees"}, {"outlineColor", "#RRGGBB"}, {"outlineOpacity", "[0,1]"},
                    {"outlineWidth", "pt"},
                    {"lineDash",
                        "solid|dot|dash|lgDash|dashDot|lgDashDot|lgDashDotDot|sysDot|sysDash|sysDashDot|"
                        "sysDashDotDot"},
                    {"lineHead", "none|triangle|stealth|diamond|oval|arrow"},
                    {"lineTail", "none|triangle|stealth|diamond|oval|arrow"}, {"shadowEnabled", "bool"},
                    {"shadowColor", "#RRGGBB"}, {"shadowOpacity", "[0,1]"}, {"shadowBlur", "pt"},
                    {"shadowX", "pt"}, {"shadowY", "pt"}, {"glowEnabled", "bool"}, {"glowColor", "#RRGGBB"},
                    {"glowOpacity", "[0,1]"}, {"glowRadius", "pt"}}},
            {"resetPlaceholderFill",
                QVariantMap{{"scope", "selected editable slide placeholder with a local fill override"},
                    {"effect", "remove the local fill and resume layout or master fill inheritance"}}},
            {"resetPlaceholderOutline",
                QVariantMap{{"scope", "selected editable slide placeholder with a local outline override"},
                    {"effect", "remove the local line and resume layout or master outline inheritance"}}},
            {"resetTextInheritance",
                QVariantMap{{"property", "fontFamily|fontSize|color; required"},
                    {"scope", "selected editable slide text shape or table cell with direct local override"},
                    {"effect",
                        "remove only that property's direct formatting from the selected shape or cell; "
                        "restore layout, master, table style, or theme value after package sync"}}},
            {"moveShape", QVariantMap{{"offset", "-1|1"}, {"targetIndex", "zero-based; overrides offset"}}},
            {"duplicateShape", QVariantMap{}}, {"deleteShape", QVariantMap{}},
            {"formatTextStyle",
                QVariantMap{{"fill", fill}, {"outline", QVariantMap{{"fill", fill}, {"width", "pt [0,72]"}}},
                    {"shadow",
                        QVariantMap{{"color", "#RRGGBB"}, {"opacity", "[0,1]"}, {"blur", "pt [0,72]"},
                            {"x", "pt [-200,200]"}, {"y", "pt [-200,200]"}}},
                    {"glow",
                        QVariantMap{{"color", "#RRGGBB"}, {"opacity", "[0,1]"}, {"radius", "pt [0,72]"}}},
                    {"reflection",
                        QVariantMap{{"opacity", "[0,1]"}, {"offset", "pt [0,200]"}, {"endOpacity", "[0,1]"},
                            {"startPosition", "[0,1]"}, {"endPosition", "[0,1]"}}},
                    {"warp",
                        "textNoShape|textPlain|textArchUp|textArchDown|textWave1|textWave2|textDoubleWave1|"
                        "textInflate|textDeflate|textSlantUp|textSlantDown|textChevron|textChevronInverted|"
                        "textCircle"},
                    {"warpAdjustment", "[0.05,0.45]"}, {"rotation", "degrees [-360,360]"},
                    {"vertical", "horz|vert|vert270"}, {"clipVertical", "bool"}, {"clipHorizontal", "bool"}}},
            {"tableCell",
                "text and local style edits target one cell; structural row/column/merge actions reparse "
                "the entire table"},
            {"groupChild",
                "preserves parent group; no cross-group layer reordering; nonrepresentable transforms "
                "rejected atomically"}};
    }
}
