#include "presentation_templates.hpp"

#include <utility>

namespace
{
    enum class Role
    {
        Ink,
        Paper,
        Card,
        Muted,
        Accent,
        Soft
    };

    class TemplatePage
    {
    public:
        TemplatePage(std::uint64_t& next_id, const mirrorfly::PresentationTemplatePalette& palette)
            : next_id_(next_id), palette_(palette)
        {
            slide.background.color = palette.paper;
            slide.background.theme_slot = "lt1";
            slide.background.theme_reference_color = palette.paper;
        }

        void block(
            double x, double y, double width, double height, Role role, const std::string& geometry = "rect")
        {
            mirrorfly::PresentationShape shape;
            shape.id = next_id_++;
            shape.name = "色块";
            shape.transform = {1, 0, 0, 1, x, y};
            shape.width = width;
            shape.height = height;
            shape.geometry = geometry;
            shape.fill.color = color(role);
            shape.fill.theme_slot = slot(role);
            shape.fill.theme_reference_color = shape.fill.color;
            shape.outline_color.clear();
            slide.shapes.push_back(std::move(shape));
        }

        void text(double x, double y, double width, double height, double size, const std::string& value,
            Role role, bool bold = false)
        {
            mirrorfly::PresentationShape shape;
            shape.id = next_id_++;
            shape.name = value;
            shape.transform = {1, 0, 0, 1, x, y};
            shape.width = width;
            shape.height = height;
            shape.geometry = "rect";
            shape.outline_color.clear();
            shape.text.inset_left = 0;
            shape.text.inset_right = 0;
            shape.text.inset_top = 0;
            shape.text.inset_bottom = 0;
            mirrorfly::PresentationRun run;
            run.font_family = "Arial";
            run.east_asian_font_family = "Microsoft YaHei";
            run.font_theme_slot = "majorLatin";
            run.east_asian_theme_slot = "majorEastAsian";
            run.font_theme_reference = run.font_family;
            run.east_asian_theme_reference = run.east_asian_font_family;
            run.font_size = size;
            run.bold = bold;
            run.color = color(role);
            run.color_theme_slot = slot(role);
            run.color_theme_reference = run.color;
            std::size_t start = 0;
            do
            {
                const auto end = value.find('\n', start);
                run.text = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
                mirrorfly::PresentationParagraph paragraph;
                paragraph.runs.push_back(run);
                paragraph.space_after = 6;
                shape.text.paragraphs.push_back(std::move(paragraph));
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            } while (true);
            slide.shapes.push_back(std::move(shape));
        }

        void heading(const std::string& category, const std::string& title, const std::string& subtitle)
        {
            slide.title = title;
            block(44, 36, 5, 74, Role::Accent);
            text(64, 32, 720, 24, 12, category, Role::Accent, true);
            text(62, 58, 850, 48, 30, title, Role::Ink, true);
            text(64, 111, 830, 32, 14, subtitle, Role::Muted);
            block(44, 494, 872, 1, Role::Soft);
            text(44, 507, 710, 22, 11, "汇报人：____    /    日期：____", Role::Muted);
            text(834, 506, 90, 22, 11, "MIRRORFLY", Role::Muted);
        }

        mirrorfly::PresentationSlide slide;

    private:
        const std::string& color(Role role) const
        {
            switch (role)
            {
            case Role::Ink:
                return palette_.ink;
            case Role::Paper:
                return palette_.paper;
            case Role::Card:
                return palette_.card;
            case Role::Muted:
                return palette_.muted;
            case Role::Accent:
                return palette_.accent;
            case Role::Soft:
                return palette_.soft;
            }
            return palette_.ink;
        }

        static const char* slot(Role role)
        {
            switch (role)
            {
            case Role::Ink:
                return "dk2";
            case Role::Paper:
                return "lt1";
            case Role::Card:
                return "lt2";
            case Role::Muted:
                return "accent2";
            case Role::Accent:
                return "accent1";
            case Role::Soft:
                return "accent3";
            }
            return "dk2";
        }

        std::uint64_t& next_id_;
        const mirrorfly::PresentationTemplatePalette& palette_;
    };
}

