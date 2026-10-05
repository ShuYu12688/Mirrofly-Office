pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var selection: ({})
    property string sourceId: ""
    signal captureRequested()
    signal applyRequested()
    signal cancelRequested()

    spacing: 8

    Text
    {
        Layout.fillWidth: true
        text: root.sourceId.length > 0
            ? "已取样。点击画布上的目标对象即可应用；也可先选中目标，再按“应用”。"
            : "选中源对象并取样，再点击要套用格式的对象。文字、位置、图片内容和链接会保留。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    RowLayout
    {
        spacing: 8

        ActionButton
        {
            theme: root.theme
            text: "取样格式"
            compact: true
            enabled: root.selection.valid === true &&
                (root.selection.actions || []).indexOf("applyFormat") >= 0
            onClicked: root.captureRequested()
        }
        ActionButton
        {
            theme: root.theme
            text: "应用到当前对象"
            compact: true
            enabled: root.sourceId.length > 0 && root.selection.valid === true &&
                root.selection.id !== root.sourceId &&
                (root.selection.actions || []).indexOf("applyFormat") >= 0
            onClicked: root.applyRequested()
        }
        ActionButton
        {
            theme: root.theme
            text: "取消"
            compact: true
            enabled: root.sourceId.length > 0
            onClicked: root.cancelRequested()
        }
        Item { Layout.fillWidth: true }
    }
}
