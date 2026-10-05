pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ColumnLayout
{
    id: root
    property var theme
    property string section: ""
    property var format: ({})
    property var systemFontFamilies: []
    signal toolRequested(string action)
    signal formatRequested(var patch)
    signal styleRequested(string preset, var palette)
    signal sizeRequested(bool columns, real size)
    ColorDialog
    {
        id: customColor
        property string field: "fill"
        title: field === "fill" ? "单元格填充颜色" : "文字颜色"
        onAccepted: root.formatRequested(field === "fill" ? {fill: String(selectedColor)} : {text: String(selectedColor)})
    }

    function applyStyle(preset)
    {
        styleRequested(preset, {header: theme.sheetsHeaderFill, body: theme.surfaceColor,
            alternate: theme.sheetsBandFill, headerText: theme.sheetsHeaderText, bodyText: theme.textPrimary});
    }

    RowLayout
    {
        visible: root.section === "font"
        SystemFontPicker
        {
            theme: root.theme
            systemFontFamilies: root.systemFontFamilies
            family: root.format.font || root.theme.fontFamily
            onFamilySelected: function(family) { root.formatRequested({font: family}); }
        }
        SpinBox { from: 6; to: 96; value: Number(root.format.size || 11); editable: true; onValueModified: root.formatRequested({size: String(value)}) }
        Text { text: "磅"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
        ActionButton { theme: root.theme; text: "加粗"; compact: true; primary: root.format.bold === "1"; onClicked: root.formatRequested({bold: root.format.bold === "1" ? "0" : "1"}) }
        ActionButton { theme: root.theme; text: "斜体"; compact: true; primary: root.format.italic === "1"; onClicked: root.formatRequested({italic: root.format.italic === "1" ? "0" : "1"}) }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "font"
        ActionButton { theme: root.theme; text: "增大字号"; compact: true; onClicked: root.toolRequested("grow") }
        ActionButton { theme: root.theme; text: "减小字号"; compact: true; onClicked: root.toolRequested("shrink") }
        ActionButton { theme: root.theme; text: "下划线"; compact: true; primary: root.format.underline === "1"; onClicked: root.formatRequested({underline: root.format.underline === "1" ? "0" : "1"}) }
        ActionButton { theme: root.theme; text: "删除线"; compact: true; primary: root.format.strike === "1"; onClicked: root.formatRequested({strike: root.format.strike === "1" ? "0" : "1"}) }
    }
    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        visible: root.section === "style"
        Repeater
        {
            model: [{key: "normal", label: "常规"}, {key: "good", label: "好"}, {key: "bad", label: "差"}, {key: "neutral", label: "中性"}, {key: "input", label: "输入"}, {key: "output", label: "输出"}, {key: "title", label: "标题"}, {key: "total", label: "汇总"}]
            delegate: ActionButton
            {
                required property var modelData
                theme: root.theme
                text: modelData.label
                compact: true
                onClicked:
                {
                    const key = modelData.key;
                    const fill = key === "good" ? root.theme.sheetsGoodFill : key === "bad" ? root.theme.sheetsBadFill : key === "neutral" || key === "input" ? root.theme.sheetsNeutralFill : root.theme.sheetsTableBodyFill;
                    const ink = key === "good" ? root.theme.sheetsGoodText : key === "bad" ? root.theme.sheetsBadText : key === "neutral" || key === "input" ? root.theme.sheetsNeutralText : root.theme.sheetsTableBodyText;
                    root.styleRequested(key, {fill: fill, text: ink, line: root.theme.sheetsStrongLine});
                }
            }
        }
    }
    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        visible: root.section === "style"
        ActionButton { theme: root.theme; text: "表头强调"; compact: true; onClicked: root.applyStyle("header") }
        ActionButton { theme: root.theme; text: "恢复导入样式"; compact: true; onClicked: root.applyStyle("plain") }
    }
    RowLayout
    {
        visible: root.section === "colors"
        Text { text: "填充"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
        Repeater
        {
            model: [root.theme.surfaceColor, root.theme.sheetsBandFill, root.theme.sheetsHeaderFill, root.theme.spectrumCoral, root.theme.spectrumGold, root.theme.spectrumBlue]
            delegate: Rectangle
            {
                required property var modelData
                implicitWidth: 30
                implicitHeight: 30
                color: modelData
                radius: 5
                border.color: root.theme.borderColor
                TapHandler { onTapped: root.formatRequested({fill: String(parent.modelData)}) }
            }
        }
        ActionButton { theme: root.theme; text: "无填充"; compact: true; onClicked: root.formatRequested({fill: "none"}) }
        ActionButton { theme: root.theme; text: "深色字"; compact: true; onClicked: root.formatRequested({text: root.theme.textPrimary}) }
        ActionButton { theme: root.theme; text: "浅色字"; compact: true; onClicked: root.formatRequested({text: root.theme.sheetsHeaderText}) }
    }
    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        visible: root.section === "colors"
        ActionButton { theme: root.theme; text: "自选填充颜色…"; compact: true; onClicked: { customColor.field = "fill"; customColor.selectedColor = root.format.fill && root.format.fill !== "none" ? root.format.fill : root.theme.surfaceColor; customColor.open(); } }
        ActionButton { theme: root.theme; text: "自选文字颜色…"; compact: true; onClicked: { customColor.field = "text"; customColor.selectedColor = root.format.text || root.theme.textPrimary; customColor.open(); } }
    }
    RowLayout
    {
        visible: root.section === "align"
        Repeater
        {
            model: [{key: "left", text: "靠左"}, {key: "center", text: "居中"}, {key: "right", text: "靠右"}, {key: "justify", text: "两端对齐"}, {key: "distributed", text: "分散对齐"}]
            delegate: ActionButton
            {
                required property var modelData
                theme: root.theme
                text: modelData.text
                compact: true
                primary: root.format.align === modelData.key
                onClicked: root.formatRequested({align: modelData.key})
            }
        }
        ActionButton { theme: root.theme; text: "自动换行"; compact: true; primary: root.format.wrap === "1"; onClicked: root.formatRequested({wrap: root.format.wrap === "1" ? "0" : "1"}) }
        ActionButton { theme: root.theme; text: "缩小字体填充"; compact: true; primary: root.format.shrinkToFit === "1"; onClicked: root.formatRequested({shrinkToFit: root.format.shrinkToFit === "1" ? "0" : "1"}) }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "align"
        ComboBox { model: ["顶端", "垂直居中", "底端"]; currentIndex: root.format.valign === "top" ? 0 : root.format.valign === "center" ? 1 : 2; onActivated: root.formatRequested({valign: ["top", "center", "bottom"][currentIndex]}) }
        ActionButton { theme: root.theme; text: "增加缩进"; compact: true; onClicked: root.formatRequested({indent: String(Math.min(15, Number(root.format.indent || 0) + 1))}) }
        ActionButton { theme: root.theme; text: "减少缩进"; compact: true; onClicked: root.formatRequested({indent: String(Math.max(0, Number(root.format.indent || 0) - 1))}) }
    }
    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        visible: root.section === "align"
        ComboBox
        {
            model: ["水平文字", "逆时针45°", "顺时针45°", "向上90°", "向下90°", "竖排文字"]
            readonly property var codes: [0, 45, 135, 90, 180, 255]
            currentIndex: codes.indexOf(Number(root.format.textRotation || 0))
            onActivated: root.formatRequested({textRotation: codes[currentIndex]})
        }
        Text { text: "角度（逆时针为正）"; color: root.theme.textSecondary }
        SpinBox
        {
            from: -90; to: 90; editable: true
            enabled: Number(root.format.textRotation || 0) !== 255
            value: Number(root.format.textRotation || 0) === 255 ? 0 : Number(root.format.textRotation || 0) <= 90 ? Number(root.format.textRotation || 0) : 90 - Number(root.format.textRotation || 0)
            onValueModified: root.formatRequested({textRotation: value < 0 ? 90 - value : value})
        }
    }
    RowLayout
    {
        visible: root.section === "number"
        Repeater
        {
            model: [{key: "0", text: "常规"}, {key: "2", text: "两位小数"}, {key: "9", text: "整数百分比"}, {key: "10", text: "两位百分比"}]
            delegate: ActionButton
            {
                required property var modelData
                theme: root.theme
                text: modelData.text
                compact: true
                primary: root.format.number === modelData.key
                onClicked: root.formatRequested({number: modelData.key})
            }
        }
        Text { text: "只改变显示格式"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "number"
        ComboBox { model: ["其他格式", "整数", "千分位整数", "千分位两位小数", "人民币", "日期", "文本"]; onActivated: if (currentIndex > 0) root.formatRequested({number: ["0", "1", "3", "4", "currency", "14", "49"][currentIndex]}) }
        Text { text: "小数位"; color: root.theme.textSecondary }
        SpinBox { from: 0; to: 10; value: Number(root.format.decimals || (["2", "4", "10", "currency"].indexOf(root.format.number) >= 0 ? 2 : 0)); enabled: ["14", "49"].indexOf(root.format.number) < 0; onValueModified: root.formatRequested({decimals: String(value), number: root.format.number === "0" ? "2" : root.format.number}) }
    }
    RowLayout
    {
        visible: root.section === "size"
        Text { text: "列宽（字符）"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
        SpinBox { id: columns; from: 3; to: 80; value: Math.round(root.format.columnWidth || 18); editable: true }
        ActionButton { theme: root.theme; text: "设置选区列宽"; compact: true; onClicked: root.sizeRequested(true, columns.value) }
        Text { text: "行高（磅）"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
        SpinBox { id: rows; from: 12; to: 300; value: Math.round(root.format.rowHeight || 26); editable: true }
        ActionButton { theme: root.theme; text: "设置选区行高"; compact: true; onClicked: root.sizeRequested(false, rows.value) }
    }
}