namespace mirrorfly
{
    PresentationSlide refined_presentation_template(std::uint64_t& next_shape_id,
        PresentationSlideLayout layout, const PresentationTemplatePalette& palette)
    {
        TemplatePage page(next_shape_id, palette);
        if (layout == PresentationSlideLayout::ResearchStudio)
        {
            page.heading("ACADEMIC / 01", "从问题到研究路径", "开题 · 课程展示 · 研究阶段汇报");
            page.block(44, 166, 276, 306, Role::Ink, "roundRect");
            page.text(66, 188, 228, 28, 12, "THE QUESTION / 核心问题", Role::Card, true);
            page.text(66, 235, 230, 112, 26, "用一句话说清\n你想回答的问题", Role::Paper, true);
            page.text(66, 383, 225, 65, 14, "研究对象：____\n价值与边界：____", Role::Paper);
            const std::string titles[]{"提出假设", "建立方法", "验证结果"};
            const std::string details[]{
                "依据与关键变量：____", "材料、样本与步骤：____", "评价标准与预期：____"};
            for (int index = 0; index < 3; ++index)
            {
                const double y = 166 + index * 106;
                page.block(340, y, 576, 94, Role::Card, "roundRect");
                page.block(358, y + 23, 42, 42, Role::Soft, "ellipse");
                page.text(369, y + 30, 26, 30, 18, std::to_string(index + 1), Role::Accent, true);
                page.text(420, y + 15, 465, 32, 21, titles[index], Role::Ink, true);
                page.text(420, y + 55, 465, 28, 14, details[index], Role::Muted);
            }
        }
        else if (layout == PresentationSlideLayout::EvidenceBoard)
        {
            page.heading("ACADEMIC / 02", "让证据支撑结论", "实验结果 · 文献汇报 · 方案比较");
            page.block(44, 166, 872, 80, Role::Accent, "roundRect");
            page.text(66, 178, 150, 56, 13, "KEY FINDING\n核心发现", Role::Card, true);
            page.text(248, 183, 632, 44, 24, "在这里写下最重要的一项结论", Role::Card, true);
            const std::string titles[]{"证据 A / 观察与数据", "证据 B / 对照与解释"};
            for (int index = 0; index < 2; ++index)
            {
                const double x = 44 + index * 446;
                page.block(x, 264, 426, 150, Role::Card, "roundRect");
                page.block(x + 20, 285, 4, 104, Role::Accent);
                page.text(x + 40, 281, 356, 32, 19, titles[index], Role::Ink, true);
                page.text(x + 40, 328, 350, 72, 15, "指标与观察：____\n证据如何支持结论：____", Role::Muted);
            }
            page.text(
                52, 432, 850, 45, 13, "适用边界：____    /    资料来源：作者、年份、页码或链接", Role::Muted);
        }
        else if (layout == PresentationSlideLayout::ProjectDashboard)
        {
            page.heading("WORK / 01", "项目进展，一页看清", "本期结论：____    /    汇报周期：____");
            const std::string labels[]{"总体完成度", "本期交付", "需要关注"};
            const std::string values[]{"— %", "— 项", "— 项"};
            for (int index = 0; index < 3; ++index)
            {
                const double x = 44 + index * 298;
                page.block(x, 164, 276, 124, Role::Card, "roundRect");
                page.block(x, 164, 276, 4, Role::Accent);
                page.text(x + 20, 184, 232, 26, 13, labels[index], Role::Muted);
                page.text(x + 20, 217, 232, 54, 34, values[index], Role::Ink, true);
            }
            page.block(44, 310, 574, 162, Role::Card, "roundRect");
            page.text(66, 328, 530, 30, 20, "已完成 / 下一步", Role::Ink, true);
            page.text(66, 376, 530, 75, 16, "交付结果：____\n下一项行动 · 负责人 · 日期：____", Role::Muted);
            page.block(640, 310, 276, 162, Role::Soft, "roundRect");
            page.text(660, 328, 236, 30, 20, "风险与支持", Role::Accent, true);
            page.text(660, 376, 236, 75, 15, "风险及影响：____\n需要的支持：____", Role::Ink);
        }
        else if (layout == PresentationSlideLayout::DeliveryRoadmap)
        {
            page.heading("WORK / 02", "对齐目标，分阶段交付", "目标结果：____    /    项目负责人：____");
            page.block(84, 195, 768, 3, Role::Accent);
            const std::string stages[]{"定义与对齐", "实施与验证", "交付与复盘"};
            for (int index = 0; index < 3; ++index)
            {
                const double x = 44 + index * 298;
                page.block(x + 20, 176, 42, 42, Role::Accent, "ellipse");
                page.text(x + 33, 183, 28, 30, 18, std::to_string(index + 1), Role::Card, true);
                page.block(x, 240, 276, 160, Role::Card, "roundRect");
                page.text(x + 20, 258, 236, 32, 22, stages[index], Role::Ink, true);
                page.text(
                    x + 20, 309, 236, 76, 15, "交付物：____\n负责人：____\n截止日期：____", Role::Muted);
            }
            page.block(44, 420, 872, 52, Role::Soft, "roundRect");
            page.text(64, 434, 832, 28, 14, "阶段验收标准：____    /    依赖与待确认事项：____", Role::Ink);
        }
        return std::move(page.slide);
    }
}
