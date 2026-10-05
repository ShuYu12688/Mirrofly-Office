#include "office_ai_style.hpp"

#include <QJsonArray>

namespace
{
    QJsonArray styles()
    {
        return {
            QJsonObject{{"id", "editorial"}, {"name", "杂志叙事"}, {"useFor", "文化、人物、旅行、品牌故事"},
                {"direction",
                    "参考杂志编辑网格：大编号与不对称主次栏，连续页交替主栏；优先 grid、visual、statement。"},
                {"ink", "#24211F"}, {"paper", "#F7F2EA"}, {"accent", "#B65339"}},
            QJsonObject{{"id", "modern"}, {"name", "现代科技"}, {"useFor", "技术、产品、软件、未来趋势"},
                {"direction",
                    "参考技术界面：重点深色侧栏与明亮说明区、节点与连接线；按内容用 flow、hub、code。"},
                {"ink", "#10233F"}, {"paper", "#EAF1FA"}, {"accent", "#246BFD"}},
            QJsonObject{{"id", "natural"}, {"name", "自然温润"}, {"useFor", "生活、健康、教育、可持续主题"},
                {"direction",
                    "参考自然观察手册：柔和圆形节点与错落文字，连续页交替节奏；优先 visual、grid、steps。"},
                {"ink", "#263B31"}, {"paper", "#F4F3E9"}, {"accent", "#708B61"}},
            QJsonObject{{"id", "research"}, {"name", "研究报告"},
                {"useFor", "商业分析、研究、政策、严肃汇报"},
                {"direction",
                    "参考学术海报与数据报告：编号、标签列和证据列对齐，细线分行；按证据用 "
                    "comparison、steps、factcheck。"},
                {"ink", "#193049"}, {"paper", "#F8FAFC"}, {"accent", "#C1763D"}}};
    }
}

namespace mirrorfly
{
    QJsonObject office_ai_style_catalog(const QString& id)
    {
        const auto entries = styles();
        if (id.isEmpty())
            return {{"ok", true}, {"styles", entries},
                {"hint", "按主题与受众选择一个 styleId；内容真实性和可读性优先。"}};
        for (const auto& value : entries)
        {
            const auto style = value.toObject();
            if (style.value("id") == id)
                return {{"ok", true}, {"style", style}};
        }
        return {{"ok", false}, {"error", "unknown_style"}, {"styles", entries}};
    }

    QJsonObject office_ai_style_theme(const QString& id)
    {
        const auto result = office_ai_style_catalog(id);
        if (!result.value("ok").toBool())
            return {};
        const auto style = result.value("style").toObject();
        return {{"ink", style.value("ink")}, {"paper", style.value("paper")},
            {"accent", style.value("accent")}, {"styleId", id}};
    }
}
