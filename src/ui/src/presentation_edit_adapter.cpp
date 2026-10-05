#include "presentation_edit_adapter.hpp"

#include <QColor>
#include <QMetaType>
#include <QStringList>

#include <algorithm>
#include <map>
#include <optional>

namespace
{
    std::string narrow(const QString& text)
    {
        return text.toUtf8().toStdString();
    }

    bool option(const QVariantMap& options, const QString& name, std::optional<bool>& destination)
    {
        if (!options.contains(name))
        {
            return false;
        }
        destination = options.value(name).toBool();
        return true;
    }

    bool option(const QVariantMap& options, const QString& name, std::optional<double>& destination)
    {
        if (!options.contains(name))
        {
            return false;
        }
        bool valid = false;
        const double value = options.value(name).toDouble(&valid);
        if (valid)
        {
            destination = value;
        }
        return valid;
    }

    bool option(const QVariantMap& options, const QString& name, std::optional<int>& destination)
    {
        if (!options.contains(name))
        {
            return false;
        }
        bool valid = false;
        const int value = options.value(name).toInt(&valid);
        if (valid)
        {
            destination = value;
        }
        return valid;
    }

    bool option(const QVariantMap& options, const QString& name, std::optional<std::string>& destination)
    {
        if (!options.contains(name))
        {
            return false;
        }
        destination = narrow(options.value(name).toString());
        return true;
    }

    std::optional<mirrorfly::PresentationTableStyleOptions> table_style_options(const QVariantMap& options)
    {
        const auto values = options.value(QStringLiteral("style")).toMap();
        const QStringList names{QStringLiteral("firstRow"), QStringLiteral("lastRow"),
            QStringLiteral("firstColumn"), QStringLiteral("lastColumn"), QStringLiteral("bandRows"),
            QStringLiteral("bandColumns")};
        for (const auto& name : names)
            if (!values.contains(name) || values.value(name).typeId() != QMetaType::Bool)
                return std::nullopt;
        return mirrorfly::PresentationTableStyleOptions{values.value(names[0]).toBool(),
            values.value(names[1]).toBool(), values.value(names[2]).toBool(), values.value(names[3]).toBool(),
            values.value(names[4]).toBool(), values.value(names[5]).toBool()};
    }
}

namespace mirrorfly
{
    std::optional<mirrorfly::PresentationSlideLayout> presentation_slide_layout(const QString& name)
    {
        using Layout = mirrorfly::PresentationSlideLayout;
        static const std::map<QString, Layout> layouts{{QStringLiteral("title"), Layout::Title},
            {QStringLiteral("titleContent"), Layout::TitleContent},
            {QStringLiteral("twoColumns"), Layout::TwoColumns}, {QStringLiteral("blank"), Layout::Blank},
            {QStringLiteral("reportOutline"), Layout::ReportOutline},
            {QStringLiteral("researchPlan"), Layout::ResearchPlan},
            {QStringLiteral("comparison"), Layout::Comparison},
            {QStringLiteral("references"), Layout::References},
            {QStringLiteral("conclusion"), Layout::Conclusion},
            {QStringLiteral("projectStatus"), Layout::ProjectStatus},
            {QStringLiteral("meetingSummary"), Layout::MeetingSummary},
            {QStringLiteral("milestones"), Layout::Milestones},
            {QStringLiteral("researchStudio"), Layout::ResearchStudio},
            {QStringLiteral("evidenceBoard"), Layout::EvidenceBoard},
            {QStringLiteral("projectDashboard"), Layout::ProjectDashboard},
            {QStringLiteral("deliveryRoadmap"), Layout::DeliveryRoadmap}};
        const auto found = layouts.find(name);
        return found == layouts.end() ? std::nullopt : std::optional<Layout>(found->second);
    }

    mirrorfly::PresentationTemplatePalette presentation_template_palette(const QVariantMap& options)
    {
        mirrorfly::PresentationTemplatePalette palette;
        const auto colors = options.value(QStringLiteral("palette")).toMap();
        const auto assign = [&colors](const QString& key, std::string& target)
        {
            const QColor color(colors.value(key).toString());
            if (color.isValid())
                target = color.name(QColor::HexRgb).toStdString();
        };
        assign(QStringLiteral("ink"), palette.ink);
        assign(QStringLiteral("paper"), palette.paper);
        assign(QStringLiteral("card"), palette.card);
        assign(QStringLiteral("muted"), palette.muted);
        assign(QStringLiteral("accent"), palette.accent);
        assign(QStringLiteral("soft"), palette.soft);
        return palette;
    }

