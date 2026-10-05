pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property string section
    property var selection: ({})
    property string paintTarget: "fillColor"
    property bool lockAspect: true
    property int restoreRevision: 0
    signal editRequested(string action, var options)

    function restoreSelection()
    {
        ++restoreRevision;
    }

    function normalizedRotation(value)
    {
        let normalized = value % 360;
        if (normalized > 180) normalized -= 360;
        if (normalized <= -180) normalized += 360;
        return normalized;
    }

    function paintOpacity()
    {
        if (paintTarget === "outlineColor")
            return selection.outlineOpacity === undefined ? 1 : selection.outlineOpacity;
        if (paintTarget === "shadowColor")
            return selection.shadowOpacity === undefined ? 0 : selection.shadowOpacity;
        if (paintTarget === "glowColor")
            return selection.glowOpacity === undefined ? 0 : selection.glowOpacity;
        return selection.fillOpacity === undefined ? 1 : selection.fillOpacity;
    }

    function opacityOption()
    {
        if (paintTarget === "outlineColor") return "outlineOpacity";
        if (paintTarget === "shadowColor") return "shadowOpacity";
        if (paintTarget === "glowColor") return "glowOpacity";
        return "fillOpacity";
    }

    function fillSourceLabel(source)
    {
        if (source === "slide") return "本页覆盖";
        if (source === "layout") return "继承版式";
        if (source === "master") return "继承母版";
        return "未指定";
    }

    onSelectionChanged: restoreSelection()

    RowLayout
    {
        visible: root.section === "geometry"
        spacing: 10

        Repeater
        {
            id: geometryInputs

            model: [{key: "x", label: "X"}, {key: "y", label: "Y"},
                {key: "width", label: "宽"}, {key: "height", label: "高"}]
            RowLayout
            {
                id: geometryField

                required property var modelData

                function restoreValue()
                {
                    const value = root.selection[modelData.key];
                    input.text = typeof value === "number" && Number.isFinite(value)
                        ? String(Number(value.toFixed(2))) : "";
                    input.cursorPosition = 0;
                }

                Connections
                {
                    target: root
                    function onRestoreRevisionChanged() { geometryField.restoreValue(); }
                }

                Label { text: geometryField.modelData.label; color: root.theme.textSecondary }
                TextField
                {
                    id: input

                    objectName: "presentationGeometry_" + geometryField.modelData.key

                    Layout.preferredWidth: 100
                    Accessible.name: geometryField.modelData.label + "（pt）"
                    selectByMouse: true
                    validator: DoubleValidator { bottom: -20000; top: 20000; decimals: 2 }
                    onAccepted:
                    {
                        if (text.trim().length > 0 && Number.isFinite(Number(text)))
                        {
                            const options = {};
                            options[geometryField.modelData.key] = Number(text);
                            if (geometryField.modelData.key === "width"
                                || geometryField.modelData.key === "height")
                            {
                                options.preserveAspect = root.lockAspect;
                            }
                            root.editRequested("transformShape", options);
                        }
                        geometryField.restoreValue();
                    }
                    onActiveFocusChanged: if (!activeFocus) geometryField.restoreValue()
                }
                Component.onCompleted: restoreValue()
            }
        }
        Label { text: "pt · 回车应用"; color: root.theme.textSecondary }
    }

    RowLayout
    {
        visible: root.section === "geometry"
        spacing: 8

        CheckBox
        {
            text: "锁定宽高比"
            checked: root.lockAspect
            onClicked: root.lockAspect = checked
        }
        Label { text: "旋转"; color: root.theme.textSecondary }
        SpinBox
        {
            from: -180
            to: 180
            value: Math.round(root.selection.rotation || 0)
            editable: true
            Accessible.name: "对象旋转角度"
            onValueModified: root.editRequested("transformShape", {rotation: value})
        }
        Label { text: "°"; color: root.theme.textSecondary }
        ActionButton
        {
            theme: root.theme
            text: "左转 90°"
            iconName: ""
            compact: true
            onClicked: root.editRequested("transformShape",
                {rotation: root.normalizedRotation((root.selection.rotation || 0) - 90)})
        }
        ActionButton
        {
            theme: root.theme
            text: "右转 90°"
            iconName: ""
            compact: true
            onClicked: root.editRequested("transformShape",
                {rotation: root.normalizedRotation((root.selection.rotation || 0) + 90)})
        }
        ActionButton
        {
            theme: root.theme
            text: "归零"
            iconName: ""
            compact: true
            onClicked: root.editRequested("transformShape", {rotation: 0})
        }
        ActionButton
        {
            theme: root.theme
            text: "水平翻转"
            iconName: ""
            compact: true
            onClicked: root.editRequested("transformShape", {flipHorizontal: true})
        }
        ActionButton
        {
            theme: root.theme
            text: "垂直翻转"
            iconName: ""
            compact: true
            onClicked: root.editRequested("transformShape", {flipVertical: true})
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout
    {
        visible: root.section === "appearance"
        enabled: !root.selection.isImage
        spacing: 8

        ActionButton
        {
            theme: root.theme
            text: "填充"
            iconName: ""
            compact: true
            primary: root.paintTarget === "fillColor"
            onClicked: root.paintTarget = "fillColor"
        }
        ActionButton
        {
            theme: root.theme
            text: "轮廓"
            iconName: ""
            compact: true
            primary: root.paintTarget === "outlineColor"
            onClicked: root.paintTarget = "outlineColor"
        }
        ActionButton
        {
            theme: root.theme
            text: "阴影"
            iconName: ""
            compact: true
            primary: root.paintTarget === "shadowColor"
            onClicked: root.paintTarget = "shadowColor"
        }
        ActionButton
        {
            theme: root.theme
            text: "发光"
            iconName: ""
            compact: true
            primary: root.paintTarget === "glowColor"
            onClicked: root.paintTarget = "glowColor"
        }
        ActionButton
        {
            theme: root.theme
            text: "清除颜色"
            iconName: ""
            compact: true
            onClicked:
            {
                const options = {};
                options[root.paintTarget] = "";
                root.editRequested("formatShape", options);
            }
        }
        Label { visible: root.paintTarget === "outlineColor"; text: "线宽"; color: root.theme.textSecondary }
        SpinBox
        {
            visible: root.paintTarget === "outlineColor"
            from: 0
            to: 72
            value: Math.round(root.selection.outlineWidth || 0)
            editable: true
            onValueModified: root.editRequested("formatShape", {outlineWidth: value})
        }
        Label { text: "透明度"; color: root.theme.textSecondary }
        SpinBox
        {
            from: 0
            to: 100
            value: Math.round((1 - root.paintOpacity()) * 100)
            editable: true
            Accessible.name: "当前样式透明度"
            onValueModified:
            {
                const options = {};
                options[root.opacityOption()] = 1 - value / 100;
                root.editRequested("formatShape", options);
            }
        }
        Label { text: "%"; color: root.theme.textSecondary }
    }

    RowLayout
    {
        visible: root.section === "appearance" && root.selection.placeholder !== undefined
        spacing: 8

        Label
        {
            text: root.selection.placeholder
                ? "占位符填充：" + root.fillSourceLabel(root.selection.placeholder.fillSource) : ""
            color: root.theme.textSecondary
        }
        ActionButton
        {
            objectName: "presentationResetPlaceholderFill"
            theme: root.theme
            text: "恢复继承填充"
            iconName: ""
            compact: true
            visible: root.selection.placeholder !== undefined
                && root.selection.placeholder.localFillOverride === true
                && root.selection.placeholder.inheritedFillSource !== "none"
            enabled: (root.selection.actions || []).includes("resetPlaceholderFill")
            onClicked: root.editRequested("resetPlaceholderFill", {})
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout
    {
        visible: root.section === "appearance" && root.selection.placeholder !== undefined
        spacing: 8

        Label
        {
            text: root.selection.placeholder
                ? "占位符轮廓：" + root.fillSourceLabel(root.selection.placeholder.outlineSource) : ""
            color: root.theme.textSecondary
        }
        ActionButton
        {
            objectName: "presentationResetPlaceholderOutline"
            theme: root.theme
            text: "恢复继承轮廓"
            iconName: ""
            compact: true
            visible: root.selection.placeholder !== undefined
                && root.selection.placeholder.localOutlineOverride === true
                && root.selection.placeholder.inheritedOutlineSource !== "none"
            enabled: (root.selection.actions || []).includes("resetPlaceholderOutline")
            onClicked: root.editRequested("resetPlaceholderOutline", {})
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout
    {
        visible: root.section === "appearance" && !root.selection.isImage
            && (root.selection.effectsSource === "theme" || root.selection.effectsSource === "direct")
        Label
        {
            objectName: "presentationEffectSource"
            text: root.selection.effectsSource === "theme"
                ? "效果来源：主题样式 " + root.selection.themeEffectStyleIndex
                : "效果来源：本页覆盖"
            color: root.theme.textSecondary
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout
    {
        visible: root.section === "appearance"
        enabled: !root.selection.isImage
        spacing: 8

        CheckBox
        {
            text: "阴影"
            checked: root.selection.shadowEnabled || false
            onClicked: root.editRequested("formatShape", {shadowEnabled: checked})
        }
        Label { text: "模糊"; color: root.theme.textSecondary }
        SpinBox
        {
            from: 0
            to: 72
            value: Math.round(root.selection.shadowBlur || 0)
            editable: true
            onValueModified: root.editRequested("formatShape", {shadowBlur: value})
        }
        Label { text: "X"; color: root.theme.textSecondary }
        SpinBox
        {
            from: -200
            to: 200
            value: Math.round(root.selection.shadowX || 0)
            editable: true
            onValueModified: root.editRequested("formatShape", {shadowX: value})
        }
        Label { text: "Y"; color: root.theme.textSecondary }
        SpinBox
        {
            from: -200
            to: 200
            value: Math.round(root.selection.shadowY || 0)
            editable: true
            onValueModified: root.editRequested("formatShape", {shadowY: value})
        }
        CheckBox
        {
            text: "发光"
            checked: root.selection.glowEnabled || false
            onClicked: root.editRequested("formatShape", {glowEnabled: checked})
        }
        Label { text: "半径"; color: root.theme.textSecondary }
        SpinBox
        {
            from: 0
            to: 72
            value: Math.round(root.selection.glowRadius || 0)
            editable: true
            onValueModified: root.editRequested("formatShape", {glowRadius: value})
        }
        Item { Layout.fillWidth: true }
    }

    PresentationPatternTools
    {
        Layout.fillWidth: true
        visible: root.section === "appearance"
        enabled: !root.selection.isImage
        theme: root.theme
        selection: root.selection
        onEditRequested: function(action, options) { root.editRequested(action, options); }
    }

    PresentationPalette
    {
        Layout.fillWidth: true
        theme: root.theme
        visible: root.section === "appearance"
        enabled: !root.selection.isImage
        onColorSelected: function(value)
        {
            const options = {};
            options[root.paintTarget] = value;
            root.editRequested("formatShape", options);
        }
    }

    RowLayout
    {
        visible: root.section === "appearance"
        enabled: !root.selection.isImage
        spacing: 8

        Label { text: "线型"; color: root.theme.textSecondary }
        ComboBox
        {
            objectName: "presentationLineDashPreset"
            Layout.preferredWidth: 150
            model: root.selection.lineDashPresets || []
            currentIndex: Math.max(0, model.indexOf(root.selection.lineDash || "solid"))
            displayText: root.selection.lineDash === "custom" ? "原始自定义虚线" : currentText
            Accessible.name: "对象轮廓线型"
            onActivated: root.editRequested("formatShape", {lineDash: currentText})
        }
        Label { text: "起点"; color: root.theme.textSecondary }
        ComboBox
        {
            id: headEnd

            Layout.preferredWidth: 110
            textRole: "label"
            valueRole: "value"
            model: [{label: "无", value: "none"}, {label: "三角", value: "triangle"},
                {label: "燕尾", value: "stealth"}, {label: "菱形", value: "diamond"},
                {label: "圆形", value: "oval"}, {label: "箭头", value: "arrow"}]
            currentIndex:
            {
                for (let index = 0; index < model.length; ++index)
                {
                    if (model[index].value === root.selection.lineHead)
                    {
                        return index;
                    }
                }
                return 0;
            }
            Accessible.name: "线条起点箭头"
            onActivated: root.editRequested("formatShape", {lineHead: currentValue})
        }
        Label { text: "终点"; color: root.theme.textSecondary }
        ComboBox
        {
            id: tailEnd

            Layout.preferredWidth: 110
            textRole: "label"
            valueRole: "value"
            model: headEnd.model
            currentIndex:
            {
                for (let index = 0; index < model.length; ++index)
                {
                    if (model[index].value === root.selection.lineTail)
                    {
                        return index;
                    }
                }
                return 0;
            }
            Accessible.name: "线条终点箭头"
            onActivated: root.editRequested("formatShape", {lineTail: currentValue})
        }
        Item { Layout.fillWidth: true }
    }
}
