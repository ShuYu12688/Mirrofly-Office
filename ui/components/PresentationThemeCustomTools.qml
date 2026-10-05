pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property bool available: false
    property var themeState: ({})
    property var chineseFontFamilies: []
    property var systemFontFamilies: []
    property string editorMode: ""
    readonly property var colorSlots: [
        {key: "accent1", label: "强调 1"}, {key: "accent2", label: "强调 2"},
        {key: "accent3", label: "强调 3"}, {key: "accent4", label: "强调 4"},
        {key: "accent5", label: "强调 5"}, {key: "accent6", label: "强调 6"},
        {key: "dk1", label: "深色 1"}, {key: "lt1", label: "浅色 1"},
        {key: "dk2", label: "深色 2"}, {key: "lt2", label: "浅色 2"},
        {key: "hlink", label: "超链接"}, {key: "folHlink", label: "已访问链接"}
    ]
    readonly property var fontSlots: [
        {key: "majorLatin", label: "标题西文"}, {key: "minorLatin", label: "正文西文"},
        {key: "majorEastAsian", label: "标题中文"}, {key: "minorEastAsian", label: "正文中文"}
    ]
    readonly property string colorSlot: colorSlots[Math.max(0, colorSlotPicker.currentIndex)].key
    readonly property string fontSlot: fontSlots[Math.max(0, fontSlotPicker.currentIndex)].key
    signal editRequested(string action, var options)

    function canEditColor()
    {
        return root.available && (root.themeState.editableColorSlots || []).indexOf(root.colorSlot) >= 0;
    }

    function canEditFont()
    {
        return root.available && (root.themeState.editableFontSlots || []).indexOf(root.fontSlot) >= 0;
    }

    function applyColor()
    {
        const color = colorInput.text.trim().toUpperCase();
        if (!root.canEditColor() || !/^#[0-9A-F]{6}$/.test(color))
            return;
        const colors = {};
        colors[root.colorSlot] = color;
        root.editRequested("applyTheme", {colors: colors});
        colorInput.clear();
    }

    function applyFont(family)
    {
        if (!root.canEditFont() || !family || family.length > 128)
            return;
        const fonts = {};
        fonts[root.fontSlot] = family;
        root.editRequested("applyTheme", {fonts: fonts});
    }

    spacing: 7

    RowLayout
    {
        Layout.fillWidth: true
        spacing: 7

        ActionButton
        {
            objectName: "presentationThemeCustomColorMode"
            theme: root.theme
            text: "自定义颜色"
            iconName: ""
            compact: true
            primary: root.editorMode === "color"
            enabled: root.available && (root.themeState.editableColorSlots || []).length > 0
            onClicked: root.editorMode = root.editorMode === "color" ? "" : "color"
        }

        ActionButton
        {
            objectName: "presentationThemeCustomFontMode"
            theme: root.theme
            text: "自定义字体"
            iconName: ""
            compact: true
            primary: root.editorMode === "font"
            enabled: root.available && (root.themeState.editableFontSlots || []).length > 0
            onClicked: root.editorMode = root.editorMode === "font" ? "" : "font"
        }

        Item { Layout.fillWidth: true }
    }

    RowLayout
    {
        Layout.fillWidth: true
        visible: root.editorMode === "color"
        spacing: 7

        ComboBox
        {
            id: colorSlotPicker
            objectName: "presentationThemeColorSlot"
            Layout.preferredWidth: 118
            model: root.colorSlots
            textRole: "label"
            Accessible.name: "主题颜色槽位"
            onActivated: colorInput.clear()
        }

        Rectangle
        {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            radius: 6
            color: (root.themeState.colors || {})[root.colorSlot] || root.theme.slidesPaper
            border.color: root.theme.borderColor
        }

        TextField
        {
            id: colorInput
            objectName: "presentationThemeCustomColorInput"
            Layout.preferredWidth: 112
            placeholderText: (root.themeState.colors || {})[root.colorSlot] || "#RRGGBB"
            maximumLength: 7
            Accessible.name: "新的主题颜色，十六进制"
            enabled: root.canEditColor()
            onAccepted: root.applyColor()
        }

        ActionButton
        {
            objectName: "presentationThemeApplyCustomColor"
            theme: root.theme
            text: "应用颜色"
            iconName: ""
            compact: true
            enabled: root.canEditColor() && /^#[0-9a-fA-F]{6}$/.test(colorInput.text.trim())
            onClicked: root.applyColor()
        }

        Item { Layout.fillWidth: true }
    }

    Text
    {
        Layout.fillWidth: true
        visible: root.editorMode === "color" && !root.canEditColor()
        text: "此颜色槽位在原主题中缺失，保持只读。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
    }

    RowLayout
    {
        Layout.fillWidth: true
        visible: root.editorMode === "font"
        spacing: 7

        ComboBox
        {
            id: fontSlotPicker
            objectName: "presentationThemeFontSlot"
            Layout.preferredWidth: 118
            model: root.fontSlots
            textRole: "label"
            Accessible.name: "主题字体槽位"
        }

        SystemFontPicker
        {
            objectName: "presentationThemeFontPicker"
            Layout.preferredWidth: 235
            theme: root.theme
            enabled: root.canEditFont() && root.systemFontFamilies.length > 0
            chineseOnly: fontSlotPicker.currentIndex >= 2
            chineseFontFamilies: root.chineseFontFamilies
            systemFontFamilies: root.systemFontFamilies
            allowLocalEnumeration: false
            family: (root.themeState.fonts || {})[root.fontSlot] || ""
            onFamilySelected: function(family) { root.applyFont(family); }
        }

        Item { Layout.fillWidth: true }
    }

    Text
    {
        Layout.fillWidth: true
        visible: root.editorMode === "font" && !root.canEditFont()
        text: "此字体槽位在原主题中缺失，保持只读。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
    }
}