    PresentationEditCommand presentation_edit_command(
        const QString& action, const QVariantMap& options, int current_slide, int selected_shape)
    {
        PresentationEditCommand command;
        command.template_palette = presentation_template_palette(options);
        command.slide_index = static_cast<std::size_t>(std::max(0, current_slide));
        command.shape_index = static_cast<std::size_t>(std::max(0, selected_shape));
        if (action == QStringLiteral("addSlide"))
        {
            command.action = PresentationEditAction::AddSlide;
            command.slide_index = static_cast<std::size_t>(
                current_slide + (options.value(QStringLiteral("before")).toBool() ? 0 : 1));
            const QString layout =
                options.value(QStringLiteral("layout"), QStringLiteral("blank")).toString();
            command.layout = presentation_slide_layout(layout).value_or(PresentationSlideLayout::Blank);
        }
        else if (action == QStringLiteral("duplicateSlide"))
        {
            command.action = PresentationEditAction::DuplicateSlide;
        }
        else if (action == QStringLiteral("deleteSlide"))
        {
            command.action = PresentationEditAction::DeleteSlide;
        }
        else if (action == QStringLiteral("moveSlide"))
        {
            command.action = PresentationEditAction::MoveSlide;
            command.offset = options.value(QStringLiteral("offset")).toInt();
        }
        else if (action == QStringLiteral("createSection"))
        {
            command.action = PresentationEditAction::CreateSection;
            command.section_name = narrow(options.value(QStringLiteral("name")).toString());
        }
        else if (action == QStringLiteral("renameSection"))
        {
            command.action = PresentationEditAction::RenameSection;
            command.section_name = narrow(options.value(QStringLiteral("name")).toString());
        }
        else if (action == QStringLiteral("removeSection"))
            command.action = PresentationEditAction::RemoveSection;
        else if (action == QStringLiteral("setClickAction"))
        {
            command.action = PresentationEditAction::SetClickAction;
            command.click_kind = narrow(options.value(QStringLiteral("kind")).toString());
            if (options.contains(QStringLiteral("targetSlide")))
            {
                const int target = options.value(QStringLiteral("targetSlide")).toInt();
                if (target >= 0)
                    command.click_target_slide = static_cast<std::size_t>(target);
            }
        }
        else if (action == QStringLiteral("applyFormat"))
            command.action = PresentationEditAction::ApplyFormat;
        else if (action == QStringLiteral("addText"))
        {
            command.action = PresentationEditAction::AddText;
            command.text = narrow(options.value(QStringLiteral("text")).toString());
            option(options, QStringLiteral("x"), command.x);
            option(options, QStringLiteral("y"), command.y);
            option(options, QStringLiteral("width"), command.width);
            option(options, QStringLiteral("height"), command.height);
        }
        else if (action == QStringLiteral("addShape"))
        {
            command.action = PresentationEditAction::AddShape;
            command.geometry =
                narrow(options.value(QStringLiteral("geometry"), QStringLiteral("rect")).toString());
            option(options, QStringLiteral("x"), command.x);
            option(options, QStringLiteral("y"), command.y);
            option(options, QStringLiteral("width"), command.width);
            option(options, QStringLiteral("height"), command.height);
        }
        else if (action == QStringLiteral("updateText"))
        {
            command.action = PresentationEditAction::UpdateText;
            command.text = narrow(options.value(QStringLiteral("text")).toString());
        }
        else if (action == QStringLiteral("replaceTextMatches"))
        {
            command.action = PresentationEditAction::ReplaceTextMatches;
            command.find_query = narrow(options.value(QStringLiteral("query")).toString());
            command.find_replacement = narrow(options.value(QStringLiteral("replacement")).toString());
            command.find_case_sensitive = options.value(QStringLiteral("caseSensitive")).toBool();
            command.find_all = options.value(QStringLiteral("scope")).toString() == QStringLiteral("all");
            if (!command.find_all)
            {
                command.slide_index =
                    static_cast<std::size_t>(options.value(QStringLiteral("slideIndex"), -1).toInt());
                command.shape_index =
                    static_cast<std::size_t>(options.value(QStringLiteral("shapeIndex"), -1).toInt());
                command.find_shape_id = options.value(QStringLiteral("shapeId")).toString().toULongLong();
                command.find_paragraph_index =
                    static_cast<std::size_t>(options.value(QStringLiteral("paragraphIndex"), -1).toInt());
                command.find_start_byte =
                    static_cast<std::size_t>(options.value(QStringLiteral("startByte"), -1).toInt());
            }
        }
        else if (action == QStringLiteral("formatTextStyle"))
        {
            command.action = PresentationEditAction::FormatTextStyle;
        }
        else if (action == QStringLiteral("formatText"))
        {
            command.action = PresentationEditAction::FormatText;
            option(options, QStringLiteral("fontFamily"), command.font_family);
            option(options, QStringLiteral("fontSize"), command.font_size);
            option(options, QStringLiteral("bold"), command.bold);
            option(options, QStringLiteral("italic"), command.italic);
            option(options, QStringLiteral("underline"), command.underline);
            option(options, QStringLiteral("strike"), command.strike);
            option(options, QStringLiteral("characterSpacing"), command.character_spacing);
            option(options, QStringLiteral("baseline"), command.baseline);
            option(options, QStringLiteral("textColor"), command.text_color);
            option(options, QStringLiteral("alignment"), command.alignment);
            option(options, QStringLiteral("bullet"), command.bullet);
        }
        else if (action == QStringLiteral("resetTextInheritance"))
        {
            command.action = PresentationEditAction::ResetTextInheritance;
            const auto property = options.value(QStringLiteral("property")).toString();
            if (property == QStringLiteral("fontFamily"))
                command.reset_text_property = PresentationTextProperty::FontFamily;
            else if (property == QStringLiteral("fontSize"))
                command.reset_text_property = PresentationTextProperty::FontSize;
            else if (property == QStringLiteral("color"))
                command.reset_text_property = PresentationTextProperty::Color;
        }
        else if (action == QStringLiteral("formatParagraph"))
        {
            command.action = PresentationEditAction::FormatParagraph;
            option(options, QStringLiteral("alignment"), command.alignment);
            option(options, QStringLiteral("bullet"), command.bullet);
            option(options, QStringLiteral("numbered"), command.numbered);
            option(options, QStringLiteral("numberStart"), command.number_start);
            option(options, QStringLiteral("listLevel"), command.list_level);
            option(options, QStringLiteral("paragraphIndex"), command.paragraph_index);
            option(options, QStringLiteral("marginLeft"), command.paragraph_margin_left);
            option(options, QStringLiteral("firstLineIndent"), command.first_line_indent);
            option(options, QStringLiteral("lineSpacing"), command.line_spacing);
            option(options, QStringLiteral("spaceBefore"), command.space_before);
            option(options, QStringLiteral("spaceAfter"), command.space_after);
        }
        else if (action == QStringLiteral("formatTextBox"))
        {
            command.action = PresentationEditAction::FormatTextBox;
            option(options, QStringLiteral("insetLeft"), command.inset_left);
            option(options, QStringLiteral("insetRight"), command.inset_right);
            option(options, QStringLiteral("insetTop"), command.inset_top);
            option(options, QStringLiteral("insetBottom"), command.inset_bottom);
            option(options, QStringLiteral("verticalAlignment"), command.vertical_alignment);
            option(options, QStringLiteral("wrap"), command.wrap);
            option(options, QStringLiteral("autoFit"), command.auto_fit);
        }
        else if (action == QStringLiteral("formatShape"))
        {
            command.action = PresentationEditAction::FormatShape;
            option(options, QStringLiteral("fillColor"), command.fill_color);
            option(options, QStringLiteral("fillOpacity"), command.fill_opacity);
            option(options, QStringLiteral("gradientStartColor"), command.gradient_start_color);
            option(options, QStringLiteral("gradientEndColor"), command.gradient_end_color);
            option(options, QStringLiteral("gradientAngle"), command.gradient_angle);
            option(options, QStringLiteral("fillPattern"), command.fill_pattern);
            option(options, QStringLiteral("patternForegroundColor"), command.pattern_foreground_color);
            option(options, QStringLiteral("patternBackgroundColor"), command.pattern_background_color);
            option(options, QStringLiteral("outlineColor"), command.outline_color);
            option(options, QStringLiteral("outlineOpacity"), command.outline_opacity);
            option(options, QStringLiteral("outlineWidth"), command.outline_width);
            option(options, QStringLiteral("lineDash"), command.line_dash);
            option(options, QStringLiteral("lineHead"), command.line_head);
            option(options, QStringLiteral("lineTail"), command.line_tail);
            option(options, QStringLiteral("shadowEnabled"), command.shadow_enabled);
            option(options, QStringLiteral("shadowColor"), command.shadow_color);
            option(options, QStringLiteral("shadowOpacity"), command.shadow_opacity);
            option(options, QStringLiteral("shadowBlur"), command.shadow_blur);
            option(options, QStringLiteral("shadowX"), command.shadow_x);
            option(options, QStringLiteral("shadowY"), command.shadow_y);
            option(options, QStringLiteral("glowEnabled"), command.glow_enabled);
            option(options, QStringLiteral("glowColor"), command.glow_color);
            option(options, QStringLiteral("glowOpacity"), command.glow_opacity);
            option(options, QStringLiteral("glowRadius"), command.glow_radius);
        }
        else if (action == QStringLiteral("resetPlaceholderFill"))
        {
            command.action = PresentationEditAction::ResetPlaceholderFill;
        }
        else if (action == QStringLiteral("resetPlaceholderOutline"))
        {
            command.action = PresentationEditAction::ResetPlaceholderOutline;
        }
        else if (action == QStringLiteral("formatImage"))
        {
            command.action = PresentationEditAction::FormatImage;
            option(options, QStringLiteral("cropLeft"), command.image_crop_left);
            option(options, QStringLiteral("cropTop"), command.image_crop_top);
            option(options, QStringLiteral("cropRight"), command.image_crop_right);
            option(options, QStringLiteral("cropBottom"), command.image_crop_bottom);
            option(options, QStringLiteral("opacity"), command.image_opacity);
        }
        else if (action == QStringLiteral("formatTableCell"))
        {
            command.action = PresentationEditAction::FormatTableCell;
            option(options, QStringLiteral("fillColor"), command.fill_color);
            option(options, QStringLiteral("fillOpacity"), command.fill_opacity);
        }
        else if (action == QStringLiteral("formatTableStyle"))
        {
            command.action = PresentationEditAction::FormatTableStyle;
            command.table_style_options = table_style_options(options);
        }
        else if (action == QStringLiteral("resetTableCellFill"))
        {
            command.action = PresentationEditAction::ResetTableCellFill;
        }
        else if (action == QStringLiteral("moveGroup"))
        {
            command.action = PresentationEditAction::MoveGroup;
            command.group_id = narrow(options.value(QStringLiteral("groupId")).toString());
            option(options, QStringLiteral("x"), command.x);
            option(options, QStringLiteral("y"), command.y);
        }
        else if (action == QStringLiteral("reorderGroup"))
        {
            command.action = PresentationEditAction::ReorderGroup;
            command.group_id = narrow(options.value(QStringLiteral("groupId")).toString());
            command.group_layer_position = narrow(options.value(QStringLiteral("position")).toString());
        }
        else if (action == QStringLiteral("applyTheme"))
        {
            command.action = PresentationEditAction::ApplyTheme;
            const auto colors = options.value(QStringLiteral("colors")).toMap();
            for (auto item = colors.constBegin(); item != colors.constEnd(); ++item)
                command.theme_colors.emplace(narrow(item.key()), narrow(item.value().toString()));
            const auto fonts = options.value(QStringLiteral("fonts")).toMap();
            for (auto item = fonts.constBegin(); item != fonts.constEnd(); ++item)
                command.theme_fonts.emplace(narrow(item.key()), narrow(item.value().toString()));
        }
        else if (action == QStringLiteral("insertTable"))
        {
            command.action = PresentationEditAction::InsertTable;
            command.table_rows = options.value(QStringLiteral("rows")).toInt();
            command.table_columns = options.value(QStringLiteral("columns")).toInt();
            option(options, QStringLiteral("x"), command.x);
            option(options, QStringLiteral("y"), command.y);
            option(options, QStringLiteral("width"), command.width);
            option(options, QStringLiteral("height"), command.height);
        }
        else if (action == QStringLiteral("insertTableRow"))
            command.action = PresentationEditAction::InsertTableRow;
        else if (action == QStringLiteral("insertTableColumn"))
            command.action = PresentationEditAction::InsertTableColumn;
        else if (action == QStringLiteral("deleteTableRow"))
            command.action = PresentationEditAction::DeleteTableRow;
        else if (action == QStringLiteral("deleteTableColumn"))
            command.action = PresentationEditAction::DeleteTableColumn;
        else if (action == QStringLiteral("mergeTableCell"))
        {
            command.action = PresentationEditAction::MergeTableCell;
            command.table_direction = narrow(options.value(QStringLiteral("direction")).toString());
        }
        else if (action == QStringLiteral("unmergeTableCell"))
            command.action = PresentationEditAction::UnmergeTableCell;
        else if (action == QStringLiteral("formatTableBorder"))
        {
            command.action = PresentationEditAction::FormatTableBorder;
            option(options, QStringLiteral("color"), command.outline_color);
            option(options, QStringLiteral("width"), command.outline_width);
            if (options.contains(QStringLiteral("edge")))
                command.table_edge = narrow(options.value(QStringLiteral("edge")).toString());
        }
        else if (action == QStringLiteral("resetTableBorder"))
        {
            command.action = PresentationEditAction::ResetTableBorder;
            if (options.contains(QStringLiteral("edge")))
                command.table_edge = narrow(options.value(QStringLiteral("edge")).toString());
        }
        else if (action == QStringLiteral("groupAdjacent"))
        {
            command.action = PresentationEditAction::GroupAdjacent;
            if (options.contains(QStringLiteral("targetIndex")))
            {
                bool valid = false;
                const int target = options.value(QStringLiteral("targetIndex")).toInt(&valid);
                if (valid && target >= 0)
                    command.target_index = static_cast<std::size_t>(target);
            }
        }
        else if (action == QStringLiteral("addToGroup"))
        {
            command.action = PresentationEditAction::AddToGroup;
            command.group_id = narrow(options.value(QStringLiteral("groupId")).toString());
            if (options.contains(QStringLiteral("targetIndex")))
            {
                bool valid = false;
                const int target = options.value(QStringLiteral("targetIndex")).toInt(&valid);
                if (valid && target >= 0)
                    command.target_index = static_cast<std::size_t>(target);
            }
        }
        else if (action == QStringLiteral("ungroup"))
        {
            command.action = PresentationEditAction::Ungroup;
            command.group_id = narrow(options.value(QStringLiteral("groupId")).toString());
        }
        else if (action == QStringLiteral("transformShape"))
        {
            command.action = PresentationEditAction::TransformShape;
            option(options, QStringLiteral("x"), command.x);
            option(options, QStringLiteral("y"), command.y);
            option(options, QStringLiteral("width"), command.width);
            option(options, QStringLiteral("height"), command.height);
            option(options, QStringLiteral("rotation"), command.rotation);
            option(options, QStringLiteral("flipHorizontal"), command.flip_horizontal);
            option(options, QStringLiteral("flipVertical"), command.flip_vertical);
            option(options, QStringLiteral("preserveAspect"), command.preserve_aspect);
        }
        else if (action == QStringLiteral("deleteShape"))
        {
            command.action = PresentationEditAction::DeleteShape;
        }
        else if (action == QStringLiteral("duplicateShape"))
        {
            command.action = PresentationEditAction::DuplicateShape;
        }
        else if (action == QStringLiteral("moveShape"))
        {
            command.action = PresentationEditAction::MoveShape;
            command.offset = options.value(QStringLiteral("offset")).toInt();
            if (options.contains(QStringLiteral("targetIndex")))
            {
                bool valid = false;
                const int target = options.value(QStringLiteral("targetIndex")).toInt(&valid);
                if (valid && target >= 0)
                {
                    command.target_index = static_cast<std::size_t>(target);
                }
            }
        }
        else if (action == QStringLiteral("alignShape"))
        {
            command.action = PresentationEditAction::AlignShape;
            option(options, QStringLiteral("alignment"), command.alignment);
        }
        else if (action == QStringLiteral("slideHidden"))
        {
            command.action = PresentationEditAction::SetSlideHidden;
            option(options, QStringLiteral("hidden"), command.hidden);
        }
        else if (action == QStringLiteral("setSlideTransition"))
        {
            command.action = PresentationEditAction::SetSlideTransition;
            const QStringList required{QStringLiteral("type"), QStringLiteral("direction"),
                QStringLiteral("durationSeconds"), QStringLiteral("advanceOnClick"),
                QStringLiteral("advanceAfterSeconds")};
            if (std::all_of(required.begin(), required.end(), [&options](const QString& key)
            {
                return options.contains(key);
            }))
            {
                bool duration_valid = false;
                bool timer_valid = false;
                PresentationTransition transition;
                transition.type = narrow(options.value(QStringLiteral("type")).toString());
                transition.direction = narrow(options.value(QStringLiteral("direction")).toString());
                transition.duration =
                    options.value(QStringLiteral("durationSeconds")).toDouble(&duration_valid);
                transition.advance_after =
                    options.value(QStringLiteral("advanceAfterSeconds")).toDouble(&timer_valid);
                if (duration_valid && timer_valid &&
                    options.value(QStringLiteral("advanceOnClick")).typeId() == QMetaType::Bool)
                {
                    transition.advance_on_click = options.value(QStringLiteral("advanceOnClick")).toBool();
                    command.slide_transition = transition;
                }
            }
        }
        else if (action == QStringLiteral("background"))
        {
            command.action = PresentationEditAction::SetBackground;
            option(options, QStringLiteral("color"), command.background_color);
        }
        return command;
    }

}
