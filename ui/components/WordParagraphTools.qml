pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Flow
{
    id: root
    property var theme
    property string section
    property var selection
    property bool editable
    signal formatRequested(string action, var value, bool keepFocus)
    readonly property int lineRule: selection.lineSpacingRule || 0
    readonly property real lineValue: lineRule ? (selection.lineSpacingPoints || 12) : (selection.spacing || 1)

    spacing: root.theme.spacing
    visible: section === "align" || section === "indent" || section === "cell"
    enabled: editable && (section !== "cell" || selection.inTable === true)

    ColorDialog
    {
        id: cellColor
        title: "单元格底色"
        selectedColor: root.selection.cellFill || root.theme.accentSoft
        onAccepted: root.formatRequested("cellFill", String(selectedColor), false)
    }
    Text
    {
        visible: root.section === "cell"
        text: root.selection.inTable ? "当前单元格" : "请先把光标放入表格单元格"
        color: root.theme.textSecondary
    }
    ComboBox
    {
        objectName: "wordCellAlignment"
        visible: root.section === "cell"
        Accessible.name: "单元格垂直对齐"
        model: ["顶端对齐", "垂直居中", "底端对齐"]
        currentIndex: root.selection.cellAlign || 0
        onActivated: root.formatRequested("cellAlign", currentIndex, false)
    }
    ActionButton
    {
        visible: root.section === "cell"
        theme: root.theme; text: "单元格底色…"; compact: true
        onClicked: cellColor.open()
    }
    ActionButton
    {
        visible: root.section === "cell"
        theme: root.theme; text: "清除底色"; compact: true
        onClicked: root.formatRequested("cellFill", "", false)
    }

    Repeater
    {
        model: root.section === "align" ? ["左对齐", "居中", "右对齐", "两端对齐", "分散对齐"] : []
        delegate: ActionButton
        {
            required property string modelData
            required property int index
            theme: root.theme; iconName: ""; text: modelData; compact: true
            primary: root.selection.align === index
            onClicked: root.formatRequested("align", index, false)
        }
    }
    WordCellBorderTools
    {
        visible: root.section === "cell"
        theme: root.theme
        borders: root.selection.cellBorders
        onFormatRequested: function(action, value, keepFocus) { root.formatRequested(action, value, keepFocus); }
    }
    ComboBox
    {
        objectName: "wordLineSpacingRule"
        visible: root.section === "align"
        Accessible.name: "行距类型"
        model: ["多倍行距", "固定行距（pt）", "最小行距（pt）"]
        currentIndex: root.lineRule
        onActivated:
        {
            const amount = currentIndex === 0 ? (root.selection.spacing || 1)
                : (root.selection.lineSpacingPoints || (root.selection.size || 12) * (root.selection.spacing || 1));
            root.formatRequested("lineSpacing", {rule: currentIndex, value: Math.max(1, Math.min(currentIndex ? 144 : 2, amount))}, false);
        }
    }
    WordNumberSpinBox
    {
        objectName: "wordLineSpacingValue"
        visible: root.section === "align"
        Accessible.name: root.lineRule ? "行距磅值" : "行距倍数"
        number: root.lineValue; minimum: 1; maximum: root.lineRule ? 144 : 2
        increment: root.lineRule ? 0.5 : 0.05
        onNumberModified: function(value) { root.formatRequested("lineSpacing", {rule: root.lineRule, value: value}, true); }
    }
    ActionButton
    {
        visible: root.section === "align"
        theme: root.theme; text: "从右向左"; compact: true; primary: root.selection.rtl || false
        onClicked: root.formatRequested("rtl", !root.selection.rtl, false)
    }
    Repeater
    {
        model: root.section === "indent" ? [
            {key: "leftIndent", label: "左缩进（pt）", min: 0, max: 504},
            {key: "rightIndent", label: "右缩进（pt）", min: 0, max: 504},
            {key: "firstLineIndent", label: "首行（负数为悬挂）", min: -144, max: 144},
            {key: "spaceBefore", label: "段前（pt）", min: 0, max: 144},
            {key: "spaceAfter", label: "段后（pt）", min: 0, max: 144}]
            : root.section === "cell" ? [
                {key: "cellLeft", label: "左内边距（pt）", min: 0, max: 144},
                {key: "cellTop", label: "上内边距（pt）", min: 0, max: 144},
                {key: "cellRight", label: "右内边距（pt）", min: 0, max: 144},
                {key: "cellBottom", label: "下内边距（pt）", min: 0, max: 144}] : []
        delegate: Column
        {
            id: parameter
            required property var modelData
            spacing: 2
            Text { text: parameter.modelData.label; color: root.theme.textSecondary }
            WordNumberSpinBox
            {
                objectName: "wordParagraph_" + parameter.modelData.key
                Accessible.name: parameter.modelData.label
                number: root.selection[parameter.modelData.key] || 0
                minimum: parameter.modelData.min; maximum: parameter.modelData.max
                onNumberModified: function(value) { root.formatRequested(parameter.modelData.key, value, true); }
            }
        }
    }
}
