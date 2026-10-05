pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var selection: ({})
    property string colorTarget: "patternForegroundColor"
    signal editRequested(string action, var options)

    RowLayout
    {
        Label { text: "图案"; color: root.theme.textSecondary }
        ComboBox
        {
            objectName: "presentationPatternPreset"
            Layout.preferredWidth: 150
            model: ["none"].concat(root.selection.patternPresets || [])
            currentIndex: Math.max(0, model.indexOf(root.selection.fillPattern || "none"))
            displayText: root.selection.patternFillSupported === false
                ? "原始图案：" + root.selection.fillPattern : currentText
            Accessible.name: "对象图案填充"
            onActivated: root.editRequested("formatShape", {fillPattern: currentText})
        }
        ActionButton
        {
            theme: root.theme
            text: "前景色"
            iconName: ""
            compact: true
            primary: root.colorTarget === "patternForegroundColor"
            enabled: (root.selection.fillPattern || "").length > 0
            onClicked: root.colorTarget = "patternForegroundColor"
        }
        ActionButton
        {
            theme: root.theme
            text: "背景色"
            iconName: ""
            compact: true
            primary: root.colorTarget === "patternBackgroundColor"
            enabled: (root.selection.fillPattern || "").length > 0
            onClicked: root.colorTarget = "patternBackgroundColor"
        }
    }
    Label
    {
        Layout.fillWidth: true
        visible: (root.selection.fillPattern || "").length > 0 && root.selection.patternFillSupported !== false
        text: "常用图案采用近似纹理预览，文件保留标准图案名称与前景、背景颜色。"
        color: root.theme.textSecondary
        wrapMode: Text.WordWrap
    }
    Label
    {
        Layout.fillWidth: true
        visible: root.selection.patternFillSupported === false
        text: "此图案仅显示背景色，原始图案和前景色随文件保留。选择支持的图案后可修改颜色。"
        color: root.theme.textSecondary
        wrapMode: Text.WordWrap
    }
    PresentationPalette
    {
        theme: root.theme
        visible: (root.selection.fillPattern || "").length > 0
        enabled: root.selection.patternFillSupported !== false
        onColorSelected: function(value)
        {
            const options = {};
            options[root.colorTarget] = value;
            root.editRequested("formatShape", options);
        }
    }
}
