#include <mirrorfly/presentation.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace mirrorfly;
    using Action = PresentationEditAction;
    using Error = PresentationEditError;
    constexpr std::array actions{Action::AddSlide, Action::DuplicateSlide, Action::DeleteSlide,
        Action::MoveSlide, Action::AddText, Action::AddShape, Action::AddImage, Action::UpdateText,
        Action::FormatText, Action::FormatTextStyle, Action::FormatParagraph, Action::FormatTextBox,
        Action::FormatShape, Action::FormatImage, Action::ReplaceImage, Action::TransformShape,
        Action::DeleteShape, Action::DuplicateShape, Action::MoveShape, Action::SetBackground,
        Action::AlignShape, Action::SetSlideHidden};
    int failures = 0;
    std::uint64_t digest = 14695981039346656037ULL;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << message << '\n';
        }
    }

    void record(const std::string& value)
    {
        // Deterministic comparison of the same cases before/after refactoring, not a security hash.
        for (const unsigned char byte : value)
            digest = (digest ^ byte) * 1099511628211ULL;
        digest = (digest ^ 255) * 1099511628211ULL;
    }

    std::string state(const PresentationScene& scene)
    {
        const auto package = serialize_presentation(scene);
        std::string result = std::to_string(static_cast<int>(package.error)) + ':' + package.message + ':' +
            std::to_string(scene.next_shape_id) + ':' + std::to_string(scene.native_editable);
        for (const auto& part : package.parts)
            result += std::to_string(part.path.size()) + ':' + part.path + std::to_string(part.bytes.size()) +
                ':' + part.bytes;
        for (const auto& slide : scene.slides)
            for (const auto& shape : slide.shapes)
                result += ':' + std::to_string(shape.id) + ':' + std::to_string(shape.editable);
        return result;
    }

    PresentationEditCommand command_for(Action action)
    {
        PresentationEditCommand command;
        command.action = action;
        command.layout = PresentationSlideLayout::TitleContent;
        command.offset = 1;
        command.text = "Edited text";
        command.geometry = "rect";
        command.image_mime_type = "image/png";
        command.image_bytes = "dispatch-image-fixture";
        command.font_family = "Arial";
        command.font_size = 24;
        command.bold = true;
        command.text_color = "#135790";
        command.alignment = "center";
        command.text_style.fill = PresentationFill{"#345678"};
        command.numbered = true;
        command.paragraph_margin_left = 12;
        command.first_line_indent = -6;
        command.line_spacing = 1.5;
        command.inset_left = 4;
        command.vertical_alignment = "center";
        command.fill_color = "#123456";
        command.outline_width = 2;
        command.shadow_enabled = true;
        command.image_crop_left = 0.1;
        command.image_opacity = 0.7;
        command.x = 90;
        command.y = 100;
        command.width = 160;
        command.height = 80;
        command.rotation = 15;
        command.background_color = "#ABCDEF";
        command.hidden = true;
        if (action == Action::FormatImage || action == Action::ReplaceImage)
            command.shape_index = 2;
        return command;
    }

    PresentationScene fixture(bool imported)
    {
        auto scene = make_presentation(PresentationSlideLayout::TitleContent);
        check(apply_presentation_edit(scene, command_for(Action::AddImage)).error == Error::None,
            "fixture image");
        auto add = command_for(Action::AddSlide);
        add.slide_index = 1;
        check(apply_presentation_edit(scene, add).error == Error::None, "fixture second slide");
        if (imported)
        {
            const auto parsed = parse_presentation(serialize_presentation(scene).parts);
            check(parsed.error == PresentationError::None && parsed.scene.source_package,
                "fixture imported package");
            scene = parsed.scene;
            scene.native_editable = true;
        }
        return scene;
    }

    void rejected(PresentationScene scene, const PresentationEditCommand& command, Error expected,
        const std::string& message)
    {
        const auto before = state(scene);
        const auto source = scene.source_package;
        const auto result = apply_presentation_edit(scene, command);
        check(result.error == expected && result.message == message && result.slide_index == 0 &&
                !result.shape_index,
            "rejection contract for action " + std::to_string(static_cast<int>(command.action)));
        check(state(scene) == before && scene.source_package == source, "rejection keeps original state");
        record(result.message);
        record(before);
    }

    bool needs_shape(Action action)
    {
        switch (action)
        {
        case Action::AddSlide:
        case Action::DuplicateSlide:
        case Action::DeleteSlide:
        case Action::MoveSlide:
        case Action::SetBackground:
        case Action::SetSlideHidden:
        case Action::AddText:
        case Action::AddShape:
        case Action::AddImage:
            return false;
        default:
            return true;
        }
    }

    void successful_actions(const PresentationScene& base)
    {
        for (const auto action : actions)
        {
            auto scene = base;
            auto command = command_for(action);
            if (!needs_shape(action))
                command.shape_index = 999;
            if (action == Action::AddSlide)
                command.slide_index = scene.slides.size();
            const auto result = apply_presentation_edit(scene, command);
            check(result.error == Error::None && result.message.empty(),
                "successful dispatch for action " + std::to_string(static_cast<int>(action)));
            const auto slide =
                action == Action::DuplicateSlide || action == Action::MoveSlide ? 1U : command.slide_index;
            check(result.slide_index == slide, "returned slide index");
            auto shape = needs_shape(action) ? std::optional<std::size_t>(command.shape_index) : std::nullopt;
            if (action == Action::DeleteShape)
                shape.reset();
            if (action == Action::DuplicateShape || action == Action::MoveShape)
                shape = 1;
            if (action == Action::AddText || action == Action::AddShape || action == Action::AddImage)
                shape = 3;
            check(result.shape_index == shape, "returned shape index");
            check(serialize_presentation(scene).error == PresentationError::None, "result remains saveable");
            record(std::to_string(static_cast<int>(action)));
            record(state(scene));
        }
    }

    void guard_order(const PresentationScene& base)
    {
        for (const auto action : actions)
        {
            auto command = command_for(action);
            auto locked = base;
            locked.native_editable = false;
            locked.slides.clear();
            rejected(locked, command, Error::ReadOnly, "外部演示文稿需要先创建可编辑副本。");
            locked.native_editable = true;
            rejected(locked, command, Error::InvalidIndex, "演示文稿没有可编辑页面。");
            command.slide_index = 999;
            rejected(base, command, action == Action::AddSlide ? Error::TooLarge : Error::InvalidIndex,
                action == Action::AddSlide ? "页面位置无效或页面、对象数量已达上限。" : "幻灯片索引无效。");
            if (needs_shape(action))
            {
                command.slide_index = 0;
                command.shape_index = 999;
                rejected(base, command, Error::InvalidIndex, "对象索引无效。");
                command = command_for(action);
                locked = base;
                locked.slides[0].shapes[command.shape_index].editable = false;
                rejected(locked, command, Error::ReadOnly, "此对象不支持该操作；原始结构保持不变。");
            }
        }
        auto unknown = command_for(static_cast<Action>(999));
        rejected(base, unknown, Error::ReadOnly, "此对象不支持该操作；原始结构保持不变。");
        unknown.shape_index = 999;
        rejected(base, unknown, Error::InvalidIndex, "对象索引无效。");
    }

    void invalid_parameters(const PresentationScene& base)
    {
        auto command = command_for(Action::AddSlide);
        command.layout = static_cast<PresentationSlideLayout>(999);
        command.slide_index = 999;
        rejected(base, command, Error::InvalidValue, "页面版式无效。");
        command = command_for(Action::AddText);
        command.width = -1;
        command.text = std::string(1, '\x01');
        rejected(base, command, Error::InvalidValue, "对象位置或尺寸无效。");
        command = command_for(Action::FormatText);
        command.font_size = std::numeric_limits<double>::quiet_NaN();
        rejected(base, command, Error::InvalidValue, "文字格式无效。");
        command = command_for(Action::FormatShape);
        command.gradient_start_color = "#112233";
        rejected(base, command, Error::InvalidValue, "形状样式无效。");
        command = command_for(Action::FormatImage);
        command.image_crop_right = 0.95;
        rejected(base, command, Error::InvalidValue, "图片裁剪或透明度无效。");
        command = command_for(Action::TransformShape);
        command.preserve_aspect = true;
        command.x = std::numeric_limits<double>::quiet_NaN();
        rejected(base, command, Error::InvalidValue, "锁定宽高比时请只修改宽度或高度。");
    }
    int run_dispatch_tests()
    {
        for (const bool imported : {false, true})
        {
            const auto base = fixture(imported);
            successful_actions(base);
            guard_order(base);
            invalid_parameters(base);
        }
        // Authored documents now include the linked table style definition in every serialized package.
        check(digest == 0xa74f06baeba2104aULL, "dispatch behavior matches authored table style");
        std::cout << "Dispatch behavior digest: " << std::hex << digest << '\n';
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_dispatch_tests();
}
