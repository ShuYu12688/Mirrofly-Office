pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RoundedDialog
{
    id: root
    property string module: "text"
    signal destinationRequested(var options)
    title: module === "pdf" ? "PDF 副本与压缩" : "导出 PDF"
    modal: true
    anchors.centerIn: parent
    width: Math.min(540, parent ? parent.width - 40 : 540)
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: { scope.currentIndex = 0; compression.currentIndex = 0; layout.currentIndex = 0; }
    onAccepted: destinationRequested({scope: scope.currentValue || "all", layout: layout.currentIndex === 1 ? "tiles" : "fit",
        compression: ["structure", "screen", "print"][compression.currentIndex], landscape: landscape.checked, overwrite: true})
    contentItem: ColumnLayout
    {
        spacing: 12
        Label { text: "范围"; visible: scope.visible }
        ComboBox
        {
            id: scope
            Layout.fillWidth: true
            visible: ["sheets", "slides", "pdf"].indexOf(root.module) >= 0
            textRole: "label"; valueRole: "key"
            model: root.module === "sheets" ? [{key: "all", label: "全部工作表"}, {key: "current", label: "当前工作表"}, {key: "selection", label: "选中区域"}]
                : [{key: "all", label: "全部可见页面"}, {key: "current", label: "当前页"}]
        }
        CheckBox { id: landscape; text: "A4 横向"; visible: ["text", "word", "sheets"].indexOf(root.module) >= 0 }
        ComboBox { id: layout; Layout.fillWidth: true; visible: root.module === "mindmap"; model: ["A3 整图缩放", "A3 分页拼接（保持字号）"] }
        ComboBox { id: compression; Layout.fillWidth: true; visible: root.module === "pdf"; model: ["整理结构（保留文字）", "小体积 · 屏幕阅读", "较清晰 · 打印阅读"] }
        Label
        {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.theme.textSecondary
            text: root.module === "pdf" ? (compression.currentIndex === 0
                ? "保存当前页面与标注的副本。结构整理不保证缩小体积；文档级书签、附件及交互信息不在本版保留范围内。"
                : "图片压缩会将每页转为图片，文字不可搜索，批注不再单独编辑。仅写入新副本，原文件保留。")
                : root.module === "sheets" ? "导出支持的单元格样式并显示网格，列宽适配纸张。公式使用当前缓存，不计算公式；复杂图表与合并排版不在本版范围内。"
                : root.module === "mindmap" ? "导出全部节点和连线；旧树形导图会展开折叠节点。内容较多时可使用分页拼接。"
                : "按本软件支持的排版导出当前内容。复杂导入文档可能与原版 Office 排版不同；隐藏幻灯片在整稿导出时跳过。"
        }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "完成当前输入后导出，最多 250 页 / 64 MiB。导出不改变原文档的保存状态。"; color: root.theme.textSecondary }
    }
}
