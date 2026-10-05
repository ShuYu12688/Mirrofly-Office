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
    signal replaceRequested()

    spacing: 8

    Flow
    {
        Layout.fillWidth: true
        spacing: 8

        Repeater
        {
            model: [{key: "cropLeft", selectionKey: "imageCropLeft", label: "左"},
                {key: "cropTop", selectionKey: "imageCropTop", label: "上"},
                {key: "cropRight", selectionKey: "imageCropRight", label: "右"},
                {key: "cropBottom", selectionKey: "imageCropBottom", label: "下"}]
            Row
            {
                id: cropField

                required property var modelData
                spacing: 5

                Label
                {
                    anchors.verticalCenter: parent.verticalCenter
                    text: cropField.modelData.label
                    color: root.theme.textSecondary
                }
                SpinBox
                {
                    from: 0
                    to: 95
                    value: Math.round((root.selection[cropField.modelData.selectionKey] || 0) * 100)
                    editable: true
                    Accessible.name: "图片" + cropField.modelData.label + "侧裁剪百分比"
                    onValueModified:
                    {
                        const options = {};
                        options[cropField.modelData.key] = value / 100;
                        root.editRequested("formatImage", options);
                    }
                }
                Label
                {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "%"
                    color: root.theme.textSecondary
                }
            }
        }
    }

    RowLayout
    {
        spacing: 8

        Label { text: "透明度"; color: root.theme.textSecondary }
        SpinBox
        {
            from: 0
            to: 100
            value: Math.round((1 - (root.selection.imageOpacity === undefined
                ? 1 : root.selection.imageOpacity)) * 100)
            editable: true
            Accessible.name: "图片透明度百分比"
            onValueModified: root.editRequested("formatImage", {opacity: 1 - value / 100})
        }
        Label { text: "%"; color: root.theme.textSecondary }
        ActionButton
        {
            theme: root.theme
            text: "替换图片"
            iconName: "folder"
            compact: true
            onClicked: root.replaceRequested()
        }
        ActionButton
        {
            theme: root.theme
            text: "重置裁剪"
            iconName: ""
            compact: true
            onClicked: root.editRequested("formatImage",
                {cropLeft: 0, cropTop: 0, cropRight: 0, cropBottom: 0})
        }
        Text
        {
            text: "左右、上下裁剪合计须小于 100%。"
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
        }
        Item { Layout.fillWidth: true }
    }
}
