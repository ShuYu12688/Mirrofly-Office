pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RoundedDialog
{
    id: root
    objectName: "markdownImageDialog"
    property var contextInfo: ({})
    signal imageApplied(var options)
    title: "设置图片"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - 80 : 460)
    modal: true
    closePolicy: Popup.CloseOnEscape
    onOpened:
    {
        address.text = String(contextInfo.imageUrl || "");
        alternative.text = String(contextInfo.imageAlt || "");
        caption.text = String(contextInfo.imageTitle || "");
    }
    contentItem: ColumnLayout
    {
        spacing: 14
        TextField
        {
            id: address
            objectName: "markdownImageAddress"
            Layout.fillWidth: true
            placeholderText: "图片相对路径或完整地址"
            selectByMouse: true
            Accessible.name: "图片地址"
        }
        TextField
        {
            id: alternative
            objectName: "markdownImageAlt"
            Layout.fillWidth: true
            placeholderText: "替代文字（可选）"
            selectByMouse: true
            Accessible.name: "图片替代文字"
        }
        TextField
        {
            id: caption
            objectName: "markdownImageTitle"
            Layout.fillWidth: true
            placeholderText: "图片提示（可选）"
            selectByMouse: true
            Accessible.name: "图片提示"
        }
        Text
        {
            Layout.fillWidth: true
            text: "本地图片按文档位置读取；远程或缺失图片显示替代文字。"
            wrapMode: Text.WordWrap
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 1
        }
        RowLayout
        {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton
            {
                theme: root.theme
                text: "取消"
                iconName: ""
                onClicked: root.close()
            }
            ActionButton
            {
                theme: root.theme
                objectName: "markdownImageApply"
                text: "应用图片"
                iconName: "check"
                primary: true
                onClicked:
                {
                    root.close();
                    root.imageApplied({ url: address.text.trim(), alt: alternative.text, title: caption.text });
                }
            }
        }
    }
}
