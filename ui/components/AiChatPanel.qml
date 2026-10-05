pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle
{
    id: root
    objectName: "aiChatPanel"
    required property var theme
    required property var agent
    required property string location
    signal closeRequested()
    signal sendRequested(string prompt)
    color: theme.surfaceColor
    radius: theme.radius
    border.color: theme.borderColor
    border.width: 1

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    Timer
    {
        id: followTimer
        interval: root.theme.aiFollowDelay
        onTriggered: if (root.visible && conversation.followTail) conversation.positionViewAtEnd()
    }
    onVisibleChanged: if (visible && conversation.followTail) followTimer.restart()

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: root.theme.aiMargin
        spacing: root.theme.spacing

        RowLayout
        {
            Layout.fillWidth: true
            Text
            {
                Layout.fillWidth: true
                text: "AI 助手"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize + 2
                font.bold: true
            }
            ActionButton
            {
                theme: root.theme
                text: "收起"
                compact: true
                onClicked: root.closeRequested()
            }
        }
        Text
        {
            Layout.fillWidth: true
            text: "当前：" + root.location
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
            elide: Text.ElideMiddle
        }
        ListView
        {
            id: conversation
            objectName: "aiConversationList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            property bool followTail: true
            reuseItems: true
            spacing: root.theme.spacing
            model: root.agent.trace
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: if (root.visible && followTail) followTimer.restart()
            onContentHeightChanged: if (root.visible && followTail) followTimer.restart()
            onMovementStarted: followTail = false
            onMovementEnded: followTail = atYEnd
            delegate: AiTraceRow
            {
                required property var modelData
                theme: root.theme
                entry: modelData
                width: conversation.width - root.theme.spacing
                ListView.onReused: expanded = false
            }
        }
        ActionButton
        {
            Layout.alignment: Qt.AlignHCenter
            visible: !conversation.followTail
            theme: root.theme
            iconName: ""
            text: "回到最新进度"
            compact: true
            onClicked: { conversation.followTail = true; followTimer.restart(); }
        }
        ScrollView
        {
            Layout.fillWidth: true
            Layout.preferredHeight: root.agent.busy && root.agent.answer.length > 0 ? root.theme.aiInputHeight : 0
            visible: root.agent.busy && root.agent.answer.length > 0
            clip: true
            TextArea
            {
                text: root.agent.answer
                readOnly: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
            }
        }
        Rectangle
        {
            Layout.fillWidth: true
            implicitHeight: statusContent.implicitHeight + root.theme.spacing * 2
            radius: root.theme.radius * 0.6
            color: root.theme.accentSoft
            ColumnLayout
            {
                id: statusContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: root.theme.spacing
                spacing: root.theme.spacing / 2
                Text
                {
                    Layout.fillWidth: true
                    text: root.agent.status
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 1
                    wrapMode: Text.Wrap
                }
                Text
                {
                    Layout.fillWidth: true
                    text: root.agent.contextText
                    color: root.theme.textSecondary
                    font.pixelSize: root.theme.fontSize - 3
                }
                ToolButton
                {
                    id: usageToggle
                    text: checked ? "收起用量" : "查看用量"
                    checkable: true
                }
                Text
                {
                    Layout.fillWidth: true
                    visible: usageToggle.checked
                    text: root.agent.usageText
                    color: root.theme.textSecondary
                    font.pixelSize: root.theme.fontSize - 2
                    wrapMode: Text.Wrap
                }
            }
        }
        ScrollView
        {
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.aiInputHeight
            clip: true
            TextArea
            {
                id: prompt
                placeholderText: "在当前文档继续编辑，或描述要新建的内容…"
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                Accessible.name: "AI 对话输入"
                font.family: root.theme.fontFamily
            }
        }
        RowLayout
        {
            Layout.fillWidth: true
            Text
            {
                Layout.fillWidth: true
                text: root.agent.configured ? "执行详情按需展开" : "请先在设置中配置模型"
                color: root.theme.textSecondary
                font.pixelSize: root.theme.fontSize - 2
            }
            ActionButton
            {
                theme: root.theme
                text: "继续原任务"
                visible: root.agent.resumable && !root.agent.busy
                enabled: root.agent.configured
                onClicked: root.agent.resume()
            }
            ActionButton
            {
                theme: root.theme
                text: root.agent.busy ? "停止" : "发送"
                primary: true
                enabled: root.agent.busy || (root.agent.configured && prompt.text.trim().length > 0)
                onClicked:
                {
                    if (root.agent.busy) root.agent.cancel();
                    else
                    {
                        root.sendRequested(prompt.text);
                        if (root.agent.busy) prompt.clear();
                    }
                }
            }
        }
    }
}
