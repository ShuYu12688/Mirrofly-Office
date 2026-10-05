#include "office_ai_tools.hpp"

namespace mirrorfly
{
    QString office_ai_compose_guide(const QString& module)
    {
        static const QJsonObject guides{
            {"word",
                "新建空白Word用office_compose_word一次提交title、可选subtitle和1～8个sections；"
                "每章heading、paragraphs数组、可选bullets数组。正文具体，不堆成一段；本地预检"
                "位置和样式。完成后office_save，不重复提交。"},
            {"sheets",
                "新建空白表格用office_compose_table一次提交title、columns和等宽rows；title是"
                "工作表标签，columns在第1行，rows从第2行开始，公式按此引用。数字或公式要有依据，"
                "末行为汇总才设totalRow。配方在编辑前检查全部行和公式，本地粘贴并排版；完成后"
                "office_save，不重复提交。"},
            {"slides",
                "整套PPT用office_compose_slides；首次targetPages为总页数，每批1..8页，下一批换"
                "batchId；verifiedPages达到目标就保存。按主题用office_style选风格，不必输出长篇"
                "设计方案。layout可省略，由本地按内容与styleId排版：杂志主次栏、研究证据行、科技"
                "重点侧栏、自然错落节点；连续同信息量页切换结构。只保留用户事实，不虚构指标。"
                "显式布局查office_layout。需要图片才"
                "加载media，再搜索、fetch并使用imagePath。失败指出页码/字段，保留已有页；成功"
                "回执已核验，不必反复查询工作区或重读刚生成页，直接接下一批或保存。"}};
        return guides.value(module).toString();
    }
}
