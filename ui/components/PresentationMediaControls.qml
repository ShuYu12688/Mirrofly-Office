pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item
{
    id: root
    property var theme
    property var items: []
    property var states: ({})
    property real sceneScale: 1
    property bool fullscreen: false
    signal commandRequested(int shape, string action, real value)

    Repeater
    {
        model: root.items
        delegate: Item
        {
            id: media
            required property var modelData
            readonly property var status: root.states[String(modelData.index)] || ({playing: false, position: 0, duration: 0, volume: 1, loop: false, error: ""})
            readonly property var matrix: modelData.transform
            width: modelData.width * root.sceneScale
            height: modelData.height * root.sceneScale
            transform: Matrix4x4
            {
                matrix: Qt.matrix4x4(media.matrix[0], media.matrix[2], 0, media.matrix[4] * root.sceneScale,
                    media.matrix[1], media.matrix[3], 0, media.matrix[5] * root.sceneScale,
                    0, 0, 1, 0, 0, 0, 0, 1)
            }
            HoverHandler { id: hover }
            TapHandler
            {
                enabled: root.fullscreen
                onTapped: root.commandRequested(media.modelData.index, media.status.playing ? "pause" : "play", 0)
            }
            Rectangle
            {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: controls.implicitHeight + 8
                color: root.theme.surfaceColor
                border.color: root.theme.borderColor
                radius: 4
                opacity: 0.95
                visible: !root.fullscreen || hover.hovered || !media.status.playing
                ColumnLayout
                {
                    id: controls
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 4
                    spacing: 2
                    RowLayout
                    {
                        Layout.fillWidth: true
                        ActionButton
                        {
                            theme: root.theme
                            text: media.status.playing ? "暂停" : "播放"
                            compact: true
                            onClicked: root.commandRequested(media.modelData.index, media.status.playing ? "pause" : "play", 0)
                        }
                        ActionButton
                        {
                            theme: root.theme; text: "停止"; compact: true
                            visible: media.width >= 220
                            onClicked: root.commandRequested(media.modelData.index, "stop", 0)
                        }
                        Slider
                        {
                            Layout.fillWidth: true
                            from: 0; to: Math.max(1, media.status.duration)
                            value: media.status.position
                            enabled: media.status.duration > 0
                            Accessible.name: "播放位置"
                            onMoved: root.commandRequested(media.modelData.index, "seek", value)
                        }
                        ToolButton
                        {
                            text: media.status.volume === 0 ? "静音" : "声音"
                            visible: media.width >= 280
                            onClicked: root.commandRequested(media.modelData.index, "volume", media.status.volume === 0 ? 1 : 0)
                        }
                        ToolButton
                        {
                            text: "循环"; checkable: true; checked: Boolean(media.status.loop)
                            visible: media.width >= 360
                            onClicked: root.commandRequested(media.modelData.index, "loop", checked ? 1 : 0)
                        }
                    }
                    Text
                    {
                        Layout.fillWidth: true
                        visible: Boolean(media.status.error)
                        text: media.status.error || ""
                        color: root.theme.textPrimary
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
