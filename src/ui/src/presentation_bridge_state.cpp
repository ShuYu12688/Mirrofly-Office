#include "presentation_bridge.hpp"
#include "presentation_edit_adapter.hpp"
#include "presentation_semantics.hpp"
#include "presentation_text_style_adapter.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace
{
    const QStringList preview_layouts{
        "researchStudio", "evidenceBoard", "projectDashboard", "deliveryRoadmap"};

    QString utf8(const std::string& text)
    {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    QString shape_text(const mirrorfly::PresentationShape& shape)
    {
        QStringList paragraphs;
        for (const auto& paragraph : shape.text.paragraphs)
        {
            QString text;
            for (const auto& run : paragraph.runs)
            {
                text += utf8(run.text);
            }
            paragraphs.append(text);
        }
        return paragraphs.join(QLatin1Char('\n'));
    }

}

namespace mirrorfly
{
    QStringList PresentationBridge::chineseFontFamilies() const
    {
        return presentation_render_environment()->cjk_fonts;
    }

    bool PresentationBridge::active() const
    {
        return active_;
    }

    bool PresentationBridge::editable() const
    {
        return scene_ && scene_->native_editable;
    }

    bool PresentationBridge::themeEditable() const
    {
        if (!editable() || current_slide_ < 0 || current_slide_ >= static_cast<int>(scene_->slides.size()))
            return false;
        const auto state = presentation_theme_state(*scene_, static_cast<std::size_t>(current_slide_));
        return !state.editable_color_slots.empty() || !state.editable_font_slots.empty();
    }

    QVariantMap PresentationBridge::themeState() const
    {
        if (!scene_ || current_slide_ < 0 || current_slide_ >= static_cast<int>(scene_->slides.size()))
            return {{"available", false}};
        return presentation_theme_state_map(*scene_, static_cast<std::size_t>(current_slide_));
    }

    bool PresentationBridge::modified() const
    {
        return active_ && generation_ != saved_generation_;
    }

    bool PresentationBridge::busy() const
    {
        return busy_;
    }

    bool PresentationBridge::loading() const
    {
        return operation_ == Operation::Load;
    }

    bool PresentationBridge::syncing() const
    {
        return !pending_edits_.empty() || edit_worker_.isRunning();
    }

    int PresentationBridge::pendingEdits() const
    {
        return static_cast<int>(pending_edits_.size());
    }

    QString PresentationBridge::loadingStage() const
    {
        return loading_stage_;
    }

    qreal PresentationBridge::loadingProgress() const
    {
        return loading_progress_;
    }

    bool PresentationBridge::locked() const
    {
        return busy_ || confirmation_open_ || save_dialog_open_;
    }

    bool PresentationBridge::canUndo() const
    {
        return editable() && !locked() && !syncing() && !undo_.empty();
    }

    bool PresentationBridge::canRedo() const
    {
        return editable() && !locked() && !syncing() && !redo_.empty();
    }

    QString PresentationBridge::error() const
    {
        return error_;
    }

    QString PresentationBridge::message() const
    {
        return message_;
    }

    QVariant PresentationBridge::document() const
    {
        return document_ ? QVariant::fromValue(document_) : QVariant{};
    }

    PdfExportSource PresentationBridge::pdfSource() const
    {
        PdfExportSource result;
        if (!active() || locked() || syncing() || !scene_)
            result.error = "演示文稿尚未准备好导出。";
        else
            result.content = std::shared_ptr<const PresentationScene>(scene_);
        result.title = documentName().toStdString();
        result.source_path = documentPath().toStdString();
        result.current = static_cast<std::size_t>(current_slide_);
        return result;
    }

    QString PresentationBridge::documentName() const
    {
        return path_.isEmpty() ? QStringLiteral("未命名.pptx") : QFileInfo(path_).fileName();
    }

    QString PresentationBridge::documentPath() const
    {
        return path_;
    }

    int PresentationBridge::slideCount() const
    {
        return scene_ ? static_cast<int>(scene_->slides.size()) : 0;
    }

    QVariantList PresentationBridge::hiddenSlides() const
    {
        QVariantList result;
        if (scene_)
        {
            for (const auto& slide : scene_->slides)
            {
                result.append(slide.hidden);
            }
        }
        return result;
    }

    QVariantList PresentationBridge::slideSections() const
    {
        QVariantList result;
        if (!scene_)
            return result;
        for (const auto& section : scene_->sections)
        {
            int first = -1;
            int count = 0;
            for (std::size_t index = 0; index < scene_->slides.size(); ++index)
                if (scene_->slides[index].section_id == section.id)
                {
                    if (first < 0)
                        first = static_cast<int>(index);
                    ++count;
                }
            result.append(QVariantMap{{QStringLiteral("id"), utf8(section.id)},
                {QStringLiteral("name"), utf8(section.name)}, {QStringLiteral("firstSlide"), first},
                {QStringLiteral("slideCount"), count}});
        }
        return result;
    }

    bool PresentationBridge::sectionsEditable() const
    {
        return editable() && scene_ && !scene_->section_structure_locked;
    }

    int PresentationBridge::currentSlide() const
    {
        return current_slide_;
    }

    QVariantMap PresentationBridge::slideTransition() const
    {
        if (!scene_ || current_slide_ < 0 ||
            static_cast<std::size_t>(current_slide_) >= scene_->slides.size())
            return {};
        const auto& transition = scene_->slides[static_cast<std::size_t>(current_slide_)].transition;
        return {{"type", QString::fromStdString(transition.type)},
            {"direction", QString::fromStdString(transition.direction)},
            {"durationSeconds", transition.duration}, {"advanceOnClick", transition.advance_on_click},
            {"advanceAfterSeconds", transition.advance_after},
            {"editable", editable() && transition.editable}, {"approximate", transition.approximate}};
    }

    int PresentationBridge::selectedShape() const
    {
        return selected_shape_;
    }

    QVariantMap PresentationBridge::selection() const
    {
        QVariantMap result{{QStringLiteral("valid"), false}, {QStringLiteral("index"), -1}};
        if (!scene_ || current_slide_ < 0 || current_slide_ >= slideCount())
        {
            return result;
        }
        const auto& slide = scene_->slides[static_cast<std::size_t>(current_slide_)];
        if (selected_shape_ < 0 || selected_shape_ >= static_cast<int>(slide.shapes.size()))
        {
            return result;
        }
        const auto& shape = slide.shapes[static_cast<std::size_t>(selected_shape_)];
        const PresentationParagraph* paragraph =
            shape.text.paragraphs.empty() ? nullptr : &shape.text.paragraphs.front();
        const PresentationRun* run =
            !paragraph || paragraph->runs.empty() ? nullptr : &paragraph->runs.front();
        result.insert(QStringLiteral("valid"), true);
        result.insert(QStringLiteral("editable"), shape.editable);
        result.insert(QStringLiteral("actions"), presentation_selection_actions(slide, selected_shape_));
        result.insert(QStringLiteral("isTableCell"), shape.table_cell.has_value());
        if (shape.table_cell)
        {
            result.insert(QStringLiteral("tableStyleAvailable"), shape.table_cell->style_available);
            result.insert(QStringLiteral("tableStyleOptions"),
                presentation_table_style_options(shape.table_cell->style_options));
            result.insert(QStringLiteral("tableStructureOptions"),
                presentation_table_structure_options(shape.table_cell->structure_options));
            result.insert(QStringLiteral("tableRowCount"), static_cast<int>(shape.table_cell->row_count));
            result.insert(
                QStringLiteral("tableColumnCount"), static_cast<int>(shape.table_cell->column_count));
            result.insert(QStringLiteral("tableHasMerges"), shape.table_cell->has_merges);
            result.insert(QStringLiteral("tableLocalFillOverride"), shape.table_cell->local_fill_override);
            result.insert(
                QStringLiteral("tableLocalBorderOverride"), shape.table_cell->local_border_override);
            result.insert(QStringLiteral("tableBorderColor"), utf8(shape.table_cell->border_color));
            result.insert(QStringLiteral("tableBorderWidth"), shape.table_cell->border_width);
            QVariantMap edges;
            const char* names[]{"left", "top", "right", "bottom"};
            for (std::size_t index = 0; index < shape.table_cell->border_edges.size(); ++index)
            {
                const auto& edge = shape.table_cell->border_edges[index];
                edges.insert(QString::fromLatin1(names[index]),
                    QVariantMap{{QStringLiteral("color"), utf8(edge.color)},
                        {QStringLiteral("opacity"), edge.opacity}, {QStringLiteral("width"), edge.width},
                        {QStringLiteral("localOverride"), edge.local_override}});
            }
            result.insert(QStringLiteral("tableBorderEdges"), edges);
        }
        const auto group_id = shape.source_groups.empty() ? std::string{} : shape.source_groups.back();
        const auto group = std::find_if(slide.groups.begin(), slide.groups.end(), [&](const auto& frame)
        {
            return frame.source_id == group_id && frame.source_part == slide.source_part;
        });
        result.insert(QStringLiteral("groupId"), utf8(group_id));
        result.insert(QStringLiteral("groupEditable"), group != slide.groups.end() && group->editable);
        result.insert(QStringLiteral("groupUngroupable"),
            group != slide.groups.end() && group->editable && group->ungroupable);
        result.insert(QStringLiteral("groupExtensible"), group != slide.groups.end() && group->extensible);
        const auto layers = group != slide.groups.end() ? presentation_group_layer_options(*group)
                                                        : PresentationGroupLayerOptions{};
        result.insert(QStringLiteral("groupLayerOptions"),
            QVariantMap{{"back", layers.back}, {"backward", layers.backward}, {"forward", layers.forward},
                {"front", layers.front}});
        const auto group_targets = presentation_group_targets(slide, selected_shape_);
        result.insert(QStringLiteral("groupPrevIndex"), group_targets[0]);
        result.insert(QStringLiteral("groupNextIndex"), group_targets[1]);
        result.insert(QStringLiteral("index"), selected_shape_);
        result.insert(QStringLiteral("name"), utf8(shape.name));
        result.insert(QStringLiteral("text"), shape_text(shape));
        QString family;
        QString family_source;
        if (run)
        {
            const auto characters = shape_text(shape).toUcs4();
            const bool chinese =
                std::any_of(characters.begin(), characters.end(), presentation_east_asian_character);
            family = utf8(run->font_family);
            family_source = utf8(run->font_family_source);
            if (chinese && !run->east_asian_font_family.empty())
            {
                family = utf8(run->east_asian_font_family);
                family_source = utf8(run->east_asian_font_source);
            }
        }
        result.insert(QStringLiteral("fontFamily"), family);
        result.insert(QStringLiteral("fontFamilySource"), family_source);
        const auto local_text = presentation_text_local_overrides(shape);
        result.insert(QStringLiteral("fontFamilyLocalOverride"), local_text.font_family);
        result.insert(QStringLiteral("fontSizeLocalOverride"), local_text.font_size);
        result.insert(QStringLiteral("textColorLocalOverride"), local_text.color);
        result.insert(QStringLiteral("fontSize"), run ? run->font_size : 18.0);
        result.insert(QStringLiteral("fontSizeSource"), run ? utf8(run->font_size_source) : QString());
        result.insert(QStringLiteral("bold"), run && run->bold);
        result.insert(QStringLiteral("italic"), run && run->italic);
        result.insert(QStringLiteral("underline"), run && run->underline);
        result.insert(QStringLiteral("strike"), run && run->strike);
        result.insert(QStringLiteral("characterSpacing"), run ? run->spacing : 0);
        result.insert(QStringLiteral("baseline"), run ? run->baseline : 0);
        result.insert(QStringLiteral("textColor"), run ? utf8(run->color) : QStringLiteral("#000000"));
        result.insert(QStringLiteral("textColorSource"), run ? utf8(run->color_source) : QString());
        result.insert(QStringLiteral("textStyle"), presentation_text_style_state(shape));
        result.insert(QStringLiteral("fillColor"), utf8(shape.fill.color));
        result.insert(QStringLiteral("fillPattern"), utf8(shape.fill.pattern));
        result.insert(QStringLiteral("patternForegroundColor"), utf8(shape.fill.pattern_foreground_color));
        result.insert(QStringLiteral("patternBackgroundColor"), utf8(shape.fill.color));
        result.insert(
            QStringLiteral("patternFillSupported"), presentation_pattern_supported(shape.fill.pattern));
        QStringList patterns;
        for (const auto& name : presentation_pattern_presets())
            patterns.append(utf8(name));
        result.insert(QStringLiteral("patternPresets"), patterns);
        QStringList dashes;
        for (const auto& name : presentation_line_dash_presets())
            dashes.append(utf8(name));
        result.insert(QStringLiteral("lineDashPresets"), dashes);
        if (shape.placeholder)
        {
            const auto& placeholder = *shape.placeholder;
            result.insert(QStringLiteral("placeholder"),
                QVariantMap{{QStringLiteral("type"), utf8(placeholder.type)},
                    {QStringLiteral("index"), static_cast<int>(placeholder.index)},
                    {QStringLiteral("hasLayout"), placeholder.has_layout},
                    {QStringLiteral("hasMaster"), placeholder.has_master},
                    {QStringLiteral("fillSource"), utf8(placeholder.fill_source)},
                    {QStringLiteral("inheritedFillSource"), utf8(placeholder.inherited_fill_source)},
                    {QStringLiteral("localFillOverride"), placeholder.local_fill_override},
                    {QStringLiteral("outlineSource"), utf8(placeholder.outline_source)},
                    {QStringLiteral("inheritedOutlineSource"), utf8(placeholder.inherited_outline_source)},
                    {QStringLiteral("localOutlineOverride"), placeholder.local_outline_override}});
        }
        result.insert(QStringLiteral("fillOpacity"), shape.fill.opacity);
        result.insert(QStringLiteral("hasGradient"), !shape.fill.stops.empty());
        result.insert(QStringLiteral("gradientStartColor"),
            shape.fill.stops.empty() ? utf8(shape.fill.color) : utf8(shape.fill.stops.front().color));
        result.insert(QStringLiteral("gradientEndColor"),
            shape.fill.stops.empty() ? utf8(shape.fill.color) : utf8(shape.fill.stops.back().color));
        result.insert(QStringLiteral("gradientAngle"), shape.fill.angle_degrees);
        result.insert(QStringLiteral("outlineColor"), utf8(shape.outline_color));
        result.insert(QStringLiteral("outlineOpacity"), shape.outline_opacity);
        result.insert(QStringLiteral("outlineWidth"), shape.outline_width);
        result.insert(QStringLiteral("lineDash"), utf8(presentation_line_dash_name(shape.line_style.dashes)));
        result.insert(QStringLiteral("lineHead"), utf8(shape.line_style.head.type));
        result.insert(QStringLiteral("lineTail"), utf8(shape.line_style.tail.type));
        result.insert(QStringLiteral("shadowEnabled"), shape.effects.shadow_opacity > 0);
        result.insert(QStringLiteral("effectsSource"), utf8(shape.effects_source));
        result.insert(
            QStringLiteral("themeEffectStyleIndex"), static_cast<int>(shape.theme_effect_style_index));
        result.insert(QStringLiteral("shadowColor"), utf8(shape.effects.shadow_color));
        result.insert(QStringLiteral("shadowOpacity"), shape.effects.shadow_opacity);
        result.insert(QStringLiteral("shadowBlur"), shape.effects.shadow_blur);
        result.insert(QStringLiteral("shadowX"), shape.effects.shadow_x);
        result.insert(QStringLiteral("shadowY"), shape.effects.shadow_y);
        result.insert(QStringLiteral("glowEnabled"), shape.effects.glow_opacity > 0);
        result.insert(QStringLiteral("glowColor"), utf8(shape.effects.glow_color));
        result.insert(QStringLiteral("glowOpacity"), shape.effects.glow_opacity);
        result.insert(QStringLiteral("glowRadius"), shape.effects.glow_radius);
        const double rotation =
            std::atan2(shape.transform[1], shape.transform[0]) * 180 / 3.14159265358979323846;
        result.insert(QStringLiteral("rotation"), std::abs(rotation) < 1e-8 ? 0 : rotation);
        result.insert(
            QStringLiteral("alignment"), paragraph ? utf8(paragraph->alignment) : QStringLiteral("left"));
        result.insert(QStringLiteral("bullet"), paragraph && !paragraph->bullet.empty());
        result.insert(QStringLiteral("numbered"), paragraph && paragraph->numbered);
        result.insert(QStringLiteral("numberStart"), paragraph ? paragraph->number_start : 1);
        result.insert(QStringLiteral("listLevel"), paragraph ? paragraph->list_level : 0);
        result.insert(QStringLiteral("paragraphCount"), static_cast<int>(shape.text.paragraphs.size()));
        result.insert(QStringLiteral("paragraphMarginLeft"), paragraph ? paragraph->margin_left : 0);
        result.insert(QStringLiteral("firstLineIndent"), paragraph ? paragraph->first_line_indent : 0);
        result.insert(QStringLiteral("lineSpacing"), paragraph ? paragraph->line_spacing : 1);
        result.insert(QStringLiteral("spaceBefore"), paragraph ? paragraph->space_before : 0);
        result.insert(QStringLiteral("spaceAfter"), paragraph ? paragraph->space_after : 0);
        result.insert(QStringLiteral("insetLeft"), shape.text.inset_left);
        result.insert(QStringLiteral("insetRight"), shape.text.inset_right);
        result.insert(QStringLiteral("insetTop"), shape.text.inset_top);
        result.insert(QStringLiteral("insetBottom"), shape.text.inset_bottom);
        const auto vertical_alignment = shape.text.vertical_alignment == "middle"
            ? QStringLiteral("center")
            : utf8(shape.text.vertical_alignment);
        result.insert(QStringLiteral("verticalAlignment"), vertical_alignment);
        result.insert(QStringLiteral("wrap"), shape.text.wrap);
        result.insert(QStringLiteral("autoFit"), shape.text.auto_fit);
        result.insert(QStringLiteral("x"), shape.transform[4]);
        result.insert(QStringLiteral("a"), shape.transform[0]);
        result.insert(QStringLiteral("b"), shape.transform[1]);
        result.insert(QStringLiteral("c"), shape.transform[2]);
        result.insert(QStringLiteral("d"), shape.transform[3]);
        result.insert(QStringLiteral("id"), QString::number(shape.id));
        result.insert(QStringLiteral("y"), shape.transform[5]);
        result.insert(QStringLiteral("width"), shape.width);
        result.insert(QStringLiteral("height"), shape.height);
        result.insert(QStringLiteral("geometry"), utf8(shape.geometry));
        result.insert(QStringLiteral("isImage"), !shape.image_path.empty());
        result.insert(QStringLiteral("shapeCount"), static_cast<int>(slide.shapes.size()));
        result.insert(QStringLiteral("imageCropLeft"), shape.image_crop[0]);
        result.insert(QStringLiteral("imageCropTop"), shape.image_crop[1]);
        result.insert(QStringLiteral("imageCropRight"), shape.image_crop[2]);
        result.insert(QStringLiteral("imageCropBottom"), shape.image_crop[3]);
        result.insert(QStringLiteral("imageOpacity"), shape.image_opacity);
        result.insert(QStringLiteral("clickAction"),
            QVariantMap{{QStringLiteral("kind"), utf8(shape.click_action.kind)},
                {QStringLiteral("targetSlide"), shape.click_action.target_slide}});
        return result;
    }

    QVariantMap PresentationBridge::paragraphInfo(int index) const
    {
        QVariantMap result{{"valid", false}, {"index", index}};
        if (!scene_ || current_slide_ < 0 || current_slide_ >= slideCount())
            return result;
        const auto& shapes = scene_->slides[static_cast<std::size_t>(current_slide_)].shapes;
        if (selected_shape_ < 0 || selected_shape_ >= static_cast<int>(shapes.size()))
            return result;
        const auto& paragraphs = shapes[static_cast<std::size_t>(selected_shape_)].text.paragraphs;
        if (index < 0 || index >= static_cast<int>(paragraphs.size()))
            return result;
        const auto& paragraph = paragraphs[static_cast<std::size_t>(index)];
        QString text;
        for (const auto& run : paragraph.runs)
        {
            if (text.size() >= 1024)
                break;
            text += utf8(run.text).left(1024 - text.size());
        }
        result.insert("valid", true);
        result.insert("listLevel", paragraph.list_level);
        result.insert("bullet", !paragraph.bullet.empty() || !paragraph.bullet_image_path.empty());
        result.insert("numbered", paragraph.numbered);
        result.insert("numberStart", paragraph.number_start);
        result.insert("marginLeft", paragraph.margin_left);
        result.insert("firstLineIndent", paragraph.first_line_indent);
        result.insert("text", text);
        result.insert("textMayBeTruncated", text.size() >= 1024);
        return result;
    }

    QVariantMap PresentationBridge::findText(const QString& query, bool case_sensitive, int offset) const
    {
        if (!scene_ || offset < 0 || query.isEmpty() || query.toUtf8().size() > 256)
            return {{"ok", false}, {"error", "invalid_query_or_offset"}};
        const auto matches =
            find_presentation_text(*scene_, query.toUtf8().toStdString(), case_sensitive, 10001);
        if (matches.size() > 10000 || offset > static_cast<int>(matches.size()))
            return {{"ok", false}, {"error", "result_limit_or_offset"}};
        const int end = std::min(offset + 64, static_cast<int>(matches.size()));
        QVariantList nodes;
        for (int index = offset; index < end; ++index)
        {
            const auto& match = matches[static_cast<std::size_t>(index)];
            nodes.append(QVariantMap{{"shapeId", QString::number(match.shape_id)},
                {"slideIndex", static_cast<int>(match.slide_index)},
                {"shapeIndex", static_cast<int>(match.shape_index)},
                {"paragraphIndex", static_cast<int>(match.paragraph_index)},
                {"startByte", static_cast<int>(match.start_byte)},
                {"lengthBytes", static_cast<int>(match.length_bytes)},
                {"replaceable", match.replaceable && editable()},
                {"excerpt", QString::fromStdString(match.excerpt)}});
        }
        return {{"ok", true}, {"schemaVersion", 1}, {"generation", QString::number(generation_)},
            {"syncing", syncing()}, {"pendingEdits", pendingEdits()}, {"offset", offset},
            {"nextOffset", end < static_cast<int>(matches.size()) ? end : -1},
            {"total", static_cast<int>(matches.size())}, {"nodes", nodes}, {"indices", "zeroBased"},
            {"offsetUnit", "utf8Bytes"}, {"contentTrust", "untrustedDocumentData"}};
    }

    QString PresentationBridge::editGeneration() const
    {
        return QString::number(generation_);
    }

    QVariantMap PresentationBridge::semanticTree(int page, int offset) const
    {
        if (scene_)
            return presentation_semantic_tree(*scene_, page, offset);
        return {{"ok", false}, {"error", "no_document"}};
    }

    QVariantMap PresentationBridge::semanticPage(int page, int offset, int limit) const
    {
        if (!scene_)
            return {{"ok", false}, {"error", "no_document"}};
        if (limit < 1 || limit > 16)
            return {{"ok", false}, {"error", "invalid_limit"}, {"limitRange", "1..16"}};
        const auto bytes = [](const QVariantMap& value)
        {
            return QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact).size();
        };
        auto result = presentation_semantic_tree(*scene_, page, offset, limit);
        while (result.value("ok").toBool() && bytes(result) > 12 * 1024 && limit > 1)
        {
            limit = std::max(1, limit / 2);
            result = presentation_semantic_tree(*scene_, page, offset, limit);
        }
        if (bytes(result) > 12 * 1024)
        {
            QVariantList nodes;
            for (const auto& value : result.value("nodes").toList())
            {
                const auto node = value.toMap();
                if (!node.contains("index") && node.value("type") != "slide")
                    continue;
                QVariantMap preview;
                const auto keys = {"id", "type", "index", "parentId", "width", "height", "transform", "text",
                    "textMayBeTruncated", "actions", "writable", "lockedReason", "style"};
                for (const auto* key : keys)
                    if (node.contains(key))
                        preview.insert(key, node.value(key));
                nodes.append(preview);
            }
            result.insert("nodes", nodes);
            result.insert("previewTruncated", true);
            result.insert("detailQuery", "selectObject(id), paragraphInfo(index) for full paragraph data");
        }
        result.insert("limit", limit);
        return result;
    }

    QString PresentationBridge::speakerNotes(int page) const
    {
        if (!scene_ || page < 0 || page >= static_cast<int>(scene_->slides.size()))
            return {};
        return QString::fromStdString(scene_->slides[static_cast<std::size_t>(page)].speaker_notes);
    }

    QVariantMap PresentationBridge::editSchema() const
    {
        return presentation_edit_schema();
    }

    QVariantMap PresentationBridge::templatePreviews(const QVariantMap& options) const
    {
        QVariantMap previews;
        for (const auto& name : preview_layouts)
        {
            const auto palette = presentation_template_palette(options.value(name).toMap());
            const std::array<std::string, 6> key{
                palette.ink, palette.paper, palette.card, palette.muted, palette.accent, palette.soft};
            auto& preview = template_previews_[name];
            if (!preview.document || preview.palette != key)
            {
                auto scene = std::make_shared<PresentationScene>(
                    make_presentation(*presentation_slide_layout(name), palette));
                preview = {key, prepare_presentation(scene)};
            }
            previews.insert(name, QVariant::fromValue(preview.document));
        }
        return previews;
    }

    QVariantMap PresentationBridge::templateDescriptions(const QVariantMap& options) const
    {
        QVariantMap descriptions;
        for (const auto& name : preview_layouts)
        {
            const auto palette = presentation_template_palette(options.value(name).toMap());
            const auto scene = make_presentation(*presentation_slide_layout(name), palette);
            const auto& slide = scene.slides.front();
            descriptions.insert(name,
                QVariantMap{{"layout", name}, {"width", scene.width}, {"height", scene.height},
                    {"units", "pt"}, {"title", QString::fromStdString(slide.title)},
                    {"objectCount", static_cast<int>(slide.shapes.size())}, {"nativeGuiPreview", true}});
        }
        return {{"schemaVersion", 1}, {"kind", "templateMetadata"}, {"templates", descriptions},
            {"selection", "applyEdit(addSlide,{layout:<key>,palette:<colors>})"}};
    }

    qreal PresentationBridge::slideWidth() const
    {
        return scene_ ? scene_->width : 0;
    }

    qreal PresentationBridge::slideHeight() const
    {
        return scene_ ? scene_->height : 0;
    }

    QVariantMap PresentationBridge::guideSettings() const
    {
        return guide_settings_.toMap();
    }

    qreal PresentationBridge::zoom() const
    {
        return zoom_;
    }

    QString PresentationBridge::fontSummary() const
    {
        return document_ ? document_->font_summary : QString{};
    }

    QUrl PresentationBridge::saveUrl() const
    {
        if (!path_.isEmpty() && editable())
        {
            return QUrl::fromLocalFile(path_);
        }
        const auto directory = path_.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : QFileInfo(path_).absolutePath();
        const auto name = path_.isEmpty()
            ? QStringLiteral("未命名.pptx")
            : QFileInfo(path_).completeBaseName() + QStringLiteral("-编辑副本.pptx");
        return QUrl::fromLocalFile(QDir(directory).filePath(name));
    }

}
