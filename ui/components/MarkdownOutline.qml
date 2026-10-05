pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle
{
    id: root

    property var theme
    property var entries: []
    property int currentPosition: 0
    signal positionRequested(int position)

    radius: theme.radius
    color: theme.surfaceColor
    opacity: theme.surfaceOpacity
    border.width: 1
    border.color: theme.borderColor

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 17
        spacing: 15

        Text
        {
            text: "文档大纲"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize + 1
            font.weight: Font.DemiBold
        }

        Text
        {
            Layout.fillWidth: true
            visible: root.entries.length === 0
            text: "设置标题后，就能在这里快速跳转。"
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
            wrapMode: Text.Wrap
        }

        ListView
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.entries
            spacing: 5
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar
            {
                policy: ScrollBar.AsNeeded
            }

            delegate: AbstractButton
            {
                id: heading

                required property var modelData
                required property int index
                readonly property bool selected: root.currentPosition >= modelData.position
                    && (index + 1 >= root.entries.length || root.currentPosition < root.entries[index + 1].position)

                width: ListView.view.width
                height: Math.max(35, headingText.implicitHeight + 16)
                leftPadding: 10 + (modelData.level - 1) * 9
                rightPadding: 8
                topPadding: 8
                bottomPadding: 8
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: modelData.title
                onClicked: root.positionRequested(modelData.position)

                background: Rectangle
                {
                    radius: root.theme.radius * 0.45
                    color: heading.hovered || heading.down ? root.theme.accent
                        : (heading.selected ? root.theme.accentSoft : root.theme.transparentColor)
                    border.width: heading.visualFocus ? 1 : 0
                    border.color: root.theme.accent

                    Behavior on color
                    {
                        ColorAnimation
                        {
                            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                        }
                    }
                }

                contentItem: Text
                {
                    id: headingText

                    text: heading.modelData.title.length > 0 ? heading.modelData.title : "未命名标题"
                    color: heading.hovered || heading.down ? root.theme.onAccent
                        : (heading.selected ? root.theme.accent : root.theme.textSecondary)
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                    font.weight: heading.selected ? Font.DemiBold : Font.Normal
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
