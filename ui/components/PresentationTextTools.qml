pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var chineseFontFamilies: []
    property var systemFontFamilies: []
    property var theme
    property string section
    property var selection: ({})
    property var paragraphInfoFunction
    property int selectedParagraph: 0
    property string selectedObjectId: ""
    readonly property bool canResetInheritance:
        (root.selection.actions || []).indexOf("resetTextInheritance") >= 0
    readonly property var selectedParagraphInfo:
    {
        const objectId = root.selection.id || "";
        return root.paragraphInfoFunction && objectId
            ? root.paragraphInfoFunction(root.selectedParagraph) : ({});
    }
    onSelectionChanged:
    {
        const objectId = root.selection.id || "";
        if (objectId !== root.selectedObjectId)
        {
            root.selectedObjectId = objectId;
            root.selectedParagraph = 0;
        }
    }
    signal formatRequested(var options)
    signal paragraphRequested(var options)
    signal resetRequested(string property)

    function sourceLabel(source)
    {
        const labels = {theme: "主题", master: "母版", layout: "版式", slide: "本页",
            presentation: "文稿", tableStyle: "表格样式", tableCell: "当前单元格",
            application: "默认", inherited: "继承", approximation: "近似显示"};
        return labels[source] || "未知";
    }

    RowLayout
    {
        visible: root.section === "font"
        spacing: 10

        SystemFontPicker
        {
            Layout.preferredWidth: 290
            theme: root.theme
            family: root.selection.fontFamily || ""
            chineseFontFamilies: root.chineseFontFamilies
            systemFontFamilies: root.systemFontFamilies
            onFamilySelected: function(value) { root.formatRequested({fontFamily: value}); }
        }
        Label { text: "字号"; color: root.theme.textSecondary }
        SpinBox
        {
            from: 1
            to: 400
            value: Math.round(root.selection.fontSize || 20)
            editable: true
            Accessible.name: "文字字号"
            onValueModified: root.formatRequested({fontSize: value})
        }
        Label { text: "pt"; color: root.theme.textSecondary }
    }

    RowLayout
    {
        visible: root.section === "font" && Boolean(root.selection.valid)
        spacing: 14
        Label
        {
            objectName: "presentationFontSourceLabel"
            text: "字体来源：" + root.sourceLabel(root.selection.fontFamilySource)
            color: root.theme.textSecondary
            font.pixelSize: root.theme.fontSize - 2
        }
        Label
        {
            objectName: "presentationFontSizeSourceLabel"
            text: "字号来源：" + root.sourceLabel(root.selection.fontSizeSource)
            color: root.theme.textSecondary
            font.pixelSize: root.theme.fontSize - 2
        }
    }

    RowLayout
    {
        visible: root.section === "font" && root.canResetInheritance
            && (Boolean(root.selection.fontFamilyLocalOverride)
                || Boolean(root.selection.fontSizeLocalOverride))
        spacing: 8
        ActionButton
        {
            objectName: "presentationResetFontInheritanceAction"
            theme: root.theme
            text: root.selection.isTableCell ? "恢复单元格字体继承" : "恢复字体继承"
            compact: true
            visible: Boolean(root.selection.fontFamilyLocalOverride)
            onClicked: root.resetRequested("fontFamily")
        }
        ActionButton
        {
            objectName: "presentationResetFontSizeInheritanceAction"
            theme: root.theme
            text: root.selection.isTableCell ? "恢复单元格字号继承" : "恢复字号继承"
            compact: true
            visible: Boolean(root.selection.fontSizeLocalOverride)
            onClicked: root.resetRequested("fontSize")
        }
    }

    RowLayout
    {
        visible: root.section === "style"
        spacing: 8
        CheckBox
        {
            text: "粗体"
            checked: Boolean(root.selection.bold)
            onClicked: root.formatRequested({bold: checked})
        }
        CheckBox
        {
            text: "斜体"
            checked: Boolean(root.selection.italic)
            onClicked: root.formatRequested({italic: checked})
        }
        CheckBox
        {
            text: "下划线"
            checked: Boolean(root.selection.underline)
            onClicked: root.formatRequested({underline: checked})
        }
        CheckBox
        {
            text: "删除线"
            checked: Boolean(root.selection.strike)
            onClicked: root.formatRequested({strike: checked})
        }
        Label { text: "字距"; color: root.theme.textSecondary }
        SpinBox
        {
            from: -50
            to: 200
            value: Math.round(root.selection.characterSpacing || 0)
            editable: true
            Accessible.name: "文字字符间距"
            onValueModified: root.formatRequested({characterSpacing: value})
        }
        Label { text: "pt"; color: root.theme.textSecondary }
        ActionButton
        {
            theme: root.theme
            text: "上标"
            iconName: ""
            compact: true
            primary: (root.selection.baseline || 0) > 0.01
            onClicked: root.formatRequested({baseline: primary ? 0 : 0.3})
        }
        ActionButton
        {
            theme: root.theme
            text: "下标"
            iconName: ""
            compact: true
            primary: (root.selection.baseline || 0) < -0.01
            onClicked: root.formatRequested({baseline: primary ? 0 : -0.25})
        }
    }

    PresentationPalette
    {
        Layout.fillWidth: true
        visible: root.section === "style"
        theme: root.theme
        onColorSelected: function(value) { root.formatRequested({textColor: value}); }
    }

    Label
    {
        objectName: "presentationTextColorSourceLabel"
        visible: root.section === "style" && Boolean(root.selection.valid)
        text: "文字颜色来源：" + root.sourceLabel(root.selection.textColorSource)
        color: root.theme.textSecondary
        font.pixelSize: root.theme.fontSize - 2
    }

    ActionButton
    {
        objectName: "presentationResetTextColorInheritanceAction"
        theme: root.theme
        text: root.selection.isTableCell ? "恢复单元格文字颜色继承" : "恢复文字颜色继承"
        compact: true
        visible: root.section === "style" && root.canResetInheritance
            && Boolean(root.selection.textColorLocalOverride)
        onClicked: root.resetRequested("color")
    }

    RowLayout
    {
        visible: root.section === "paragraph"
        spacing: 8

        Repeater
        {
            model: [{label: "左对齐", value: "left"}, {label: "居中", value: "center"},
                {label: "右对齐", value: "right"}, {label: "两端对齐", value: "justify"}]
            ActionButton
            {
                required property var modelData

                theme: root.theme
                text: modelData.label
                primary: root.selection.alignment === modelData.value
                compact: true
                iconName: ""
                onClicked: root.paragraphRequested({alignment: modelData.value})
            }
        }
        CheckBox
        {
            text: "项目符号"
            checked: Boolean(root.selection.bullet)
            onClicked: root.paragraphRequested({bullet: checked, numbered: false})
        }
        CheckBox
        {
            text: "编号"
            checked: Boolean(root.selection.numbered)
            onClicked: root.paragraphRequested({numbered: checked, bullet: false})
        }
        Label { visible: Boolean(root.selection.numbered); text: "起始"; color: root.theme.textSecondary }
        SpinBox
        {
            visible: Boolean(root.selection.numbered)
            from: 1
            to: 32767
            value: root.selection.numberStart || 1
            editable: true
            Accessible.name: "段落编号起始值"
            onValueModified: root.paragraphRequested({numberStart: value})
        }
    }

    RowLayout
    {
        visible: root.section === "paragraph"
        spacing: 8

        Label { text: "段落"; color: root.theme.textSecondary }
        SpinBox
        {
            objectName: "presentationParagraphSelector"
            from: 1
            to: Math.max(1, root.selection.paragraphCount || 1)
            value: root.selectedParagraph + 1
            editable: true
            Accessible.name: "选择段落"
            onValueModified: root.selectedParagraph = value - 1
        }
        Label { text: "列表层级"; color: root.theme.textSecondary }
        SpinBox
        {
            objectName: "presentationListLevelControl"
            from: 1
            to: 9
            value: Math.max(1, Math.min(9,
                (root.selectedParagraphInfo.listLevel || 0) + 1))
            editable: true
            Accessible.name: "段落列表层级"
            onValueModified: root.paragraphRequested(
                {paragraphIndex: root.selectedParagraph, listLevel: value - 1})
        }
        Label { text: "第 1 级为基础层级"; color: root.theme.textSecondary }
    }

    Flow
    {
        Layout.fillWidth: true
        visible: root.section === "paragraph"
        spacing: 8

        Repeater
        {
            model: [{key: "lineSpacing", label: "行距 %", from: 50, to: 500,
                    value: Math.round((root.selection.lineSpacing || 1) * 100), scale: 100},
                {key: "spaceBefore", label: "段前", from: 0, to: 400,
                    value: Math.round(root.selection.spaceBefore || 0), scale: 1},
                {key: "spaceAfter", label: "段后", from: 0, to: 400,
                    value: Math.round(root.selection.spaceAfter || 0), scale: 1},
                {key: "marginLeft", label: "左缩进", from: 0, to: 1000,
                    value: Math.round(root.selection.paragraphMarginLeft || 0), scale: 1},
                {key: "firstLineIndent", label: "首行", from: -1000, to: 1000,
                    value: Math.round(root.selection.firstLineIndent || 0), scale: 1}]
            Row
            {
                id: paragraphField

                required property var modelData
                spacing: 5

                Label
                {
                    anchors.verticalCenter: parent.verticalCenter
                    text: paragraphField.modelData.label
                    color: root.theme.textSecondary
                }
                SpinBox
                {
                    from: paragraphField.modelData.from
                    to: paragraphField.modelData.to
                    value: paragraphField.modelData.value
                    editable: true
                    Accessible.name: "段落" + paragraphField.modelData.label
                    onValueModified:
                    {
                        const options = {};
                        options[paragraphField.modelData.key] = value / paragraphField.modelData.scale;
                        root.paragraphRequested(options);
                    }
                }
                Label
                {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: paragraphField.modelData.key !== "lineSpacing"
                    text: "pt"
                    color: root.theme.textSecondary
                }
            }
        }
    }
}
