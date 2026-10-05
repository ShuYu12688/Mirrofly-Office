pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var selection: ({})
    property string colorTarget: "start"
    signal editRequested(string action, var options)

    function gradientColor(key, fallback)
    {
        const value = root.selection[key];
        return value && value.length > 0 ? value : fallback;
    }

    RowLayout
    {
        spacing: 8

        ActionButton
        {
            theme: root.theme
            text: "起始颜色"
            iconName: ""
            compact: true
            primary: root.colorTarget === "start"
            onClicked: root.colorTarget = "start"
        }
        ActionButton
        {
            theme: root.theme
            text: "结束颜色"
            iconName: ""
            compact: true
            primary: root.colorTarget === "end"
            onClicked: root.colorTarget = "end"
        }
        Label { text: "角度"; color: root.theme.textSecondary }
        SpinBox
        {
            from: -180
            to: 180
            value: Math.round(root.selection.gradientAngle || 0)
            editable: true
            enabled: Boolean(root.selection.hasGradient)
            Accessible.name: "渐变角度"
            onValueModified: root.editRequested("formatShape", {gradientAngle: value})
        }
        Label { text: "°"; color: root.theme.textSecondary }
        ActionButton
        {
            theme: root.theme
            text: "转为纯色"
            iconName: ""
            compact: true
            enabled: Boolean(root.selection.hasGradient)
            onClicked: root.editRequested("formatShape",
                {fillColor: root.gradientColor("gradientStartColor", root.theme.slidesPaper)})
        }
        Item { Layout.fillWidth: true }
    }

    PresentationPalette
    {
        Layout.fillWidth: true
        theme: root.theme
        onColorSelected: function(value)
        {
            const fallback = root.selection.fillColor && root.selection.fillColor.length > 0
                ? root.selection.fillColor : root.theme.slidesPaper;
            const start = root.colorTarget === "start" ? value
                : root.gradientColor("gradientStartColor", fallback);
            const end = root.colorTarget === "end" ? value
                : root.gradientColor("gradientEndColor", fallback);
            root.editRequested("formatShape", {gradientStartColor: start, gradientEndColor: end});
        }
    }
}
