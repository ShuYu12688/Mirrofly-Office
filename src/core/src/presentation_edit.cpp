#include "presentation_edit_common.hpp"
#include "presentation_edit_layout.hpp"
#include "presentation_edit_native.hpp"
#include "presentation_find_replace.hpp"
#include "presentation_format_brush.hpp"
#include "presentation_preservation.hpp"
#include "presentation_sections.hpp"
#include "presentation_table_edges.hpp"
#include "presentation_table_style.hpp"
#include "presentation_templates.hpp"
#include "presentation_text_style.hpp"
#include "presentation_theme_package.hpp"
#include "presentation_transition.hpp"
#include <mirrorfly/presentation.hpp>
#include <mirrorfly/presentation_geometry.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace
{
    using mirrorfly::presentation_edit_common::finite;
    using mirrorfly::presentation_edit_common::image_extension;
    using mirrorfly::presentation_edit_common::saturated_add;
    using mirrorfly::presentation_edit_common::valid_xml_text;
    using mirrorfly::presentation_edit_native::compose_transform;
    using mirrorfly::presentation_edit_native::decompose_transform;
    using mirrorfly::presentation_edit_native::emu;
    using mirrorfly::presentation_edit_native::shape_xml;
    using mirrorfly::presentation_edit_native::solid_fill;
    using mirrorfly::presentation_edit_native::transformed_bounds;
    using mirrorfly::presentation_edit_native::valid_color;

    std::uint64_t next_id(std::uint64_t& next_shape_id)
    {
        if (next_shape_id == 0)
        {
            next_shape_id = 1;
        }
        return next_shape_id++;
    }

    mirrorfly::PresentationEditResult edit_failure(
        mirrorfly::PresentationEditError error, const std::string& message)
    {
        mirrorfly::PresentationEditResult result;
        result.error = error;
        result.message = message;
        return result;
    }

    bool valid_geometry(const std::string& geometry)
    {
        static const auto geometries = []()
        {
            const auto names = mirrorfly::presentation_geometry_presets();
            return std::set<std::string>(names.begin(), names.end());
        }();
        return geometries.count(geometry) != 0;
    }

    bool valid_alignment(const std::string& alignment)
    {
        static const std::set<std::string> alignments{"left", "center", "right", "justify"};
        return alignments.count(alignment) != 0;
    }

    bool valid_vertical_alignment(const std::string& alignment)
    {
        static const std::set<std::string> alignments{"top", "center", "bottom"};
        return alignments.count(alignment) != 0;
    }

    bool valid_line_dash(const std::string& dash)
    {
        const auto& dashes = mirrorfly::presentation_line_dash_presets();
        return std::find(dashes.begin(), dashes.end(), dash) != dashes.end();
    }

    bool valid_line_end(const std::string& end)
    {
        static const std::set<std::string> ends{"none", "triangle", "stealth", "diamond", "oval", "arrow"};
        return ends.count(end) != 0;
    }

    std::vector<double> line_dashes(const std::string& dash)
    {
        return mirrorfly::presentation_line_dash_pattern(dash);
    }

    bool valid_shape_alignment(const std::string& alignment)
    {
        static const std::set<std::string> alignments{"left", "center", "right", "top", "middle", "bottom"};
        return alignments.count(alignment) != 0;
    }

    std::size_t shape_text_bytes(const mirrorfly::PresentationShape& shape)
    {
        std::size_t total = 0;
        for (const auto& paragraph : shape.text.paragraphs)
        {
            for (const auto& run : paragraph.runs)
            {
                total = saturated_add(total, run.text.size());
            }
        }
        return total;
    }

    std::size_t slide_text_bytes(const mirrorfly::PresentationSlide& slide)
    {
        std::size_t total = 0;
        for (const auto& shape : slide.shapes)
        {
            total = saturated_add(total, shape_text_bytes(shape));
        }
        return total;
    }

    std::size_t total_text_bytes(const mirrorfly::PresentationScene& scene)
    {
        std::size_t total = 0;
        for (const auto& slide : scene.slides)
        {
            total = saturated_add(total, slide_text_bytes(slide));
        }
        return total;
    }

    std::size_t total_shapes(const mirrorfly::PresentationScene& scene)
    {
        std::size_t total = 0;
        for (const auto& slide : scene.slides)
        {
            total = saturated_add(total, slide.shapes.size());
        }
        return total;
    }

    bool exceeds_budget(std::size_t current, std::size_t added, std::size_t maximum)
    {
        return current > maximum || added > maximum - current;
    }

    std::size_t total_image_bytes(const mirrorfly::PresentationScene& scene)
    {
        std::size_t total = 0;
        for (const auto& image : scene.images)
        {
            if (image.bytes)
            {
                total = saturated_add(total, image.bytes->size());
            }
        }
        return total;
    }

    void prune_images(mirrorfly::PresentationScene& scene)
    {
        std::set<std::string> used;
        for (const auto& slide : scene.slides)
        {
            if (!slide.background.image_path.empty())
            {
                used.insert(slide.background.image_path);
            }
            for (const auto& shape : slide.shapes)
            {
                if (!shape.image_path.empty())
                {
                    used.insert(shape.image_path);
                }
                if (!shape.fill.image_path.empty())
                {
                    used.insert(shape.fill.image_path);
                }
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    if (!paragraph.bullet_image_path.empty())
                    {
                        used.insert(paragraph.bullet_image_path);
                    }
                }
            }
        }
        const auto unused = [&used](const auto& image)
        {
            return !used.count(image.path);
        };
        scene.images.erase(
            std::remove_if(scene.images.begin(), scene.images.end(), unused), scene.images.end());
    }

    std::size_t image_reference_count(const mirrorfly::PresentationScene& scene, const std::string& path)
    {
        std::size_t references = 0;
        for (const auto& slide : scene.slides)
        {
            references += slide.background.image_path == path ? 1 : 0;
            for (const auto& shape : slide.shapes)
            {
                references += shape.image_path == path ? 1 : 0;
                references += shape.fill.image_path == path ? 1 : 0;
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    references += paragraph.bullet_image_path == path ? 1 : 0;
                }
            }
        }
        return references;
    }

}

namespace mirrorfly
{
    PresentationScene make_presentation(
        PresentationSlideLayout layout, const PresentationTemplatePalette& palette)
    {
        PresentationScene scene;
        scene.width = 960;
        scene.height = 540;
        scene.native_editable = true;
        scene.slides.push_back(mirrorfly::presentation_edit_layout::slide_for_layout(scene, layout, palette));
        return scene;
    }

    static PresentationEditResult edit_add_slide(
        PresentationScene& scene, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (command.layout < PresentationSlideLayout::Title ||
            command.layout > PresentationSlideLayout::DeliveryRoadmap)
        {
            return edit_failure(PresentationEditError::InvalidValue, "页面版式无效。");
        }
        const auto& palette = command.template_palette;
        if (!valid_color(palette.ink) || !valid_color(palette.paper) || !valid_color(palette.card) ||
            !valid_color(palette.muted) || !valid_color(palette.accent) || !valid_color(palette.soft))
        {
            return edit_failure(PresentationEditError::InvalidValue, "模板配色无效。");
        }
        PresentationScene candidate;
        candidate.width = scene.width;
        candidate.height = scene.height;
        candidate.next_shape_id = scene.next_shape_id;
        candidate.slides.push_back(mirrorfly::presentation_edit_layout::slide_for_layout(
            candidate, command.layout, command.template_palette));
        const auto existing_shapes = total_shapes(scene);
        const std::size_t added_shapes = candidate.slides.front().shapes.size();
        if (scene.slides.size() >= maximum_presentation_slides || command.slide_index > scene.slides.size() ||
            exceeds_budget(existing_shapes, added_shapes, maximum_presentation_shapes))
        {
            return edit_failure(PresentationEditError::TooLarge, "页面位置无效或页面、对象数量已达上限。");
        }
        if (exceeds_budget(
                total_text_bytes(scene), total_text_bytes(candidate), maximum_presentation_text_bytes))
        {
            return edit_failure(PresentationEditError::TooLarge, "演示文稿文字超过 2 MiB 上限。");
        }
        scene.slides.insert(scene.slides.begin() + static_cast<std::ptrdiff_t>(command.slide_index),
            std::move(candidate.slides.front()));
        assign_inserted_slide_section(scene, command.slide_index);
        scene.next_shape_id = candidate.next_shape_id;
        result.slide_index = command.slide_index;
        return result;
    }

    static PresentationEditResult edit_duplicate_slide(PresentationScene& scene, PresentationSlide& slide,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        const auto existing_shapes = total_shapes(scene);
        if (scene.slides.size() >= maximum_presentation_slides ||
            exceeds_budget(existing_shapes, slide.shapes.size(), maximum_presentation_shapes) ||
            exceeds_budget(total_text_bytes(scene), slide_text_bytes(slide), maximum_presentation_text_bytes))
        {
            return edit_failure(PresentationEditError::TooLarge, "复制页面会超过页面、对象或文字上限。");
        }
        PresentationSlide copy = slide;
        auto candidate_next_shape_id = scene.next_shape_id;
        for (auto& shape : copy.shapes)
        {
            shape.id = next_id(candidate_next_shape_id);
        }
        scene.slides.insert(
            scene.slides.begin() + static_cast<std::ptrdiff_t>(command.slide_index + 1), std::move(copy));
        scene.next_shape_id = candidate_next_shape_id;
        result.slide_index = command.slide_index + 1;
        return result;
    }

