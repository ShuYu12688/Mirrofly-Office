pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property bool available: false
    property var themeState: ({})
    property var chineseFontFamilies: []
    property var systemFontFamilies: []
    signal editRequested(string action, var options)

    function canApplyPreset(preset)
    {
        if (!root.available)
            return false;
        const colors = root.themeState.editableColorSlots || [];
        const fonts = root.themeState.editableFontSlots || [];
        return Object.keys(preset.colors).every(function(slot) { return colors.indexOf(slot) >= 0; })
            && Object.keys(preset.fonts).every(function(slot) { return fonts.indexOf(slot) >= 0; });
    }

    spacing: 8

    Text
    {
        objectName: "presentationCurrentTheme"
        Layout.fillWidth: true
        visible: root.themeState.available === true
        text: "当前主题：" + (root.themeState.name || "未命名") + " · 关联 "
            + (root.themeState.linkedSlideCount || 0) + " 页"
        color: root.theme.textPrimary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize
        textFormat: Text.PlainText
        elide: Text.ElideRight
    }

    Flow
    {
        Layout.fillWidth: true
        visible: root.themeState.available === true && customEditor.editorMode === ""
        spacing: 7

        Repeater
        {
            model: ["accent1", "accent2", "accent3", "accent4", "accent5", "accent6"]

            Column
            {
                id: swatch
                required property string modelData
                spacing: 3

                Rectangle
                {
                    width: 40
                    height: 20
                    radius: 5
                    color: (root.themeState.colors || {})[swatch.modelData] || root.theme.slidesPaper
                    border.color: root.theme.borderColor
                }

                Text
                {
                    text: swatch.modelData.replace("accent", "强调")
                    color: root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 3
                    textFormat: Text.PlainText
                }
            }
        }
    }

    Text
    {
        Layout.fillWidth: true
        visible: root.themeState.available === true && customEditor.editorMode === ""
        text: "标题字体：" + ((root.themeState.fonts || {}).majorLatin || "未指定")
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        textFormat: Text.PlainText
        elide: Text.ElideRight
    }

    Text
    {
        Layout.fillWidth: true
        visible: customEditor.editorMode === ""
        text: root.available
            ? "更换当前页面关联的主题；使用同一主题的页面会一起更新。"
            : "当前页面缺少可编辑主题关联；可以新建演示文稿，或打开带主题的演示文件并创建可编辑副本。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    Flow
    {
        Layout.fillWidth: true
        visible: customEditor.editorMode === ""
        spacing: 7

        Repeater
        {
            model: [
                {label: "海蓝课堂", colors: {dk2: "#203A5E", lt2: "#F3F7FB", accent1: "#3269B0",
                    accent2: "#55A5C8", accent3: "#6C8DC6", accent4: "#805FA8", accent5: "#36A4A0",
                    accent6: "#D8944E", hlink: "#245CBA"},
                    fonts: {majorLatin: "Segoe UI", minorLatin: "Arial",
                        majorEastAsian: "Microsoft YaHei", minorEastAsian: "Microsoft YaHei"}},
                {label: "暖橙课堂", colors: {dk2: "#523222", lt2: "#FFF7EF", accent1: "#D46A32",
                    accent2: "#E69B46", accent3: "#BD7D4A", accent4: "#8D624F", accent5: "#C15B62",
                    accent6: "#DBB260", hlink: "#B9512D"},
                    fonts: {majorLatin: "Georgia", minorLatin: "Arial",
                        majorEastAsian: "Microsoft YaHei", minorEastAsian: "Microsoft YaHei"}},
                {label: "墨绿讲堂", colors: {dk2: "#203C35", lt2: "#F3F8F5", accent1: "#247A61",
                    accent2: "#63A76B", accent3: "#4E958F", accent4: "#798D5B", accent5: "#4B829C",
                    accent6: "#BEA366", hlink: "#267B72"},
                    fonts: {majorLatin: "Segoe UI", minorLatin: "Arial",
                        majorEastAsian: "Microsoft YaHei", minorEastAsian: "Microsoft YaHei"}}
            ]

            ActionButton
            {
                required property var modelData

                objectName: "presentationThemePreset_" + modelData.label
                theme: root.theme
                text: modelData.label
                iconName: ""
                compact: true
                enabled: root.canApplyPreset(modelData)
                onClicked: root.editRequested("applyTheme",
                    {colors: modelData.colors, fonts: modelData.fonts})
            }
        }
    }

    PresentationThemeCustomTools
    {
        id: customEditor
        objectName: "presentationThemeCustomTools"
        Layout.fillWidth: true
        theme: root.theme
        available: root.available
        themeState: root.themeState
        chineseFontFamilies: root.chineseFontFamilies
        systemFontFamilies: root.systemFontFamilies
        onEditRequested: function(action, options) { root.editRequested(action, options); }
    }
}
