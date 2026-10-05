pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var selection: ({})
    signal editRequested(string action, var options)

    spacing: 8

    Flow
    {
        Layout.fillWidth: true
        spacing: 8

        Repeater
        {
            model: [{key: "insetLeft", label: "左"}, {key: "insetRight", label: "右"},
                {key: "insetTop", label: "上"}, {key: "insetBottom", label: "下"}]
            Row
            {
                id: insetField

                required property var modelData
                spacing: 5

                Label
                {
                    anchors.verticalCenter: parent.verticalCenter
                    text: insetField.modelData.label
                    color: root.theme.textSecondary
                }
                SpinBox
                {
                    from: 0
                    to: 1000
                    value: Math.round(root.selection[insetField.modelData.key] || 0)
                    editable: true
                    Accessible.name: "文本框" + insetField.modelData.label + "内边距"
                    onValueModified:
                    {
                        const options = {};
                        options[insetField.modelData.key] = value;
                        root.editRequested("formatTextBox", options);
                    }
                }
                Label
                {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "pt"
                    color: root.theme.textSecondary
                }
            }
        }
    }

    RowLayout
    {
        spacing: 8

        Label { text: "垂直对齐"; color: root.theme.textSecondary }
        Repeater
        {
            model: [{label: "顶部", value: "top"}, {label: "居中", value: "center"},
                {label: "底部", value: "bottom"}]
            ActionButton
            {
                required property var modelData

                theme: root.theme
                text: modelData.label
                iconName: ""
                compact: true
                primary: root.selection.verticalAlignment === modelData.value
                onClicked: root.editRequested("formatTextBox", {verticalAlignment: modelData.value})
            }
        }
        CheckBox
        {
            text: "自动换行"
            checked: root.selection.wrap !== false
            onClicked: root.editRequested("formatTextBox", {wrap: checked})
        }
        CheckBox
        {
            text: "溢出时缩小文字"
            checked: Boolean(root.selection.autoFit)
            onClicked: root.editRequested("formatTextBox", {autoFit: checked})
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout
    {
        CheckBox
        {
            text: "裁剪超出底边的文字"
            checked: Boolean((root.selection.textStyle || {}).clipVertical)
            onClicked: root.editRequested("formatTextStyle", {clipVertical: checked})
        }
        CheckBox
        {
            text: "裁剪超出侧边的文字"
            checked: Boolean((root.selection.textStyle || {}).clipHorizontal)
            onClicked: root.editRequested("formatTextStyle", {clipHorizontal: checked})
        }
    }
}
