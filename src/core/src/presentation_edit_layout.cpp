#include "presentation_edit_layout.hpp"
#include "presentation_templates.hpp"

#include <algorithm>
#include <utility>

namespace
{
    std::uint64_t next_id(std::uint64_t& next_shape_id)
    {
        if (next_shape_id == 0)
        {
            next_shape_id = 1;
        }
        return next_shape_id++;
    }

}

namespace mirrorfly::presentation_edit_layout
{
    mirrorfly::PresentationShape text_box(std::uint64_t& next_shape_id, const std::string& name, double x,
        double y, double width, double height, double font_size, bool bold, const std::string& text)
    {
        mirrorfly::PresentationShape shape;
        shape.id = next_id(next_shape_id);
        shape.name = name;
        shape.transform = {1, 0, 0, 1, x, y};
        shape.width = width;
        shape.height = height;
        shape.geometry = "rect";
        shape.outline_color.clear();
        mirrorfly::PresentationParagraph paragraph;
        mirrorfly::PresentationRun run;
        run.text = text;
        run.font_family = "Arial";
        run.east_asian_font_family = "Microsoft YaHei";
        run.font_theme_slot = "majorLatin";
        run.east_asian_theme_slot = "majorEastAsian";
        run.color_theme_slot = "dk2";
        run.font_theme_reference = run.font_family;
        run.east_asian_theme_reference = run.east_asian_font_family;
        run.color_theme_reference = run.color;
        run.font_size = font_size;
        run.bold = bold;
        paragraph.runs.push_back(std::move(run));
        shape.text.paragraphs.push_back(std::move(paragraph));
        return shape;
    }

    void replace_text(mirrorfly::PresentationShape& shape, const std::string& text);

