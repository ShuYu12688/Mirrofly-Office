pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle
{
    id: root
    required property var theme
    required property var agent
    property bool compactMode: false
    property bool nativeDragging: false
    signal closeRequested()
    signal dragPressed()
    signal dragStarted()
    signal dragEnded()
    signal editRequested()
    signal dragMoved(real deltaX, real deltaY)

    function focusPrompt()
    {
        prompt.forceActiveFocus();
    }

    function submitPrompt()
    {
        const text = prompt.text.trim();
        if (!root.agent.configured || root.agent.busy || text.length === 0)
            return;
        if (root.agent.sendPrompt(text)) prompt.clear();
    }

    Connections
    {
        target: typeof root.agent.sendPrompt === "function" ? root.agent : null
        function onPromptRejected(text)
        {
            prompt.text = prompt.text.length === 0 ? text : text + "\n" + prompt.text;
        }
    }

    radius: theme.islandPanelRadius
    color: theme.islandBorder
    gradient: Gradient
    {
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: root.theme.islandBlue }
        GradientStop { position: 0.35; color: root.theme.islandPurple }
        GradientStop { position: 0.7; color: root.theme.islandPink }
        GradientStop { position: 1; color: root.theme.islandBlue }
    }

    Rectangle
    {
        anchors.fill: parent
        anchors.margins: 1
        radius: root.theme.islandPanelRadius - 1
        color: root.theme.islandSurface

        ColumnLayout
        {
            anchors.fill: parent
            anchors.margins: root.compactMode ? root.theme.islandCompactPadding : root.theme.islandPanelPadding
            spacing: 12

            Item
            {
                Layout.fillWidth: true
                Layout.preferredHeight: root.compactMode ? 28 : 38

                RowLayout
                {
                    anchors.left: parent.left
                    anchors.right: effortBox.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 12
                    spacing: 12
                    Image
                    {
                        Layout.preferredWidth: root.compactMode ? 28 : 34
                        Layout.preferredHeight: root.compactMode ? 28 : 34
                        source: "../assets/mirrorfly-mark-light.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                    ColumnLayout
                    {
                        Layout.fillWidth: true
                        spacing: 2
                        Text
                        {
                            Layout.fillWidth: true
                            text: "Mirrorfly AI"
                            elide: Text.ElideRight
                            color: root.theme.islandText
                            font.family: root.theme.fontFamily
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        Text
                        {
                            Layout.fillWidth: true
                            visible: !root.compactMode
                            text: root.agent.location + " · 拖动此处移动"
                            color: root.theme.islandMuted
                            font.family: root.theme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }
                }
                AiIslandEffortControl
                {
                    id: effortBox
                    objectName: "aiIslandEffort"
                    theme: root.theme
                    anchors.right: closeButton.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.theme.islandEffortWidth
                    height: 30
                    effort: root.agent.effortPreview || root.agent.thinkingEffort || "none"
                    effortPending: root.agent.effortPending || false
                    enabled: root.agent.configured && !root.agent.busy
                    onEffortRequested: function(value) { root.agent.setThinkingEffort(value); }
                }
                AiIslandDragArea
                {
                    objectName: "aiIslandHeaderDrag"
                    threshold: root.theme.islandDragThreshold
                    anchors.left: parent.left
                    anchors.right: effortBox.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    nativeMove: root.nativeDragging
                    onDragStarted: root.dragStarted()
                    onDragEnded: root.dragEnded()
                    onTapped: if (root.compactMode) root.editRequested()
                    onDragPressed: root.dragPressed()
                    onDragMoved: function(deltaX, deltaY) { root.dragMoved(deltaX, deltaY); }
                }
                Rectangle
                {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    radius: 14
                    color: closeMouse.containsMouse ? root.theme.islandHover : "transparent"
                    Text
                    {
                        anchors.centerIn: parent
                        text: "×"
                        color: root.theme.islandMuted
                        font.pixelSize: 20
                    }
                    MouseArea
                    {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeRequested()
                    }
                }
            }

            Rectangle
            {
                Layout.fillWidth: true
                visible: !root.compactMode
                Layout.preferredHeight: 1
                color: root.theme.islandDivider
            }

            Text
            {
                Layout.fillWidth: true
                objectName: "aiIslandStatusLine"
                ToolTip.visible: statusHover.hovered && !root.agent.busy
                    && String(root.agent.statusDetail || "").length > 0
                ToolTip.text: root.agent.statusDetail || ""
                ToolTip.delay: 500
                HoverHandler { id: statusHover }

                text: root.agent.status
                color: root.agent.busy ? root.theme.islandBlue : root.theme.islandMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 1
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }

            Rectangle
            {
                Layout.fillWidth: true
                visible: !root.compactMode
                Layout.preferredHeight: root.theme.islandInputHeight
                radius: root.theme.islandInputRadius
                color: root.theme.islandInput
                border.width: 1
                border.color: prompt.activeFocus ? root.theme.islandBlue : root.theme.islandDivider
                Behavior on border.color
                {
                    ColorAnimation { duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0 }
                }

                ScrollView
                {
                    id: editor
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: inputTools.top
                    anchors.margins: 14
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea
                    {
                        id: prompt
                        objectName: "aiIslandPrompt"
                        width: editor.availableWidth
                        padding: 0
                        placeholderText: "向 Mirrorfly AI 描述你的想法…"
                        placeholderTextColor: root.theme.islandMuted
                        color: root.theme.islandText
                        selectionColor: root.theme.islandPurple
                        background: null
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        font.family: root.theme.fontFamily
                        font.pixelSize: 15
                        Accessible.name: "AI 对话输入"
                        Keys.onReturnPressed: function(event)
                        {
                            if (prompt.inputMethodComposing || event.modifiers & Qt.ShiftModifier)
                            {
                                event.accepted = false;
                                return;
                            }
                            root.submitPrompt();
                            event.accepted = true;
                        }
                        Keys.onEnterPressed: function(event)
                        {
                            if (prompt.inputMethodComposing || event.modifiers & Qt.ShiftModifier)
                            {
                                event.accepted = false;
                                return;
                            }
                            root.submitPrompt();
                            event.accepted = true;
                        }
                    }
                }

                Item
                {
                    id: inputTools
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    height: 34
                    Text
                    {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "描述任务，或继续提问"
                        color: root.theme.islandMuted
                        font.family: root.theme.fontFamily
                        font.pixelSize: 11
                    }
                    Rectangle
                    {
                        id: sendButton
                        anchors.right: parent.right
                        width: 34
                        height: 34
                        radius: 11
                        readonly property bool available: root.agent.busy
                            || (root.agent.configured && prompt.text.trim().length > 0)
                        color: available ? root.theme.islandBlue : root.theme.islandHover
                        Text
                        {
                            anchors.centerIn: parent
                            text: root.agent.busy ? "■" : "↑"
                            color: sendButton.available ? root.theme.islandButtonText : root.theme.islandMuted
                            font.pixelSize: root.agent.busy ? 13 : 22
                            font.weight: Font.DemiBold
                        }
                        MouseArea
                        {
                            anchors.fill: parent
                            enabled: sendButton.available
                            cursorShape: Qt.PointingHandCursor
                            onClicked:
                            {
                                if (root.agent.busy)
                                    root.agent.cancel();
                                else
                                    root.submitPrompt();
                            }
                        }
                    }
                }
            }

            RowLayout
            {
                visible: !root.compactMode
                Layout.fillWidth: true
                Text
                {
                    Layout.fillWidth: true
                    text: "Enter 发送 · Shift+Enter 换行"
                    color: root.theme.islandMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text
                {
                    visible: root.agent.resumable && !root.agent.busy
                    text: "继续任务"
                    color: root.theme.islandBlue
                    font.pixelSize: 12
                    MouseArea
                    {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.agent.resume()
                    }
                }

            }
        }
    }
}