    static PresentationEditResult edit_delete_slide(
        PresentationScene& scene, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (scene.slides.size() == 1)
        {
            return edit_failure(PresentationEditError::InvalidValue, "演示文稿至少保留一张幻灯片。");
        }
        scene.slides.erase(scene.slides.begin() + static_cast<std::ptrdiff_t>(command.slide_index));
        reconcile_deleted_slide_sections(scene);
        prune_images(scene);
        result.slide_index = std::min(command.slide_index, scene.slides.size() - 1);
        return result;
    }

    static PresentationEditResult edit_move_slide(
        PresentationScene& scene, const PresentationEditCommand& command, PresentationEditResult result)
    {
        const auto destination = static_cast<long long>(command.slide_index) + command.offset;
        if ((command.offset != -1 && command.offset != 1) || destination < 0 ||
            destination >= static_cast<long long>(scene.slides.size()))
        {
            return edit_failure(PresentationEditError::InvalidIndex, "幻灯片已经位于该方向的边界。");
        }
        std::swap(scene.slides[command.slide_index], scene.slides[static_cast<std::size_t>(destination)]);
        reconcile_moved_slide_section(scene, command.slide_index, static_cast<std::size_t>(destination));
        result.slide_index = static_cast<std::size_t>(destination);
        return result;
    }

