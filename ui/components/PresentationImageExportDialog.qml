pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RoundedDialog
{
    id: root
    title: "导出幻灯片图片"
    modal: true
    anchors.centerIn: parent
    width: Math.min(500, parent ? parent.width - 40 : 500)
    standardButtons: Dialog.Ok | Dialog.Cancel
    signal destinationRequested(var options)

    onOpened:
    {
        format.currentIndex = 0;
        scope.currentIndex = 0;
        longEdge.currentIndex = 1;
    }
    onAccepted: destinationRequested({format: format.currentValue, scope: scope.currentValue,
        longEdge: longEdge.currentValue})

    contentItem: ColumnLayout
    {
        spacing: 12

        Label { text: "图片格式" }
        ComboBox
        {
            id: format
            objectName: "presentationImageFormat"
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "key"
            model: [{key: "png", label: "PNG · 无损"}, {key: "jpg", label: "JPG · 较小文件"}]
        }

        Label { text: "导出范围" }
        ComboBox
        {
            id: scope
            objectName: "presentationImageScope"
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "key"
            model: [{key: "all", label: "全部可见幻灯片"},
                {key: "current", label: "当前幻灯片（可含隐藏页）"}]
        }

        Label { text: "图片长边" }
        ComboBox
        {
            id: longEdge
            objectName: "presentationImageLongEdge"
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "key"
            model: [{key: 1280, label: "1280 像素"}, {key: 1920, label: "1920 像素"}]
        }

        Label
        {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "选择一个父文件夹。完成后会新建独立的图片文件夹；原演示文稿和已有图片不会被覆盖。"
            color: root.theme.textSecondary
        }
    }
}
