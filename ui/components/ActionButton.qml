import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AbstractButton
{
    id: root

    property var theme
    property string iconName: "folder"
    property bool primary: false
    property bool compact: false
    readonly property bool filled: primary

    implicitWidth: contentItem.implicitWidth + (compact ? 24 : 32)
    implicitHeight: compact ? 34 : 42
    hoverEnabled: true
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.5
    scale: down && theme.motionEnabled ? 0.985 : 1

    Behavior on scale
    {
        NumberAnimation
        {
            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
            easing.type: Easing.OutCubic
        }
    }

    HoverHandler
    {
        cursorShape: Qt.PointingHandCursor
    }

    background: GradientSurface
    {
        theme: root.theme
        radius: root.theme.radius * 0.58
        saturated: root.primary
        highlighted: root.hovered || root.down
        flowing: root.hovered

        Rectangle
        {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: root.theme.transparentColor
            border.width: 2
            border.color: root.theme.accent
            visible: root.visualFocus
        }
    }

    contentItem: RowLayout
    {
        spacing: 8

        UiIcon
        {
            visible: root.iconName.length > 0
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            symbol: root.iconName
            iconColor: root.filled ? root.theme.onAccent : root.theme.textSecondary

            Behavior on iconColor
            {
                ColorAnimation
                {
                    duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                }
            }
        }

        Text
        {
            Layout.fillWidth: true
            text: root.text
            elide: Text.ElideRight
            color: root.filled ? root.theme.onAccent : root.theme.textPrimary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - (root.compact ? 2 : 1)
            font.weight: Font.Medium

            Behavior on color
            {
                ColorAnimation
                {
                    duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                }
            }
        }
    }

    leftPadding: compact ? 12 : 16
    rightPadding: compact ? 12 : 16
}