    mirrorfly::PresentationSlide slide_for_layout(mirrorfly::PresentationScene& scene,
        mirrorfly::PresentationSlideLayout layout, const mirrorfly::PresentationTemplatePalette& palette)
    {
        mirrorfly::PresentationSlide slide;
        slide.title = "幻灯片";
        slide.background.color = palette.paper;
        slide.background.theme_slot = "lt1";
        slide.background.theme_reference_color = palette.paper;
        if (layout == mirrorfly::PresentationSlideLayout::Title ||
            layout == mirrorfly::PresentationSlideLayout::TitleContent ||
            layout == mirrorfly::PresentationSlideLayout::TwoColumns)
        {
            slide.shapes.push_back(
                text_box(scene.next_shape_id, "标题", 60, 42, 840, 72, 28, true, "演示标题"));
        }
        if (layout == mirrorfly::PresentationSlideLayout::Title)
        {
            slide.shapes.push_back(
                text_box(scene.next_shape_id, "副标题", 144, 210, 672, 96, 18, false, "汇报人 · 日期"));
        }
        else if (layout == mirrorfly::PresentationSlideLayout::TitleContent)
        {
            slide.shapes.push_back(
                text_box(scene.next_shape_id, "正文", 72, 132, 816, 336, 20, false, "在此输入要点"));
        }
        else if (layout == mirrorfly::PresentationSlideLayout::TwoColumns)
        {
            slide.shapes.push_back(
                text_box(scene.next_shape_id, "左栏", 60, 132, 396, 336, 18, false, "左栏内容"));
            slide.shapes.push_back(
                text_box(scene.next_shape_id, "右栏", 504, 132, 396, 336, 18, false, "右栏内容"));
        }
        const auto append = [&scene, &slide](const std::string& name, double x, double y, double width,
                                double height, double size, bool bold, const std::string& text)
        {
            auto shape = text_box(scene.next_shape_id, name, x, y, width, height, size, bold);
            replace_text(shape, text);
            slide.shapes.push_back(std::move(shape));
        };
        using Layout = mirrorfly::PresentationSlideLayout;
        switch (layout)
        {
        case Layout::ReportOutline:
            slide.title = "汇报提纲";
            append("提纲", 84, 148, 792, 300, 24, false,
                "01 研究背景与问题\n\n02 方法与过程\n\n03 结果与讨论\n\n04 总结与展望");
            break;
        case Layout::ResearchPlan:
            slide.title = "研究思路";
            append("问题", 60, 150, 264, 290, 20, false, "01 提出问题\n\n研究什么？\n为什么值得研究？");
            append("方法", 348, 150, 264, 290, 20, false, "02 研究方法\n\n使用什么资料？\n如何验证？");
            append("结果", 636, 150, 264, 290, 20, false, "03 预期结果\n\n得到什么发现？\n有哪些局限？");
            break;
        case Layout::Comparison:
            slide.title = "对比分析";
            append("方案 A", 60, 146, 396, 322, 22, false, "方案 A\n\n优势：\n\n限制：\n\n适用场景：");
            append("方案 B", 504, 146, 396, 322, 22, false, "方案 B\n\n优势：\n\n限制：\n\n适用场景：");
            break;
        case Layout::References:
            slide.title = "参考资料";
            append("资料列表", 72, 146, 816, 276, 20, false,
                "[1] 作者．资料题名．年份．\n\n[2] 作者．论文或书籍名称．期刊或出版社．\n\n"
                "[3] 来源名称．链接．访问日期．");
            append("引用提醒", 72, 450, 816, 40, 14, false, "请按课程要求核对引用格式和来源。");
            break;
        case Layout::Conclusion:
            slide.title = "总结与交流";
            append("结论", 72, 152, 816, 114, 24, false, "核心结论\n在此填写最重要的发现");
            append("计划", 72, 302, 816, 100, 22, false, "下一步\n在此填写后续计划");
            append("交流", 72, 448, 816, 44, 18, false, "谢谢聆听 · 欢迎提问");
            break;
        case Layout::ProjectStatus:
            slide.title = "项目进展";
            append("总体进度", 60, 148, 252, 300, 22, false, "总体进度\n\n完成度：____%\n当前阶段：____");
            append("本期完成", 336, 148, 264, 300, 20, false, "本期完成\n\n• 已完成事项\n\n• 交付结果");
            append("风险与计划", 624, 148, 276, 300, 20, false, "风险与计划\n\n风险：____\n\n下一步：____");
            break;
        case Layout::MeetingSummary:
            slide.title = "会议纪要";
            append("会议结论", 60, 148, 408, 144, 21, false, "会议结论\n\n在此填写已确认的决定");
            append("行动事项", 492, 148, 408, 144, 21, false, "行动事项\n\n负责人 · 截止时间 · 事项");
            append("待跟进", 60, 324, 840, 124, 20, false, "待跟进\n\n未决问题与下次会议安排");
            break;
        case Layout::Milestones:
            slide.title = "里程碑";
            append("里程碑一", 60, 160, 264, 260, 21, false, "01 里程碑\n\n目标：____\n日期：____");
            append("里程碑二", 348, 160, 264, 260, 21, false, "02 里程碑\n\n目标：____\n日期：____");
            append("里程碑三", 636, 160, 264, 260, 21, false, "03 里程碑\n\n目标：____\n日期：____");
            break;
        default:
            break;
        }
        if (layout >= Layout::ReportOutline && layout <= Layout::Milestones)
        {
            auto title = text_box(scene.next_shape_id, "标题", 60, 42, 840, 72, 28, true, slide.title);
            slide.shapes.insert(slide.shapes.begin(), std::move(title));
        }
        if (layout >= Layout::ResearchStudio && layout <= Layout::DeliveryRoadmap)
        {
            slide = mirrorfly::refined_presentation_template(scene.next_shape_id, layout, palette);
        }
        slide.authored_theme = !scene.source_package;
        slide.theme_available = true;
        if (layout >= Layout::ResearchStudio && layout <= Layout::DeliveryRoadmap)
            slide.authored_palette = {
                palette.ink, palette.paper, palette.card, palette.muted, palette.accent, palette.soft};
        else
            slide.authored_palette = {"#292724", "#FFFFFF", "#F4F0E9", "#6F7D69", "#B77746", "#9A8978"};
        const double scale_x = scene.width / 960;
        const double scale_y = scene.height / 540;
        for (auto& shape : slide.shapes)
        {
            shape.transform[4] *= scale_x;
            shape.transform[5] *= scale_y;
            shape.width *= scale_x;
            shape.height *= scale_y;
            for (auto& paragraph : shape.text.paragraphs)
            {
                for (auto& run : paragraph.runs)
                {
                    if (run.color_theme_slot == "dk2")
                    {
                        run.color = palette.ink;
                        run.color_theme_reference = run.color;
                    }
                    run.font_size *= std::min(scale_x, scale_y);
                }
            }
        }
        if (scene.source_package)
        {
            slide.background.theme_slot.clear();
            for (auto& shape : slide.shapes)
            {
                shape.fill.theme_slot.clear();
                for (auto& paragraph : shape.text.paragraphs)
                    for (auto& run : paragraph.runs)
                    {
                        run.color_theme_slot.clear();
                        run.font_theme_slot.clear();
                        run.east_asian_theme_slot.clear();
                    }
            }
        }
        return slide;
    }

    void replace_text(mirrorfly::PresentationShape& shape, const std::string& text)
    {
        mirrorfly::PresentationRun format;
        mirrorfly::PresentationParagraph paragraph_format;
        if (!shape.text.paragraphs.empty())
        {
            paragraph_format = shape.text.paragraphs.front();
            if (!shape.text.paragraphs.front().runs.empty())
            {
                format = shape.text.paragraphs.front().runs.front();
            }
        }
        format.click_action = {};
        if (shape.click_action.from_text)
            shape.click_action = {};
        paragraph_format.runs.clear();
        shape.text.paragraphs.clear();
        std::size_t start = 0;
        do
        {
            const auto end = text.find('\n', start);
            mirrorfly::PresentationParagraph paragraph = paragraph_format;
            mirrorfly::PresentationRun run = format;
            run.text = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            paragraph.runs.push_back(std::move(run));
            shape.text.paragraphs.push_back(std::move(paragraph));
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        } while (true);
    }
}
