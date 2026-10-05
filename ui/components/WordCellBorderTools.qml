pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Flow
{
    id: root
    property var theme
    property var borders
    signal formatRequested(string action, var value, bool keepFocus)
    readonly property var edgeNames: ["left", "top", "right", "bottom"]
    readonly property var styleNames: ["nil", "single", "double", "dotted", "dashed"]
    readonly property var current: borders && borders.source ? (borders.source[edgeNames[edge.currentIndex]] || {}) : ({})
    readonly property string currentStyle: current.style && styleNames.indexOf(current.style) >= 0 ? current.style : "single"
    readonly property bool knownStyle: styleNames.indexOf(current.style || "nil") >= 0 && current.mixed !== true
    readonly property string currentColor: current.color || String(theme.textPrimary)
    readonly property real currentWidth: current.width !== undefined ? current.width : 0.5
    spacing: theme.spacing
    enabled: Boolean(borders && borders.editable === true)

    function apply(style, color, width, keepFocus)
    {
        formatRequested("cellBorder", {edge: edgeNames[edge.currentIndex], style: style, color: color, width: width}, keepFocus);
    }

    ComboBox
    {
        id: edge
        objectName: "wordCellBorderEdge"
        Accessible.name: "单元格边框方向"
        model: ["左边框", "上边框", "右边框", "下边框"]
    }
    ComboBox
    {
        objectName: "wordCellBorderStyle"
        Accessible.name: "边框线型"
        model: ["无边框", "实线", "双线", "点线", "虚线"]
        currentIndex: root.current.mixed ? -1 : root.styleNames.indexOf(root.current.style || "nil")
        displayText: currentIndex < 0 ? (root.current.mixed ? "多种线型" : "原文件线型") : currentText
        onActivated: root.apply(root.styleNames[currentIndex], root.currentColor, currentIndex ? Math.max(0.25, root.currentWidth) : 0, false)
    }
    SpinBox
    {
        id: borderWidth
        objectName: "wordCellBorderWidth"
        Accessible.name: "边框宽度（磅）"
        enabled: root.knownStyle
        from: 2; to: 96; stepSize: 1
        value: Math.max(from, Math.min(to, Math.round(root.currentWidth * 8)))
        editable: true
        validator: DoubleValidator { bottom: 0.25; top: 12; decimals: 3 }
        textFromValue: function(value, locale) { return Number(value / 8).toLocaleString(locale, "f", 3) + " pt"; }
        valueFromText: function(text, locale)
        {
            const amount = Number.fromLocaleString(locale, text.replace(/\s*pt\s*$/, ""));
            return isNaN(amount) ? borderWidth.value : Math.round(amount * 8);
        }
        onValueModified: root.apply(root.currentStyle === "nil" ? "single" : root.currentStyle, root.currentColor, value / 8, true)
    }
    ActionButton
    {
        theme: root.theme; text: "边框颜色…"; compact: true
        enabled: root.knownStyle
        onClicked: borderColor.open()
    }
    ColorDialog
    {
        id: borderColor
        title: "边框颜色"
        selectedColor: root.currentColor
        onAccepted: root.apply(root.currentStyle, String(selectedColor), root.currentStyle === "nil" ? 0 : Math.max(0.25, root.currentWidth), false)
    }
    Text
    {
        visible: Boolean(root.borders && root.borders.display && root.borders.display.exact === false)
        text: "此表格含暂未精确显示的边框，保存会保留原始样式。"
        color: root.theme.textSecondary
    }
}
