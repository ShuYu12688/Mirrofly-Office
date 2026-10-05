pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var selection: ({})
    property string section: "fill"
    property bool endColor: false
    readonly property var style: selection.textStyle || ({})
    readonly property bool fillSection: section === "fill" || section === "outline"
    readonly property var activeFill: section === "outline"
        ? ((style.outline || {}).fill || {}) : (style.fill || {})
    readonly property bool gradient: (activeFill.stops || []).length > 0
    readonly property var warps: ["textNoShape", "textArchUp", "textArchDown", "textWave1",
        "textDoubleWave1", "textInflate", "textDeflate", "textSlantUp", "textSlantDown",
        "textChevron", "textCircle"]
    readonly property var fields:
    {
        const opacity = {key: "opacity", label: "不透明度 %", min: 0, max: 100, scale: 100};
        if (fillSection)
        {
            let result = [opacity];
            if (gradient)
                result.push({key: "angle", label: "渐变角度 °", min: -180, max: 180, scale: 1});
            if (section === "outline")
                result.push({key: "width", label: "线宽（0.1 pt）", min: 0, max: 720, scale: 10});
            return result;
        }
        if (section === "shadow")
            return [opacity, {key: "blur", label: "模糊 pt", min: 0, max: 72, scale: 1},
                {key: "x", label: "水平偏移 pt", min: -200, max: 200, scale: 1},
                {key: "y", label: "垂直偏移 pt", min: -200, max: 200, scale: 1}];
        if (section === "glow")
            return [opacity, {key: "radius", label: "光晕 pt", min: 0, max: 72, scale: 1}];
        if (section === "reflection")
            return [opacity, {key: "offset", label: "距离 pt", min: 0, max: 200, scale: 1},
                {key: "endOpacity", label: "末端不透明度 %", min: 0, max: 100, scale: 100},
                {key: "startPosition", label: "淡出起点 %", min: 0, max: 99, scale: 100},
                {key: "endPosition", label: "淡出终点 %", min: 1, max: 100, scale: 100}];
        return [{key: "rotation", label: "文字旋转 °", min: -180, max: 180, scale: 1},
            {key: "warpAdjustment", label: "变形幅度 %", min: 5, max: 45, scale: 100}];
    }
    signal editRequested(string action, var options)

    function copy(value)
    {
        return JSON.parse(JSON.stringify(value || {}));
    }

    function send(group, value)
    {
        let options = {};
        options[group] = value;
        editRequested("formatTextStyle", options);
    }

    function sendFill(fill)
    {
        if (section === "outline")
        {
            let outline = copy(style.outline);
            outline.fill = fill;
            if (!outline.width)
                outline.width = 1;
            send("outline", outline);
        }
        else
            send("fill", fill);
    }

    function changeColor(color)
    {
        if (fillSection)
        {
            let fill = copy(activeFill);
            if (gradient)
                fill.stops[endColor ? fill.stops.length - 1 : 0].color = color;
            else
                fill.color = color;
            sendFill(fill);
        }
        else
        {
            let effect = copy(style[section]);
            effect.color = color;
            if (!effect.opacity)
                effect.opacity = 0.45;
            if (section === "glow" && !effect.radius)
                effect.radius = 3;
            send(section, effect);
        }
    }

    function numberValue(key)
    {
        const value = section === "warp" ? style[key]
            : (fillSection && key !== "width" ? activeFill[key] : (style[section] || {})[key]);
        if (key === "angle" || key === "rotation")
            return value > 180 ? value - 360 : (value || 0);
        return value === undefined ? (key === "opacity" ? 1 : 0) : value;
    }

    function changeNumber(key, value)
    {
        if (section === "warp")
            send(key, value);
        else if (fillSection && key !== "width")
        {
            let fill = copy(activeFill);
            fill[key] = value;
            sendFill(fill);
        }
        else
        {
            let effect = copy(style[section]);
            effect[key] = value;
            send(section, effect);
        }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        Repeater
        {
            model: [{key: "fill", label: "文字填充"}, {key: "outline", label: "文字轮廓"},
                {key: "shadow", label: "阴影"}, {key: "glow", label: "发光"},
                {key: "reflection", label: "倒影"}, {key: "warp", label: "方向与变形"}]
            ActionButton
            {
                required property var modelData
                theme: root.theme
                text: modelData.label
                compact: true
                iconName: ""
                primary: root.section === modelData.key
                onClicked: root.section = modelData.key
            }
        }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 8
        visible: root.fillSection
        ActionButton
        {
            theme: root.theme
            text: "纯色"
            iconName: ""
            compact: true
            primary: !root.gradient
            onClicked: root.sendFill({color: root.activeFill.color || root.theme.textPrimary,
                opacity: root.activeFill.opacity === undefined ? 1 : root.activeFill.opacity})
        }
        ActionButton
        {
            theme: root.theme
            text: "渐变"
            iconName: ""
            compact: true
            primary: root.gradient
            onClicked:
            {
                if (!root.gradient)
                    root.sendFill({opacity: 1, angle: 90, stops: [
                        {position: 0, color: root.activeFill.color || root.theme.textPrimary, opacity: 1},
                        {position: 1, color: root.theme.accent, opacity: 1}]});
            }
        }
        ActionButton
        {
            theme: root.theme
            text: "无填充"
            iconName: ""
            compact: true
            onClicked: root.sendFill({color: "", opacity: 0})
        }
        ActionButton
        {
            theme: root.theme
            text: root.endColor ? "正在设置：末端颜色" : "正在设置：起始颜色"
            iconName: ""
            compact: true
            visible: root.gradient
            onClicked: root.endColor = !root.endColor
        }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 8
        visible: root.section === "warp"
        ComboBox
        {
            Accessible.name: "艺术字变形（近似预览）"
            model: ["无变形", "上弧", "下弧", "波浪", "双波浪", "膨胀", "收缩",
                "上斜", "下斜", "山形", "环形"]
            currentIndex: root.style.warp ? root.warps.indexOf(root.style.warp) : 0
            onActivated: root.send("warp", root.warps[currentIndex])
        }
        ComboBox
        {
            Accessible.name: "文字方向"
            model: ["水平", "顺时针竖排", "逆时针竖排"]
            currentIndex: root.style.vertical === "vert" ? 1 : root.style.vertical === "vert270" ? 2 : 0
            onActivated: root.send("vertical", ["horz", "vert", "vert270"][currentIndex])
        }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 10
        Repeater
        {
            model: root.fields
            RowLayout
            {
                required property var modelData
                Label { text: parent.modelData.label; color: root.theme.textSecondary }
                SpinBox
                {
                    from: parent.modelData.min
                    to: parent.modelData.max
                    value: Math.round(root.numberValue(parent.modelData.key) * parent.modelData.scale)
                    editable: true
                    Accessible.name: parent.modelData.label
                    onValueModified: root.changeNumber(parent.modelData.key, value / parent.modelData.scale)
                }
            }
        }
        ActionButton
        {
            visible: !root.fillSection && root.section !== "warp"
            theme: root.theme
            text: "关闭此效果"
            iconName: ""
            compact: true
            onClicked: root.changeNumber("opacity", 0)
        }
    }

    PresentationPalette
    {
        Layout.fillWidth: true
        theme: root.theme
        visible: root.fillSection || root.section === "shadow" || root.section === "glow"
        onColorSelected: function(value) { root.changeColor(value); }
    }

    Label
    {
        Layout.fillWidth: true
        text: "设置作用于整个文字对象；其他效果保留。变形为近似预览，复杂三维效果仍以原文件为准。"
        color: root.theme.textSecondary
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }
}
