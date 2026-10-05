pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle
{
    id: root
    required property var theme
    required property var entry
    property bool expanded: false
    readonly property bool chat: entry.kind === "user" || entry.kind === "assistant"
    height: content.implicitHeight + theme.spacing * 2
    radius: theme.radius * 0.6
    color: entry.kind === "user" ? theme.accentSoft : theme.surfaceColor
    border.width: chat ? 0 : 1
    border.color: theme.borderColor

    ColumnLayout
    {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.theme.spacing
        spacing: root.theme.spacing / 2
        RowLayout
        {
            Layout.fillWidth: true
            Text
            {
                visible: !root.chat
                text: root.entry.state === "running" ? "◌" : root.entry.state === "done" ? "✓" : "!"
                color: root.theme.accent
                font.pixelSize: root.theme.fontSize
            }
            Text
            {
                Layout.fillWidth: true
                text: root.entry.title
                textFormat: Text.PlainText
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - (root.chat ? 0 : 2)
                font.bold: root.chat
                wrapMode: Text.Wrap
            }
            ToolButton
            {
                visible: !root.chat
                text: root.expanded ? "收起" : "详情"
                Accessible.name: (root.expanded ? "收起 " : "展开 ") + root.entry.title
                onClicked: root.expanded = !root.expanded
            }
        }
        Loader
        {
            id: detail
            objectName: "aiTraceDetailLoader"
            Layout.fillWidth: true
            Layout.preferredHeight: active && item ? (root.chat ? (item as Item).implicitHeight : root.theme.aiDetailHeight) : 0
            active: root.chat || root.expanded
            visible: active
            sourceComponent: ScrollView
            {
                implicitHeight: body.implicitHeight
                contentWidth: availableWidth
                clip: true
                TextArea
                {
                    id: body
                    objectName: "aiTraceDetailText"
                    text: root.entry.detail
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - (root.chat ? 0 : 2)
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    padding: 0
                    background: null
                }
            }
        }
        Text
        {
            text: root.chat ? root.entry.time : (root.entry.elapsedMs / 1000).toFixed(2) + " 秒"
            color: root.theme.textSecondary
            font.pixelSize: root.theme.fontSize - 3
        }
    }
}
