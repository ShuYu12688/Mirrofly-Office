import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item
{
    id: root

    default property alias content: bodyColumn.data
    property var theme
    property string title
    property string description
    property string iconName
    property bool expanded: false

    signal toggled()

    implicitWidth: 880
    implicitHeight: header.height + bodyViewport.height

    Rectangle
    {
        anchors.fill: parent
        radius: root.theme.radius
        color: root.theme.surfaceColor
        opacity: root.theme.surfaceOpacity
        border.width: 1
        border.color: root.theme.borderColor
    }

    AbstractButton
    {
        id: header

        readonly property bool filled: false

        width: parent.width
        height: Math.max(100, headerContent.implicitHeight + 40)
        leftPadding: 25
        rightPadding: 25
        topPadding: 20
        bottomPadding: 20
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.name: root.title + "，" + root.description
        Accessible.description: root.expanded ? "已展开，点击收起" : "已收起，点击展开"
        onClicked: root.toggled()

        HoverHandler
        {
            cursorShape: Qt.PointingHandCursor
        }

        background: GradientSurface
        {
            theme: root.theme
            anchors.fill: parent
            anchors.margins: 2
            radius: Math.max(0, root.theme.radius - 2)
            highlighted: header.hovered || header.down
            flowing: header.hovered

            Rectangle
            {
                anchors.fill: parent
                anchors.margins: -3
                radius: parent.radius + 3
                color: root.theme.transparentColor
                border.width: 2
                border.color: root.theme.accent
                visible: header.visualFocus
            }
        }

        contentItem: RowLayout
        {
            id: headerContent

            spacing: 18

            Item
            {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44

                Rectangle
                {
                    anchors.fill: parent
                    radius: root.theme.radius * 0.72
                    color: header.filled ? root.theme.onAccent : root.theme.surfaceColor
                    opacity: header.filled ? 0.18 : 0.75

                    Behavior on opacity
                    {
                        NumberAnimation
                        {
                            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                        }
                    }
                }

                UiIcon
                {
                    anchors.centerIn: parent
                    width: 23
                    height: 23
                    symbol: root.iconName
                    iconColor: header.filled ? root.theme.onAccent : root.theme.accent

                    Behavior on iconColor
                    {
                        ColorAnimation
                        {
                            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                        }
                    }
                }
            }

            ColumnLayout
            {
                Layout.fillWidth: true
                spacing: 7

                Text
                {
                    Layout.fillWidth: true
                    text: root.title
                    color: header.filled ? root.theme.onAccent : root.theme.textPrimary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize + 2
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap

                    Behavior on color
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
                    text: root.description
                    color: header.filled ? root.theme.onAccentMuted : root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                    wrapMode: Text.Wrap

                    Behavior on color
                    {
                        ColorAnimation
                        {
                            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                        }
                    }
                }
            }

            Text
            {
                text: root.expanded ? "收起" : "展开"
                color: header.filled ? root.theme.onAccent : root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2

                Behavior on color
                {
                    ColorAnimation
                    {
                        duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                    }
                }
            }

            UiIcon
            {
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
                symbol: "arrow"
                iconColor: header.filled ? root.theme.onAccent : root.theme.textSecondary
                rotation: root.expanded ? 90 : 0

                Behavior on iconColor
                {
                    ColorAnimation
                    {
                        duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                    }
                }

                Behavior on rotation
                {
                    NumberAnimation
                    {
                        duration: root.theme.motionEnabled ? root.theme.disclosureDuration : 0
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    Item
    {
        id: bodyViewport

        y: header.height
        width: parent.width
        height: root.expanded ? bodyColumn.implicitHeight + 25 : 0
        clip: true
        visible: height > 0
        enabled: root.expanded

        Behavior on height
        {
            NumberAnimation
            {
                duration: root.theme.motionEnabled ? root.theme.disclosureDuration : 0
                easing.type: Easing.OutCubic
            }
        }

        Column
        {
            id: bodyColumn

            x: 25
            width: parent.width - 50
            spacing: root.theme.spacing * 2
            opacity: root.expanded ? root.theme.surfaceOpacity : 0

            Behavior on opacity
            {
                NumberAnimation
                {
                    duration: root.theme.motionEnabled ? root.theme.disclosureDuration : 0
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