    static PresentationEditResult edit_set_background(
        PresentationSlide& slide, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!command.background_color || !valid_color(*command.background_color))
        {
            return edit_failure(PresentationEditError::InvalidValue, "背景颜色必须使用 #RRGGBB。");
        }
        slide.background = PresentationFill{*command.background_color};
        return result;
    }

    static PresentationEditResult edit_set_slide_hidden(
        PresentationSlide& slide, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!command.hidden)
        {
            return edit_failure(PresentationEditError::InvalidValue, "页面隐藏状态无效。");
        }
        slide.hidden = *command.hidden;
        return result;
    }

    static PresentationEditResult edit_set_slide_transition(
        PresentationSlide& slide, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!slide.transition.editable)
            return edit_failure(PresentationEditError::ReadOnly, "当前页面切换包含无法安全覆盖的内容。");
        if (!command.slide_transition || !valid_presentation_transition(*command.slide_transition))
            return edit_failure(PresentationEditError::InvalidValue, "页面切换参数无效。");
        slide.transition = *command.slide_transition;
        slide.transition.editable = true;
        slide.transition.approximate = false;
        return result;
    }

    static PresentationEditResult edit_add_object(PresentationScene& scene, PresentationSlide& slide,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (total_shapes(scene) >= maximum_presentation_shapes)
        {
            return edit_failure(PresentationEditError::TooLarge, "演示文稿中的对象数量已达到上限。");
        }
        const double default_x = command.action == PresentationEditAction::AddText ? 72 : 96;
        const double default_y = command.action == PresentationEditAction::AddText ? 72 : 120;
        const double default_width = command.action == PresentationEditAction::AddText ? 360 : 240;
        const double default_height = command.action == PresentationEditAction::AddText ? 90 : 135;
        const double x = command.x.value_or(default_x);
        const double y = command.y.value_or(default_y);
        const double width = command.width.value_or(default_width);
        const double height = command.height.value_or(default_height);
        if (!finite(x) || !finite(y) || !finite(width) || !finite(height) || width < 1 || height < 1 ||
            std::abs(x) > 20000 || std::abs(y) > 20000 || width > 20000 || height > 20000)
        {
            return edit_failure(PresentationEditError::InvalidValue, "对象位置或尺寸无效。");
        }
        if (command.action == PresentationEditAction::AddText)
        {
            if (!valid_xml_text(command.text))
            {
                return edit_failure(PresentationEditError::InvalidValue, "文字包含无效字符。");
            }
            if (exceeds_budget(total_text_bytes(scene), command.text.size(), maximum_presentation_text_bytes))
            {
                return edit_failure(PresentationEditError::TooLarge, "演示文稿文字超过 2 MiB 上限。");
            }
        }
        const std::string geometry =
            command.action == PresentationEditAction::AddImage ? "rect" : command.geometry;
        if (command.action != PresentationEditAction::AddText && !valid_geometry(geometry))
        {
            return edit_failure(PresentationEditError::InvalidValue, "不支持此基础形状。");
        }
        if (command.action == PresentationEditAction::AddImage &&
            (image_extension(command.image_mime_type).empty() || command.image_bytes.empty() ||
                command.image_bytes.size() > maximum_presentation_part_bytes ||
                exceeds_budget(total_image_bytes(scene), command.image_bytes.size(),
                    maximum_presentation_editable_image_bytes) ||
                !valid_xml_text(command.image_path)))
        {
            return edit_failure(PresentationEditError::InvalidValue, "图片格式无效或超过大小限制。");
        }

        auto candidate_next_shape_id = scene.next_shape_id;
        PresentationShape shape;
        std::optional<PresentationImage> image;
        if (command.action == PresentationEditAction::AddText)
        {
            shape = mirrorfly::presentation_edit_layout::text_box(
                candidate_next_shape_id, "文本框", x, y, width, height, 20);
            mirrorfly::presentation_edit_layout::replace_text(shape, command.text);
        }
        else
        {
            shape.id = next_id(candidate_next_shape_id);
            shape.name = command.action == PresentationEditAction::AddImage ? "图片" : "形状";
            shape.geometry = geometry;
            shape.transform = {1, 0, 0, 1, x, y};
            shape.width = width;
            shape.height = height;
            if (command.action == PresentationEditAction::AddShape)
            {
                auto path = presentation_geometry(geometry, width, height);
                if (!path.error.empty() || path.paths.empty())
                    return edit_failure(PresentationEditError::InvalidValue, "形状路径生成失败。");
                shape.path_geometry = std::make_shared<const PresentationGeometry>(std::move(path));
            }
            shape.fill.color = command.action == PresentationEditAction::AddImage ? "" : "#D7C6B2";
            shape.outline_color = command.action == PresentationEditAction::AddImage ? "" : "#6F7D69";
            if (command.action == PresentationEditAction::AddImage)
            {
                std::string path = command.image_path;
                if (path.empty())
                {
                    path = "mirrorfly-image-" + std::to_string(scene.images.size() + 1);
                }
                while (std::any_of(scene.images.begin(), scene.images.end(), [&path](const auto& image)
                {
                    return image.path == path;
                }))
                {
                    path += "-copy";
                }
                image = PresentationImage{path, command.image_mime_type, command.image_bytes};
                shape.image_path = path;
            }
        }
        slide.shapes.reserve(slide.shapes.size() + 1);
        if (image)
        {
            scene.images.reserve(scene.images.size() + 1);
            scene.images.push_back(std::move(*image));
        }
        slide.shapes.push_back(std::move(shape));
        scene.next_shape_id = candidate_next_shape_id;
        result.shape_index = slide.shapes.size() - 1;
        return result;
    }

    static PresentationEditResult edit_delete_shape(PresentationScene& scene, PresentationSlide& slide,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        slide.shapes.erase(slide.shapes.begin() + static_cast<std::ptrdiff_t>(command.shape_index));
        prune_images(scene);
        result.shape_index.reset();
        return result;
    }

    static PresentationEditResult edit_duplicate_shape(PresentationScene& scene, PresentationSlide& slide,
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (total_shapes(scene) >= maximum_presentation_shapes ||
            exceeds_budget(total_text_bytes(scene), shape_text_bytes(shape), maximum_presentation_text_bytes))
        {
            return edit_failure(PresentationEditError::TooLarge, "复制对象会超过对象或文字上限。");
        }
        PresentationShape copy = shape;
        auto candidate_next_shape_id = scene.next_shape_id;
        copy.id = next_id(candidate_next_shape_id);
        copy.transform[4] += 12;
        copy.transform[5] += 12;
        slide.shapes.insert(
            slide.shapes.begin() + static_cast<std::ptrdiff_t>(command.shape_index + 1), std::move(copy));
        scene.next_shape_id = candidate_next_shape_id;
        result.shape_index = command.shape_index + 1;
        return result;
    }

    static PresentationEditResult edit_move_shape(
        PresentationSlide& slide, const PresentationEditCommand& command, PresentationEditResult result)
    {
        const auto offset_destination = static_cast<long long>(command.shape_index) + command.offset;
        const bool offset_valid = (command.offset == -1 || command.offset == 1) && offset_destination >= 0 &&
            offset_destination < static_cast<long long>(slide.shapes.size());
        const std::size_t destination = command.target_index.value_or(
            offset_valid ? static_cast<std::size_t>(offset_destination) : command.shape_index);
        if ((!command.target_index && !offset_valid) || destination >= slide.shapes.size() ||
            destination == command.shape_index)
        {
            return edit_failure(PresentationEditError::InvalidIndex, "对象已经位于该层次的边界。");
        }
        auto moved = std::move(slide.shapes[command.shape_index]);
        slide.shapes.erase(slide.shapes.begin() + static_cast<std::ptrdiff_t>(command.shape_index));
        slide.shapes.insert(
            slide.shapes.begin() + static_cast<std::ptrdiff_t>(destination), std::move(moved));
        result.shape_index = destination;
        return result;
    }

    static PresentationEditResult edit_update_text(PresentationScene& scene, PresentationShape& shape,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        const auto previous_size = shape_text_bytes(shape);
        const auto current_size = total_text_bytes(scene);
        const auto unchanged_size =
            current_size >= previous_size ? current_size - previous_size : current_size;
        if (!valid_xml_text(command.text) ||
            exceeds_budget(unchanged_size, command.text.size(), maximum_presentation_text_bytes))
        {
            return edit_failure(PresentationEditError::TooLarge, "文字无效或超过 2 MiB 上限。");
        }
        mirrorfly::presentation_edit_layout::replace_text(shape, command.text);
        return result;
    }

    static PresentationEditResult edit_set_click_action(PresentationScene& scene, PresentationShape& shape,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        static const std::set<std::string> kinds{"", "slide", "firstslide", "lastslide", "nextslide",
            "previousslide", "lastslideviewed", "endshow"};
        if (!kinds.count(command.click_kind) ||
            (command.click_kind == "slide" &&
                (!command.click_target_slide || *command.click_target_slide >= scene.slides.size())))
            return edit_failure(PresentationEditError::InvalidValue, "超链接目标无效。");
        shape.click_action = {command.click_kind,
            command.click_kind == "slide" ? static_cast<int>(*command.click_target_slide) : -1, false};
        for (auto& paragraph : shape.text.paragraphs)
            for (auto& run : paragraph.runs)
                run.click_action = {};
        return result;
    }

    static PresentationEditResult edit_format_text_style(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!shape.image_path.empty() || !apply_presentation_text_style(shape.text, command.text_style))
            return edit_failure(PresentationEditError::InvalidValue, "文字效果参数无效。");
        return result;
    }

    static PresentationEditResult edit_format_text(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!shape.image_path.empty())
        {
            return edit_failure(PresentationEditError::InvalidValue, "当前对象不是可编辑文字对象。");
        }
        if ((command.font_family &&
                (!valid_xml_text(*command.font_family) || command.font_family->size() > 256)) ||
            (command.font_size &&
                (!finite(*command.font_size) || *command.font_size < 1 || *command.font_size > 400)) ||
            (command.text_color && !valid_color(*command.text_color)) ||
            (command.alignment && !valid_alignment(*command.alignment)) ||
            (command.character_spacing &&
                (!finite(*command.character_spacing) || *command.character_spacing < -50 ||
                    *command.character_spacing > 200)) ||
            (command.baseline &&
                (!finite(*command.baseline) || *command.baseline < -1 || *command.baseline > 1)))
        {
            return edit_failure(PresentationEditError::InvalidValue, "文字格式无效。");
        }
        if (shape.text.paragraphs.empty())
        {
            mirrorfly::presentation_edit_layout::replace_text(shape, "");
        }
        for (auto& paragraph : shape.text.paragraphs)
        {
            if (command.alignment)
            {
                paragraph.alignment = *command.alignment;
            }
            if (command.bullet)
            {
                paragraph.bullet = *command.bullet ? "•" : "";
                paragraph.numbered = false;
            }
            if (paragraph.runs.empty())
            {
                paragraph.runs.emplace_back();
            }
            for (auto& run : paragraph.runs)
            {
                if (command.font_family)
                {
                    run.font_family = *command.font_family;
                    run.east_asian_font_family = *command.font_family;
                    run.font_family_source = shape.table_cell ? "tableCell" : "slide";
                    run.east_asian_font_source = shape.table_cell ? "tableCell" : "slide";
                    run.local_font_override = true;
                    run.font_theme_slot.clear();
                    run.east_asian_theme_slot.clear();
                }
                if (command.font_size)
                {
                    run.font_size = *command.font_size;
                    run.font_size_source = shape.table_cell ? "tableCell" : "slide";
                    run.local_size_override = true;
                }
                if (command.bold)
                {
                    run.bold = *command.bold;
                }
                if (command.italic)
                {
                    run.italic = *command.italic;
                }
                if (command.underline)
                {
                    run.underline = *command.underline;
                }
                if (command.strike)
                {
                    run.strike = *command.strike;
                }
                if (command.character_spacing)
                {
                    run.spacing = *command.character_spacing;
                }
                if (command.baseline)
                {
                    run.baseline = *command.baseline;
                }
                if (command.text_color)
                {
                    run.color = *command.text_color;
                    run.color_source = shape.table_cell ? "tableCell" : "slide";
                    run.local_color_override = true;
                    run.color_theme_slot.clear();
                    run.opacity = 1;
                    run.fill = {};
                }
            }
        }
        return result;
    }

    static PresentationEditResult edit_format_paragraph(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!shape.image_path.empty())
        {
            return edit_failure(PresentationEditError::InvalidValue, "当前对象不是可编辑文字对象。");
        }
        if ((command.alignment && !valid_alignment(*command.alignment)) ||
            (command.number_start && (*command.number_start < 1 || *command.number_start > 32767)) ||
            (command.list_level && (*command.list_level < 0 || *command.list_level > 8)) ||
            (command.paragraph_index &&
                (*command.paragraph_index < 0 ||
                    *command.paragraph_index >= static_cast<int>(shape.text.paragraphs.size()))) ||
            (command.paragraph_margin_left &&
                (!finite(*command.paragraph_margin_left) || *command.paragraph_margin_left < 0 ||
                    *command.paragraph_margin_left > 1000)) ||
            (command.first_line_indent &&
                (!finite(*command.first_line_indent) || *command.first_line_indent < -1000 ||
                    *command.first_line_indent > 1000)) ||
            (command.line_spacing &&
                (!finite(*command.line_spacing) || *command.line_spacing < 0.5 ||
                    *command.line_spacing > 5)) ||
            (command.space_before &&
                (!finite(*command.space_before) || *command.space_before < 0 ||
                    *command.space_before > 400)) ||
            (command.space_after &&
                (!finite(*command.space_after) || *command.space_after < 0 || *command.space_after > 400)))
        {
            return edit_failure(PresentationEditError::InvalidValue, "段落格式无效。");
        }
        for (std::size_t index = 0; index < shape.text.paragraphs.size(); ++index)
        {
            if (command.paragraph_index && index != static_cast<std::size_t>(*command.paragraph_index))
                continue;
            const auto& paragraph = shape.text.paragraphs[index];
            const double indent = command.first_line_indent.value_or(paragraph.first_line_indent);
            const double level_margin = command.list_level
                ? paragraph.margin_left + 24.0 * (*command.list_level - paragraph.list_level)
                : paragraph.margin_left;
            const double margin = command.paragraph_margin_left.value_or(
                std::clamp(std::max(level_margin, -indent), 0.0, 1000.0));
            if (!finite(margin + indent) || margin + indent < 0)
            {
                return edit_failure(PresentationEditError::InvalidValue, "段落缩进不能越过文本框左边界。");
            }
        }
        if (shape.text.paragraphs.empty())
        {
            mirrorfly::presentation_edit_layout::replace_text(shape, "");
        }
        for (std::size_t index = 0; index < shape.text.paragraphs.size(); ++index)
        {
            if (command.paragraph_index && index != static_cast<std::size_t>(*command.paragraph_index))
                continue;
            auto& paragraph = shape.text.paragraphs[index];
            if (command.alignment)
            {
                paragraph.alignment = *command.alignment;
            }
            if (command.bullet)
            {
                paragraph.bullet = *command.bullet ? "•" : "";
                if (*command.bullet)
                {
                    paragraph.numbered = false;
                }
            }
            if (command.numbered)
            {
                paragraph.numbered = *command.numbered;
                if (*command.numbered)
                {
                    paragraph.bullet.clear();
                }
            }
            if (command.number_start)
            {
                paragraph.number_start = *command.number_start;
            }
            if (command.list_level)
            {
                const double indent = command.first_line_indent.value_or(paragraph.first_line_indent);
                paragraph.margin_left = std::clamp(
                    std::max(
                        paragraph.margin_left + 24.0 * (*command.list_level - paragraph.list_level), -indent),
                    0.0, 1000.0);
                paragraph.list_level = *command.list_level;
            }
            if (command.paragraph_margin_left)
            {
                paragraph.margin_left = *command.paragraph_margin_left;
            }
            if (command.first_line_indent)
            {
                paragraph.first_line_indent = *command.first_line_indent;
            }
            if (command.line_spacing)
            {
                paragraph.line_spacing = *command.line_spacing;
                paragraph.fixed_line_spacing = 0;
            }
            if (command.space_before)
            {
                paragraph.space_before = *command.space_before;
                paragraph.space_before_percent = -1;
            }
            if (command.space_after)
            {
                paragraph.space_after = *command.space_after;
                paragraph.space_after_percent = -1;
            }
        }
        return result;
    }

    static PresentationEditResult edit_format_text_box(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!shape.image_path.empty())
        {
            return edit_failure(PresentationEditError::InvalidValue, "当前对象不是可编辑文字对象。");
        }
        const double left = command.inset_left.value_or(shape.text.inset_left);
        const double right = command.inset_right.value_or(shape.text.inset_right);
        const double top = command.inset_top.value_or(shape.text.inset_top);
        const double bottom = command.inset_bottom.value_or(shape.text.inset_bottom);
        if (!finite(left) || !finite(right) || !finite(top) || !finite(bottom) || left < 0 || right < 0 ||
            top < 0 || bottom < 0 || left > 1000 || right > 1000 || top > 1000 || bottom > 1000 ||
            left + right >= shape.width || top + bottom >= shape.height ||
            (command.vertical_alignment && !valid_vertical_alignment(*command.vertical_alignment)))
        {
            return edit_failure(PresentationEditError::InvalidValue, "文本框格式无效。");
        }
        shape.text.inset_left = left;
        shape.text.inset_right = right;
        shape.text.inset_top = top;
        shape.text.inset_bottom = bottom;
        if (command.vertical_alignment)
        {
            shape.text.vertical_alignment = *command.vertical_alignment;
        }
        if (command.wrap)
        {
            shape.text.wrap = *command.wrap;
        }
        if (command.auto_fit)
        {
            shape.text.auto_fit = *command.auto_fit;
            if (!*command.auto_fit)
            {
                shape.text.font_scale = 1;
                shape.text.line_spacing_reduction = 0;
            }
        }
        return result;
    }

    static PresentationEditResult edit_format_shape(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        const bool has_gradient_start = command.gradient_start_color.has_value();
        const bool has_gradient_end = command.gradient_end_color.has_value();
        const bool pattern_edit =
            command.fill_pattern || command.pattern_foreground_color || command.pattern_background_color;
        const auto pattern = command.fill_pattern
            ? (*command.fill_pattern == "none" ? std::string{} : *command.fill_pattern)
            : shape.fill.pattern;
        if ((command.fill_color && !command.fill_color->empty() && !valid_color(*command.fill_color)) ||
            (command.outline_color && !command.outline_color->empty() &&
                !valid_color(*command.outline_color)) ||
            (command.fill_opacity &&
                (!finite(*command.fill_opacity) || *command.fill_opacity < 0 || *command.fill_opacity > 1)) ||
            (command.outline_opacity &&
                (!finite(*command.outline_opacity) || *command.outline_opacity < 0 ||
                    *command.outline_opacity > 1)) ||
            (command.outline_width &&
                (!finite(*command.outline_width) || *command.outline_width < 0 ||
                    *command.outline_width > 72)) ||
            has_gradient_start != has_gradient_end ||
            (has_gradient_start &&
                (!valid_color(*command.gradient_start_color) || !valid_color(*command.gradient_end_color))) ||
            (command.gradient_angle && !finite(*command.gradient_angle)) ||
            (command.gradient_angle && !has_gradient_start && shape.fill.stops.empty()) ||
            (command.fill_color && has_gradient_start) ||
            (pattern_edit && (command.fill_color || has_gradient_start || command.gradient_angle)) ||
            (pattern_edit && !presentation_pattern_supported(pattern)) ||
            ((command.pattern_foreground_color || command.pattern_background_color) && pattern.empty()) ||
            (command.pattern_foreground_color && !valid_color(*command.pattern_foreground_color)) ||
            (command.pattern_background_color && !valid_color(*command.pattern_background_color)) ||
            (command.line_dash && !valid_line_dash(*command.line_dash)) ||
            (command.line_head && !valid_line_end(*command.line_head)) ||
            (command.line_tail && !valid_line_end(*command.line_tail)) ||
            (command.shadow_color && !command.shadow_color->empty() && !valid_color(*command.shadow_color)) ||
            (command.glow_color && !command.glow_color->empty() && !valid_color(*command.glow_color)) ||
            (command.shadow_opacity &&
                (!finite(*command.shadow_opacity) || *command.shadow_opacity < 0 ||
                    *command.shadow_opacity > 1)) ||
            (command.glow_opacity &&
                (!finite(*command.glow_opacity) || *command.glow_opacity < 0 || *command.glow_opacity > 1)) ||
            (command.shadow_blur &&
                (!finite(*command.shadow_blur) || *command.shadow_blur < 0 || *command.shadow_blur > 72)) ||
            (command.glow_radius &&
                (!finite(*command.glow_radius) || *command.glow_radius < 0 || *command.glow_radius > 72)) ||
            (command.shadow_x && (!finite(*command.shadow_x) || std::abs(*command.shadow_x) > 200)) ||
            (command.shadow_y && (!finite(*command.shadow_y) || std::abs(*command.shadow_y) > 200)))
        {
            return edit_failure(PresentationEditError::InvalidValue, "形状样式无效。");
        }
        auto candidate = shape;
        if (command.fill_color)
        {
            candidate.fill = PresentationFill{*command.fill_color};
        }
        if (has_gradient_start)
        {
            candidate.fill.color.clear();
            candidate.fill.image_path.clear();
            candidate.fill.pattern.clear();
            candidate.fill.pattern_foreground_color.clear();
            candidate.fill.stops = {
                {0, *command.gradient_start_color, 1}, {1, *command.gradient_end_color, 1}};
        }
        if (command.fill_opacity)
        {
            candidate.fill.opacity = *command.fill_opacity;
            if (!candidate.fill.pattern.empty())
                candidate.fill.pattern_foreground_opacity = *command.fill_opacity;
        }
        if (pattern_edit)
        {
            candidate.fill.pattern = pattern;
            if (command.fill_opacity)
                candidate.fill.pattern_foreground_opacity = *command.fill_opacity;
            candidate.fill.stops.clear();
            candidate.fill.image_path.clear();
            if (candidate.fill.color.empty())
                candidate.fill.color = "#FFFFFF";
            if (command.pattern_background_color)
                candidate.fill.color = *command.pattern_background_color;
            if (candidate.fill.pattern_foreground_color.empty())
                candidate.fill.pattern_foreground_color = "#000000";
            if (command.pattern_foreground_color)
                candidate.fill.pattern_foreground_color = *command.pattern_foreground_color;
            candidate.fill.theme_slot.clear();
            candidate.fill.theme_reference_color.clear();
        }
        if (command.gradient_angle)
        {
            candidate.fill.angle_degrees = std::remainder(*command.gradient_angle, 360.0);
        }
        if (candidate.placeholder &&
            (command.fill_color || command.fill_opacity || has_gradient_start || command.gradient_angle ||
                pattern_edit))
        {
            candidate.placeholder->local_fill_override = true;
            candidate.placeholder->fill_source = "slide";
        }
        if (command.outline_color)
        {
            candidate.outline_color = *command.outline_color;
            candidate.outline_fill = {};
        }
        if (command.outline_opacity)
        {
            candidate.outline_opacity = *command.outline_opacity;
            candidate.outline_fill.opacity = *command.outline_opacity;
        }
        if (command.outline_width)
        {
            candidate.outline_width = *command.outline_width;
        }
        if (command.line_dash)
        {
            candidate.line_style.dashes = line_dashes(*command.line_dash);
        }
        if (command.line_head)
        {
            candidate.line_style.head.type = *command.line_head;
        }
        if (command.line_tail)
        {
            candidate.line_style.tail.type = *command.line_tail;
        }
        if (candidate.placeholder &&
            (command.outline_color || command.outline_opacity || command.outline_width || command.line_dash ||
                command.line_head || command.line_tail))
        {
            candidate.placeholder->local_outline_override = true;
            candidate.placeholder->outline_source = "slide";
        }
        if (command.shadow_enabled)
        {
            candidate.effects.shadow_opacity =
                *command.shadow_enabled ? std::max(0.45, candidate.effects.shadow_opacity) : 0;
            if (*command.shadow_enabled && !valid_color(candidate.effects.shadow_color))
            {
                candidate.effects.shadow_color = "#000000";
            }
        }
        if (command.shadow_color)
        {
            candidate.effects.shadow_color = *command.shadow_color;
            if (command.shadow_color->empty())
            {
                candidate.effects.shadow_opacity = 0;
            }
        }
        if (command.shadow_opacity)
            candidate.effects.shadow_opacity = *command.shadow_opacity;
        if (command.shadow_blur)
            candidate.effects.shadow_blur = *command.shadow_blur;
        if (command.shadow_x)
            candidate.effects.shadow_x = *command.shadow_x;
        if (command.shadow_y)
            candidate.effects.shadow_y = *command.shadow_y;
        if (command.glow_enabled)
        {
            candidate.effects.glow_opacity =
                *command.glow_enabled ? std::max(0.45, candidate.effects.glow_opacity) : 0;
            if (*command.glow_enabled && !valid_color(candidate.effects.glow_color))
            {
                candidate.effects.glow_color = "#4B8CFF";
            }
        }
        if (command.glow_color)
        {
            candidate.effects.glow_color = *command.glow_color;
            if (command.glow_color->empty())
            {
                candidate.effects.glow_opacity = 0;
            }
        }
        if (command.glow_opacity)
            candidate.effects.glow_opacity = *command.glow_opacity;
        if (command.glow_radius)
            candidate.effects.glow_radius = *command.glow_radius;
        if (command.shadow_enabled || command.shadow_color || command.shadow_opacity || command.shadow_blur ||
            command.shadow_x || command.shadow_y || command.glow_enabled || command.glow_color ||
            command.glow_opacity || command.glow_radius)
        {
            candidate.effects_source = "direct";
        }
        shape = std::move(candidate);
        return result;
    }

    static PresentationEditResult edit_format_table_cell(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!shape.table_cell || !command.fill_color || !valid_color(*command.fill_color) ||
            (command.fill_opacity &&
                (!finite(*command.fill_opacity) || *command.fill_opacity < 0 || *command.fill_opacity > 1)) ||
            command.outline_color || command.outline_opacity || command.outline_width ||
            command.gradient_start_color || command.gradient_end_color || command.gradient_angle ||
            command.fill_pattern || command.pattern_foreground_color || command.pattern_background_color ||
            command.line_dash || command.line_head || command.line_tail || command.shadow_enabled ||
            command.glow_enabled)
        {
            return edit_failure(PresentationEditError::InvalidValue, "表格单元格底色参数无效。");
        }
        shape.fill = PresentationFill{*command.fill_color};
        shape.fill.opacity = command.fill_opacity.value_or(1);
        shape.table_cell->local_fill_override = true;
        return result;
    }

    static PresentationEditResult edit_format_image(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (shape.image_path.empty())
        {
            return edit_failure(PresentationEditError::InvalidValue, "当前对象不是可编辑图片。");
        }
        auto crop = shape.image_crop;
        crop[0] = command.image_crop_left.value_or(crop[0]);
        crop[1] = command.image_crop_top.value_or(crop[1]);
        crop[2] = command.image_crop_right.value_or(crop[2]);
        crop[3] = command.image_crop_bottom.value_or(crop[3]);
        const double opacity = command.image_opacity.value_or(shape.image_opacity);
        const bool crop_valid = std::all_of(crop.begin(), crop.end(), [](double value)
        {
            return finite(value) && value >= 0 && value <= 0.95;
        });
        if (!crop_valid || crop[0] + crop[2] >= 1 || crop[1] + crop[3] >= 1 || !finite(opacity) ||
            opacity < 0 || opacity > 1)
        {
            return edit_failure(PresentationEditError::InvalidValue, "图片裁剪或透明度无效。");
        }
        shape.image_crop = crop;
        shape.image_opacity = opacity;
        return result;
    }

    static PresentationEditResult edit_replace_image(PresentationScene& scene, PresentationShape& shape,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (shape.image_path.empty())
        {
            return edit_failure(PresentationEditError::InvalidValue, "当前对象不是可替换图片。");
        }
        const auto previous =
            std::find_if(scene.images.begin(), scene.images.end(), [&shape](const auto& image)
        {
            return image.path == shape.image_path;
        });
        if (previous == scene.images.end() || image_extension(command.image_mime_type).empty() ||
            command.image_bytes.empty() || command.image_bytes.size() > maximum_presentation_part_bytes ||
            !valid_xml_text(command.image_path))
        {
            return edit_failure(PresentationEditError::InvalidValue, "替换图片格式无效或资源缺失。");
        }
        const bool remove_previous = image_reference_count(scene, shape.image_path) == 1;
        const std::size_t retained_bytes =
            total_image_bytes(scene) - (remove_previous && previous->bytes ? previous->bytes->size() : 0);
        if (exceeds_budget(
                retained_bytes, command.image_bytes.size(), maximum_presentation_editable_image_bytes))
        {
            return edit_failure(PresentationEditError::TooLarge, "替换图片会超过图片资源上限。");
        }
        std::string path = command.image_path.empty()
            ? "mirrorfly-image-" + std::to_string(scene.images.size() + 1)
            : command.image_path;
        while (std::any_of(scene.images.begin(), scene.images.end(), [&path](const auto& image)
        {
            return image.path == path;
        }))
        {
            path += "-copy";
        }
        PresentationShape candidate = shape;
        candidate.image_path = path;
        PresentationImage replacement{path, command.image_mime_type, command.image_bytes};
        const std::string previous_path = shape.image_path;
        scene.images.reserve(scene.images.size() + 1);
        scene.images.push_back(std::move(replacement));
        shape = std::move(candidate);
        if (remove_previous)
        {
            const auto previous_resource = [&previous_path](const auto& image)
            {
                return image.path == previous_path;
            };
            scene.images.erase(std::remove_if(scene.images.begin(), scene.images.end(), previous_resource),
                scene.images.end());
        }
        return result;
    }

    static PresentationEditResult edit_transform_shape(
        PresentationShape& shape, const PresentationEditCommand& command, PresentationEditResult result)
    {
        const double x = command.x.value_or(shape.transform[4]);
        const double y = command.y.value_or(shape.transform[5]);
        double width = command.width.value_or(shape.width);
        double height = command.height.value_or(shape.height);
        if (command.preserve_aspect.value_or(false))
        {
            if (command.width && command.height)
            {
                return edit_failure(PresentationEditError::InvalidValue, "锁定宽高比时请只修改宽度或高度。");
            }
            if (command.width)
            {
                height = shape.height * width / shape.width;
            }
            else if (command.height)
            {
                width = shape.width * height / shape.height;
            }
        }
        if (!finite(x) || !finite(y) || !finite(width) || !finite(height) ||
            (command.rotation && !finite(*command.rotation)) || width < 1 || height < 1 ||
            std::abs(x) > 20000 || std::abs(y) > 20000 || width > 20000 || height > 20000)
        {
            return edit_failure(PresentationEditError::InvalidValue, "对象位置或尺寸无效。");
        }
        auto candidate = shape;
        candidate.transform[4] = x;
        candidate.transform[5] = y;
        candidate.width = width;
        candidate.height = height;
        if (command.rotation || command.flip_horizontal.value_or(false) ||
            command.flip_vertical.value_or(false))
        {
            auto transform = decompose_transform(candidate);
            if (!transform)
            {
                return edit_failure(PresentationEditError::InvalidValue, "对象变换无效，不能旋转或翻转。");
            }
            if (command.rotation)
            {
                transform->angle = std::remainder(*command.rotation, 360.0);
            }
            if (command.flip_horizontal.value_or(false))
            {
                transform->angle = std::remainder(transform->angle + 180.0, 360.0);
                transform->flip_vertical = !transform->flip_vertical;
            }
            if (command.flip_vertical.value_or(false))
            {
                transform->flip_vertical = !transform->flip_vertical;
            }
            compose_transform(candidate, *transform);
        }
        if (candidate.path_geometry)
        {
            auto geometry = presentation_geometry(
                candidate.geometry, candidate.width, candidate.height, candidate.geometry_definition);
            if (geometry.error.empty())
                candidate.path_geometry = std::make_shared<const PresentationGeometry>(std::move(geometry));
        }
        shape = std::move(candidate);
        return result;
    }

    static PresentationEditResult edit_align_shape(PresentationScene& scene, PresentationShape& shape,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (!command.alignment || !valid_shape_alignment(*command.alignment) || !finite(scene.width) ||
            !finite(scene.height) || scene.width <= 0 || scene.height <= 0)
        {
            return edit_failure(PresentationEditError::InvalidValue, "对象对齐方式或页面尺寸无效。");
        }
        const auto bounds = transformed_bounds(shape);
        if (!bounds)
        {
            return edit_failure(PresentationEditError::InvalidValue, "对象变换无效，不能对齐。");
        }
        double delta_x = 0;
        double delta_y = 0;
        if (*command.alignment == "left")
        {
            delta_x = -bounds->left;
        }
        else if (*command.alignment == "center")
        {
            delta_x = scene.width / 2 - (bounds->left + bounds->right) / 2;
        }
        else if (*command.alignment == "right")
        {
            delta_x = scene.width - bounds->right;
        }
        else if (*command.alignment == "top")
        {
            delta_y = -bounds->top;
        }
        else if (*command.alignment == "middle")
        {
            delta_y = scene.height / 2 - (bounds->top + bounds->bottom) / 2;
        }
        else
        {
            delta_y = scene.height - bounds->bottom;
        }
        if (!finite(delta_x) || !finite(delta_y) || !finite(shape.transform[4] + delta_x) ||
            !finite(shape.transform[5] + delta_y))
        {
            return edit_failure(PresentationEditError::InvalidValue, "对象对齐结果无效。");
        }
        shape.transform[4] += delta_x;
        shape.transform[5] += delta_y;
        return result;
    }

    static PresentationEditResult apply_shape_edit(PresentationScene& scene, PresentationSlide& slide,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (command.shape_index >= slide.shapes.size())
        {
            return edit_failure(PresentationEditError::InvalidIndex, "对象索引无效。");
        }
        auto& shape = slide.shapes[command.shape_index];
        const auto capabilities = presentation_edit_capabilities(shape);
        if (std::find(capabilities.begin(), capabilities.end(), command.action) == capabilities.end())
            return edit_failure(PresentationEditError::ReadOnly, "此对象不支持该操作；原始结构保持不变。");
        result.shape_index = command.shape_index;
        switch (command.action)
        {
        case PresentationEditAction::DeleteShape:
            return edit_delete_shape(scene, slide, command, result);
        case PresentationEditAction::DuplicateShape:
            return edit_duplicate_shape(scene, slide, shape, command, result);
        case PresentationEditAction::MoveShape:
            return edit_move_shape(slide, command, result);
        case PresentationEditAction::UpdateText:
            return edit_update_text(scene, shape, command, result);
        case PresentationEditAction::SetClickAction:
            return edit_set_click_action(scene, shape, command, result);
        case PresentationEditAction::ApplyFormat:
        {
            if (command.format_source_slide >= scene.slides.size() ||
                command.format_source_shape >= scene.slides[command.format_source_slide].shapes.size() ||
                (command.format_source_slide == command.slide_index &&
                    command.format_source_shape == command.shape_index))
                return edit_failure(PresentationEditError::InvalidIndex, "格式刷源对象无效。");
            const auto& source =
                scene.slides[command.format_source_slide].shapes[command.format_source_shape];
            if (!apply_presentation_format_brush(shape, source))
                return edit_failure(
                    PresentationEditError::ReadOnly, "当前对象包含无法安全复制的图片填充或复杂样式。");
            if (shape.placeholder && source.image_path.empty())
            {
                shape.placeholder->local_fill_override = true;
                shape.placeholder->fill_source = "slide";
                shape.placeholder->local_outline_override = true;
                shape.placeholder->outline_source = "slide";
            }
            return result;
        }
        case PresentationEditAction::FormatTextStyle:
            return edit_format_text_style(shape, command, result);
        case PresentationEditAction::FormatText:
            return edit_format_text(shape, command, result);
        case PresentationEditAction::ResetTextInheritance:
        {
            if (!scene.source_package || shape.source_part != slide.source_part ||
                !command.reset_text_property)
                return edit_failure(PresentationEditError::ReadOnly, "此对象不能恢复文字继承。");
            const auto local = presentation_text_local_overrides(shape);
            bool available = false;
            switch (*command.reset_text_property)
            {
            case PresentationTextProperty::FontFamily:
                available = local.font_family;
                break;
            case PresentationTextProperty::FontSize:
                available = local.font_size;
                break;
            case PresentationTextProperty::Color:
                available = local.color;
                break;
            }
            if (!available)
                return edit_failure(PresentationEditError::InvalidValue, "指定文字属性没有本页直接覆盖。");
            return result;
        }
        case PresentationEditAction::FormatParagraph:
            return edit_format_paragraph(shape, command, result);
        case PresentationEditAction::FormatTextBox:
            return edit_format_text_box(shape, command, result);
        case PresentationEditAction::FormatShape:
            return edit_format_shape(shape, command, result);
        case PresentationEditAction::ResetPlaceholderFill:
            if (!scene.source_package || shape.source_part != slide.source_part)
                return edit_failure(PresentationEditError::ReadOnly, "仅可恢复当前页的占位符继承。");
            shape.fill = shape.placeholder->inherited_fill;
            shape.placeholder->local_fill_override = false;
            shape.placeholder->fill_source = shape.placeholder->inherited_fill_source;
            return result;
        case PresentationEditAction::ResetPlaceholderOutline:
            if (!scene.source_package || shape.source_part != slide.source_part)
                return edit_failure(PresentationEditError::ReadOnly, "仅可恢复当前页的占位符继承。");
            shape.outline_color = shape.placeholder->inherited_outline.color;
            shape.outline_fill = shape.placeholder->inherited_outline.fill;
            shape.outline_opacity = shape.placeholder->inherited_outline.opacity;
            shape.outline_width = shape.placeholder->inherited_outline.width;
            shape.line_style = shape.placeholder->inherited_outline.line_style;
            shape.placeholder->local_outline_override = false;
            shape.placeholder->outline_source = shape.placeholder->inherited_outline_source;
            return result;
        case PresentationEditAction::FormatTableCell:
            return edit_format_table_cell(shape, command, result);
        case PresentationEditAction::ResetTableCellFill:
            if (!scene.source_package || shape.source_part != slide.source_part ||
                !shape.table_cell->local_fill_override)
                return edit_failure(PresentationEditError::ReadOnly, "此单元格没有可恢复的直接底色。");
            shape.fill = shape.table_cell->inherited_fill;
            shape.table_cell->local_fill_override = false;
            return result;
        case PresentationEditAction::FormatImage:
            return edit_format_image(shape, command, result);
        case PresentationEditAction::ReplaceImage:
            return edit_replace_image(scene, shape, command, result);
        case PresentationEditAction::TransformShape:
            return edit_transform_shape(shape, command, result);
        case PresentationEditAction::AlignShape:
            return edit_align_shape(scene, shape, command, result);
        default:
            return edit_failure(PresentationEditError::InvalidValue, "编辑操作无效。");
        }
    }

    static PresentationEditResult edit_move_group(PresentationScene& scene, PresentationSlide& slide,
        const PresentationEditCommand& command, PresentationEditResult result)
    {
        const auto group = std::find_if(slide.groups.begin(), slide.groups.end(), [&](const auto& frame)
        {
            return frame.source_id == command.group_id && frame.source_part == slide.source_part;
        });
        const double dx = command.x.value_or(0);
        const double dy = command.y.value_or(0);
        if (!scene.source_package || group == slide.groups.end() || !group->editable ||
            command.group_id.empty() || command.group_id.size() > 128)
            return edit_failure(PresentationEditError::ReadOnly, "此组合尚不能安全编辑。");
        if (!finite(dx) || !finite(dy) || std::abs(dx) > 10000 || std::abs(dy) > 10000 ||
            (dx == 0 && dy == 0))
            return edit_failure(PresentationEditError::InvalidValue, "组合移动距离无效。");
        const auto contains = [&](const PresentationShape& shape, const std::string& id)
        {
            return std::find(shape.source_groups.begin(), shape.source_groups.end(), id) !=
                shape.source_groups.end();
        };
        bool has_children = false;
        for (const auto& shape : slide.shapes)
            if (contains(shape, command.group_id))
            {
                has_children = true;
                if (!finite(shape.transform[4] + dx) || !finite(shape.transform[5] + dy) ||
                    !finite(shape.source_parent_transform[4] + dx) ||
                    !finite(shape.source_parent_transform[5] + dy))
                    return edit_failure(PresentationEditError::InvalidValue, "组合坐标超出范围。");
            }
        if (!has_children || !finite(group->transform[4] + dx) || !finite(group->transform[5] + dy))
            return edit_failure(PresentationEditError::InvalidValue, "组合缺少可移动内容。");
        for (auto& shape : slide.shapes)
            if (contains(shape, command.group_id))
            {
                shape.transform[4] += dx;
                shape.transform[5] += dy;
                shape.source_parent_transform[4] += dx;
                shape.source_parent_transform[5] += dy;
            }
        for (auto& frame : slide.groups)
        {
            const bool nested = std::any_of(slide.shapes.begin(), slide.shapes.end(), [&](const auto& shape)
            {
                return contains(shape, command.group_id) && contains(shape, frame.source_id);
            });
            if (frame.source_id == command.group_id || nested)
            {
                frame.transform[4] += dx;
                frame.transform[5] += dy;
            }
        }
        if (command.shape_index < slide.shapes.size() &&
            contains(slide.shapes[command.shape_index], command.group_id))
            result.shape_index = command.shape_index;
        return result;
    }

    static PresentationEditResult apply_scene_edit(
        PresentationScene& scene, const PresentationEditCommand& command)
    {
        if (!scene.native_editable)
        {
            return edit_failure(PresentationEditError::ReadOnly, "外部演示文稿需要先创建可编辑副本。");
        }
        if (scene.slides.empty())
        {
            return edit_failure(PresentationEditError::InvalidIndex, "演示文稿没有可编辑页面。");
        }

        PresentationEditResult result;
        result.slide_index = std::min(command.slide_index, scene.slides.size() - 1);
        // Insertion accepts the end position; other actions require an existing slide.
        if (command.action == PresentationEditAction::AddSlide)
            return edit_add_slide(scene, command, result);
        if (command.action == PresentationEditAction::ReplaceTextMatches)
            return replace_presentation_text_model(scene, command, result);
        if (command.slide_index >= scene.slides.size())
        {
            return edit_failure(PresentationEditError::InvalidIndex, "幻灯片索引无效。");
        }
        auto& slide = scene.slides[command.slide_index];
        result.slide_index = command.slide_index;
        switch (command.action)
        {
        case PresentationEditAction::DuplicateSlide:
            return edit_duplicate_slide(scene, slide, command, result);
        case PresentationEditAction::DeleteSlide:
            return edit_delete_slide(scene, command, result);
        case PresentationEditAction::MoveSlide:
            return edit_move_slide(scene, command, result);
        case PresentationEditAction::CreateSection:
        case PresentationEditAction::RenameSection:
        case PresentationEditAction::RemoveSection:
            return edit_presentation_section(scene, command, result);
        case PresentationEditAction::SetBackground:
            return edit_set_background(slide, command, result);
        case PresentationEditAction::SetSlideHidden:
            return edit_set_slide_hidden(slide, command, result);
        case PresentationEditAction::SetSlideTransition:
            return edit_set_slide_transition(slide, command, result);
        case PresentationEditAction::MoveGroup:
            return edit_move_group(scene, slide, command, result);
        case PresentationEditAction::ReorderGroup:
        {
            const auto group = std::find_if(slide.groups.begin(), slide.groups.end(), [&](const auto& frame)
            {
                return frame.source_id == command.group_id && frame.source_part == slide.source_part;
            });
            if (!scene.source_package || group == slide.groups.end() || command.group_id.empty() ||
                command.group_id.size() > 128)
                return edit_failure(PresentationEditError::ReadOnly, "当前组合不能调整图层。");
            const auto options = presentation_group_layer_options(*group);
            const auto& position = command.group_layer_position;
            const bool allowed = (position == "back" && options.back) ||
                (position == "backward" && options.backward) || (position == "forward" && options.forward) ||
                (position == "front" && options.front);
            if (!allowed)
                return edit_failure(PresentationEditError::InvalidValue, "组合图层目标无效或不可用。");
            return result;
        }
        case PresentationEditAction::ApplyTheme:
        {
            static const std::set<std::string> slots{"dk1", "lt1", "dk2", "lt2", "accent1", "accent2",
                "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink"};
            static const std::set<std::string> fonts{
                "majorLatin", "minorLatin", "majorEastAsian", "minorEastAsian"};
            if (!scene.source_package || !slide.theme_available ||
                (command.theme_colors.empty() && command.theme_fonts.empty()) ||
                command.theme_colors.size() > slots.size() ||
                std::any_of(command.theme_colors.begin(), command.theme_colors.end(),
                    [&](const auto& item)
            {
                return !slots.count(item.first) || !valid_color(item.second);
            }) ||
                command.theme_fonts.size() > fonts.size() ||
                std::any_of(command.theme_fonts.begin(), command.theme_fonts.end(), [&](const auto& item)
            {
                return !fonts.count(item.first) || item.second.empty() || item.second.size() > 128 ||
                    !valid_xml_text(item.second);
            }))
                return edit_failure(PresentationEditError::InvalidValue, "主题颜色参数无效。");
            const auto theme_state = presentation_theme_state(scene, command.slide_index);
            const bool missing_color =
                std::any_of(command.theme_colors.begin(), command.theme_colors.end(), [&](const auto& entry)
            {
                return !theme_state.editable_color_slots.count(entry.first);
            });
            const bool missing_font =
                std::any_of(command.theme_fonts.begin(), command.theme_fonts.end(), [&](const auto& entry)
            {
                return !theme_state.editable_font_slots.count(entry.first);
            });
            if (missing_color || missing_font)
                return edit_failure(PresentationEditError::ReadOnly, "当前主题缺少所选颜色或字体槽位。");
            return result;
        }
        case PresentationEditAction::InsertTable:
        {
            const double x = command.x.value_or(scene.width * 0.1);
            const double y = command.y.value_or(scene.height * 0.2);
            const double width = command.width.value_or(scene.width * 0.8);
            const double height = command.height.value_or(scene.height * 0.45);
            if (command.table_rows < 1 || command.table_rows > 30 || command.table_columns < 1 ||
                command.table_columns > 20 || command.table_rows * command.table_columns > 400 ||
                slide.shapes.size() + static_cast<std::size_t>(command.table_rows * command.table_columns) >
                    maximum_presentation_shapes ||
                !finite(x) || !finite(y) || !finite(width) || !finite(height) || x < 0 || y < 0 ||
                width < 24 || height < 24 || x + width > 10000 || y + height > 10000)
                return edit_failure(PresentationEditError::InvalidValue, "插入表格的行列或尺寸无效。");
            return result;
        }
        case PresentationEditAction::InsertTableRow:
        case PresentationEditAction::InsertTableColumn:
        case PresentationEditAction::FormatTableStyle:
        case PresentationEditAction::DeleteTableRow:
        case PresentationEditAction::DeleteTableColumn:
        case PresentationEditAction::MergeTableCell:
        case PresentationEditAction::UnmergeTableCell:
        case PresentationEditAction::FormatTableBorder:
        case PresentationEditAction::ResetTableBorder:
        {
            if (!scene.source_package || command.shape_index >= slide.shapes.size() ||
                !slide.shapes[command.shape_index].editable ||
                !slide.shapes[command.shape_index].table_cell ||
                slide.shapes[command.shape_index].source_part != slide.source_part)
                return edit_failure(PresentationEditError::ReadOnly, "请先选择当前页可编辑的表格单元格。");
            auto& cell = *slide.shapes[command.shape_index].table_cell;
            if (command.action == PresentationEditAction::MergeTableCell &&
                command.table_direction != "right" && command.table_direction != "down")
                return edit_failure(PresentationEditError::InvalidValue, "表格合并方向无效。");
            const auto& structure = cell.structure_options;
            const bool structural_action = command.action == PresentationEditAction::InsertTableRow ||
                command.action == PresentationEditAction::InsertTableColumn ||
                command.action == PresentationEditAction::DeleteTableRow ||
                command.action == PresentationEditAction::DeleteTableColumn ||
                command.action == PresentationEditAction::MergeTableCell;
            const bool structural_allowed =
                (command.action == PresentationEditAction::InsertTableRow && structure.insert_row) ||
                (command.action == PresentationEditAction::InsertTableColumn && structure.insert_column) ||
                (command.action == PresentationEditAction::DeleteTableRow && structure.delete_row) ||
                (command.action == PresentationEditAction::DeleteTableColumn && structure.delete_column) ||
                (command.action == PresentationEditAction::MergeTableCell &&
                    ((command.table_direction == "right" && structure.merge_right) ||
                        (command.table_direction == "down" && structure.merge_down)));
            if (structural_action && !structural_allowed)
                return edit_failure(
                    PresentationEditError::ReadOnly, "目标行列与已有合并区域相交，不能安全修改。");
            if (command.action == PresentationEditAction::UnmergeTableCell && !cell.unmergeable)
                return edit_failure(PresentationEditError::ReadOnly, "当前合并单元格不能安全拆分。");
            if (command.action == PresentationEditAction::FormatTableStyle && !cell.style_available)
                return edit_failure(PresentationEditError::ReadOnly, "当前表格没有可编辑的整表样式。");
            if (command.action == PresentationEditAction::FormatTableStyle && !command.table_style_options)
                return edit_failure(PresentationEditError::InvalidValue, "整表样式选项无效。");
            if (command.action == PresentationEditAction::FormatTableBorder &&
                (!command.outline_color || !valid_color(*command.outline_color) || !command.outline_width ||
                    !finite(*command.outline_width) || *command.outline_width < 0.25 ||
                    *command.outline_width > 12))
                return edit_failure(PresentationEditError::InvalidValue, "表格边框参数无效。");
            const auto edge = detail::presentation_table_edge_index(command.table_edge);
            if ((command.action == PresentationEditAction::FormatTableBorder ||
                    command.action == PresentationEditAction::ResetTableBorder) &&
                command.table_edge != "all" && !edge)
                return edit_failure(PresentationEditError::InvalidValue, "表格边框位置无效。");
            const bool reset_available = command.table_edge == "all"
                ? cell.local_border_override
                : (edge && cell.border_edges[*edge].local_override);
            if (command.action == PresentationEditAction::ResetTableBorder && !reset_available)
                return edit_failure(PresentationEditError::ReadOnly, "此单元格没有可恢复的直接边框。");
            if (command.action == PresentationEditAction::FormatTableBorder)
            {
                for (std::size_t index = 0; index < cell.border_edges.size(); ++index)
                    if (detail::presentation_table_edge_selected(command.table_edge, index))
                        cell.border_edges[index] = {*command.outline_color, 1, *command.outline_width, true};
            }
            else if (command.action == PresentationEditAction::ResetTableBorder)
            {
                for (std::size_t index = 0; index < cell.border_edges.size(); ++index)
                    if (detail::presentation_table_edge_selected(command.table_edge, index))
                        cell.border_edges[index].local_override = false;
            }
            if (command.action == PresentationEditAction::FormatTableBorder ||
                command.action == PresentationEditAction::ResetTableBorder)
            {
                cell.local_border_override =
                    std::any_of(cell.border_edges.begin(), cell.border_edges.end(), [](const auto& item)
                {
                    return item.local_override;
                });
                const auto& first = cell.border_edges.front();
                const bool uniform = !first.color.empty() && first.width > 0 &&
                    std::abs(first.opacity - 1) < 1e-6 &&
                    std::all_of(cell.border_edges.begin() + 1, cell.border_edges.end(), [&](const auto& item)
                {
                    return item.color == first.color && std::abs(item.width - first.width) < 1e-6 &&
                        std::abs(item.opacity - first.opacity) < 1e-6;
                });
                cell.border_color = uniform ? first.color : "";
                cell.border_width = uniform ? first.width : 0;
            }
            return result;
        }
        case PresentationEditAction::GroupAdjacent:
        {
            if (!scene.source_package || !command.target_index ||
                command.shape_index >= slide.shapes.size() || *command.target_index >= slide.shapes.size() ||
                command.shape_index == *command.target_index)
                return edit_failure(PresentationEditError::InvalidIndex, "请选择同一页的两个对象。");
            const auto& first = slide.shapes[command.shape_index];
            const auto& second = slide.shapes[*command.target_index];
            if (!first.editable || !second.editable || first.table_cell || second.table_cell ||
                !first.source_groups.empty() || !second.source_groups.empty() ||
                first.source_part != slide.source_part || second.source_part != slide.source_part)
                return edit_failure(PresentationEditError::ReadOnly, "只能组合当前页相邻的普通对象。");
            return result;
        }
        case PresentationEditAction::AddToGroup:
        {
            if (!scene.source_package || !command.target_index ||
                command.shape_index >= slide.shapes.size() || *command.target_index >= slide.shapes.size() ||
                command.group_id.empty() || command.group_id.size() > 128)
                return edit_failure(PresentationEditError::InvalidIndex, "组合或相邻对象无效。");
            const auto& selected = slide.shapes[command.shape_index];
            const auto& target = slide.shapes[*command.target_index];
            const auto frame = std::find_if(slide.groups.begin(), slide.groups.end(), [&](const auto& item)
            {
                return item.source_id == command.group_id && item.source_part == slide.source_part &&
                    item.editable && item.extensible;
            });
            if (frame == slide.groups.end() || selected.source_groups.empty() ||
                selected.source_groups.back() != command.group_id || !target.editable || target.table_cell ||
                !target.source_groups.empty() || target.source_part != slide.source_part)
                return edit_failure(PresentationEditError::ReadOnly, "只能向当前简单组合加入相邻普通对象。");
            return result;
        }
        case PresentationEditAction::Ungroup:
        {
            const auto frame = std::find_if(slide.groups.begin(), slide.groups.end(), [&](const auto& item)
            {
                return item.source_id == command.group_id && item.source_part == slide.source_part &&
                    item.editable && item.ungroupable;
            });
            if (!scene.source_package || command.group_id.empty() || command.group_id.size() > 128 ||
                frame == slide.groups.end())
                return edit_failure(PresentationEditError::ReadOnly, "当前组合不能安全取消。");
            return result;
        }
        case PresentationEditAction::AddText:
        case PresentationEditAction::AddShape:
        case PresentationEditAction::AddImage:
            return edit_add_object(scene, slide, command, result);
        default:
            return apply_shape_edit(scene, slide, command, result);
        }
    }

    PresentationTextLocalOverrides presentation_text_local_overrides(const PresentationShape& shape)
    {
        PresentationTextLocalOverrides result;
        for (const auto& paragraph : shape.text.paragraphs)
            for (const auto& run : paragraph.runs)
            {
                result.font_family = result.font_family || run.local_font_override;
                result.font_size = result.font_size || run.local_size_override;
                result.color = result.color || run.local_color_override;
            }
        return result;
    }

    std::vector<PresentationEditAction> presentation_edit_capabilities(const PresentationShape& shape)
    {
        using A = PresentationEditAction;
        if (!shape.editable)
            return {};
        std::vector<A> result;
        if (shape.image_path.empty() && shape.geometry != "line")
            result = {A::UpdateText, A::FormatText, A::FormatParagraph, A::FormatTextBox, A::FormatTextStyle};
        const auto local_text = presentation_text_local_overrides(shape);
        if (shape.image_path.empty() && (local_text.font_family || local_text.font_size || local_text.color))
            result.push_back(A::ResetTextInheritance);
        if (shape.image_path.empty() && !shape.table_cell && shape.source_groups.empty())
            result.push_back(A::ReplaceTextMatches);
        if (shape.table_cell)
        {
            result.push_back(A::FormatTableCell);
            result.push_back(A::FormatTableBorder);
            const auto& cell = *shape.table_cell;
            if (cell.style_available)
                result.push_back(A::FormatTableStyle);
            if (cell.local_fill_override)
                result.push_back(A::ResetTableCellFill);
            if (cell.local_border_override)
                result.push_back(A::ResetTableBorder);
            if (cell.unmergeable)
                result.push_back(A::UnmergeTableCell);
            if (cell.structure_options.insert_row)
                result.push_back(A::InsertTableRow);
            if (cell.structure_options.insert_column)
                result.push_back(A::InsertTableColumn);
            if (cell.structure_options.delete_row)
                result.push_back(A::DeleteTableRow);
            if (cell.structure_options.delete_column)
                result.push_back(A::DeleteTableColumn);
            if (cell.structure_options.merge_right || cell.structure_options.merge_down)
                result.push_back(A::MergeTableCell);
            return result;
        }
        result.insert(result.end(),
            {A::TransformShape, A::AlignShape, A::FormatShape, A::DuplicateShape, A::DeleteShape});
        if (shape.placeholder && shape.placeholder->local_fill_override &&
            shape.placeholder->inherited_fill_source != "none")
            result.push_back(A::ResetPlaceholderFill);
        if (shape.placeholder && shape.placeholder->local_outline_override &&
            shape.placeholder->inherited_outline_source != "none")
            result.push_back(A::ResetPlaceholderOutline);
        result.push_back(A::SetClickAction);
        if (presentation_format_brush_supported(shape))
            result.push_back(A::ApplyFormat);
        if (shape.source_groups.empty())
        {
            result.push_back(A::MoveShape);
            result.push_back(A::GroupAdjacent);
        }
        if (!shape.image_path.empty())
            result.insert(result.end(), {A::FormatImage, A::ReplaceImage});
        return result;
    }

    PresentationEditResult apply_presentation_edit(
        PresentationScene& scene, const PresentationEditCommand& command)
    {
        if (!scene.source_package)
        {
            if ((command.action == PresentationEditAction::InsertTable ||
                    command.action == PresentationEditAction::FormatTableStyle ||
                    command.action == PresentationEditAction::GroupAdjacent ||
                    command.action == PresentationEditAction::ApplyTheme ||
                    command.action == PresentationEditAction::ResetTextInheritance ||
                    command.action == PresentationEditAction::CreateSection ||
                    command.action == PresentationEditAction::SetClickAction ||
                    command.action == PresentationEditAction::ApplyFormat ||
                    command.action == PresentationEditAction::ReplaceTextMatches) &&
                scene.native_editable)
            {
                auto package = serialize_presentation(scene);
                if (package.error != PresentationError::None)
                    return edit_failure(PresentationEditError::InvalidValue, package.message);
                auto imported = parse_presentation(std::move(package.parts));
                if (imported.error != PresentationError::None)
                    return edit_failure(PresentationEditError::InvalidValue, imported.message);
                imported.scene.native_editable = true;
                auto adjusted = command;
                if (adjusted.action == PresentationEditAction::ReplaceTextMatches && !adjusted.find_all &&
                    adjusted.slide_index < imported.scene.slides.size() &&
                    adjusted.shape_index < imported.scene.slides[adjusted.slide_index].shapes.size())
                    adjusted.find_shape_id =
                        imported.scene.slides[adjusted.slide_index].shapes[adjusted.shape_index].id;
                auto result = apply_presentation_edit(imported.scene, adjusted);
                if (result.error == PresentationEditError::None)
                    scene = std::move(imported.scene);
                return result;
            }
            return apply_scene_edit(scene, command);
        }
        auto candidate = scene;
        auto result = apply_scene_edit(candidate, command);
        if (result.error != PresentationEditError::None)
            return result;
        result = preserve_presentation_edit(scene, candidate, command, result);
        if (result.error == PresentationEditError::None)
            scene = std::move(candidate);
        return result;
    }

    PresentationEditResult apply_presentation_model_edit(
        PresentationScene& scene, const PresentationEditCommand& command)
    {
        return apply_scene_edit(scene, command);
    }

}
