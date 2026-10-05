#include "office_ai_layout_catalog.hpp"

#include <QJsonArray>

namespace mirrorfly
{
    QJsonObject office_ai_layout_catalog(const QString& name)
    {
        static const QJsonObject layouts{
            {"cover", "封面：0..1块；subtitle为导语；可用imagePath，或coverWord作短主题词。"},
            {"grid", "开放式论点：1..4块，每块约30..70字。"},
            {"columns", "并列主题：1..4块，三栏时每块约20..32字。"},
            {"steps", "详细步骤：1..4块；每块一个动作，约30..60字。"},
            {"flow", "因果或顺序链：2..4块；短标题，正文约20..35字。"},
            {"comparison", "对立对象：恰好2块；同类并列内容请用grid。"},
            {"statement", "核心结论：0..1块；无块时需要subtitle。"},
            {"hub", "中心概念：2..4块；subtitle为2..6字名词，四块时每块约25字。"},
            {"code", "代码：code为最多600字完整源码，0..2块解释；片段须明确标注。"},
            {"factcheck", "误区澄清：2..3块，heading是误区，text为约18..28字事实。"},
            {"visual", "图文：imagePath必填，1..2块，每块约20..30字，不放example。"}};
        if (name.isEmpty())
            return {{"ok", true}, {"layouts", layouts},
                {"hint", "无需指定布局时省略layout；本地自动选择，仍检查溢出并保留全部内容。"}};
        if (!layouts.contains(name))
            return {{"ok", false}, {"error", "unknown_layout"},
                {"names", QJsonArray::fromStringList(layouts.keys())}};
        return {{"ok", true}, {"layout", name}, {"constraints", layouts.value(name)}};
    }
}
