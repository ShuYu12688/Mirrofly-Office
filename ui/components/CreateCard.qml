import QtQuick
import QtQuick.Controls

AbstractButton
{
    id: root
    required property var theme
    property string subtitle: ""
    property string kind: "word"
    property string formatLabel: ""
    property bool horizontal: false
    property color tileColor: theme["create" + kind + "Color"]
    property color ink: kind === "word" ? theme.whiteColor : theme.workspaceInk
    readonly property bool showHint: hovered || visualFocus
    property real hoverProgress: showHint ? 1 : 0
    transform: Translate { y: -root.theme.workspaceHoverLift * root.hoverProgress }
    Behavior on hoverProgress
    {
        NumberAnimation
        {
            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
            easing.type: Easing.OutCubic
        }
    }
    implicitHeight: 180
    implicitWidth: 240
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.description: subtitle
    padding: theme.workspaceTilePadding
    scale: down ? 0.99 : (showHint ? theme.workspaceHoverScale : 1)
    z: showHint ? 2 : 0
    Behavior on scale
    {
        NumberAnimation
        {
            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
            easing.type: Easing.OutCubic
        }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    background: Rectangle
    {
        radius: root.theme.workspaceRadius
        color: root.tileColor
        SpectrumStroke
        {
            anchors.fill: parent
            anchors.margins: -4
            theme: root.theme
            radius: parent.radius + 4
            lineWidth: 2.2
            opacity: root.showHint ? 1 : 0
            Behavior on opacity
            {
                NumberAnimation { duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0 }
            }
        }
    }
    contentItem: Item
    {
        Text
        {
            id: title
            width: parent.width - (root.horizontal ? 100 : 0)
            text: root.text
            color: root.ink
            font.family: root.theme.fontFamily
            font.pixelSize: root.horizontal ? root.theme.fontSize + 4 : root.theme.workspaceTileTitleSize
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text
        {
            anchors.top: title.bottom
            anchors.topMargin: 8
            width: root.horizontal ? parent.width - 100 : parent.width
            text: root.showHint ? root.subtitle : root.formatLabel
            color: root.ink
            opacity: 0.8
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        CreativeIcon
        {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: root.horizontal ? 48 : 10
            anchors.bottomMargin: root.horizontal ? 0 : 10
            width: root.horizontal ? 42 : Math.min(80, parent.height * 0.42)
            height: width
            kind: root.kind
            ink: root.ink
            opacity: root.kind === "word" ? 0.94 : 0.64 + root.hoverProgress * 0.22
            rotation: -3 * root.hoverProgress
            transform: Translate { y: -3 * root.hoverProgress }
        }
        Rectangle
        {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 30
            height: 30
            radius: 15
            color: root.theme.whiteColor
            opacity: 0.65 + root.hoverProgress * 0.3
            transform: Translate { x: 3 * root.hoverProgress; y: -2 * root.hoverProgress }
            UiIcon
            {
                anchors.centerIn: parent
                width: 17
                height: 17
                symbol: "arrow"
                iconColor: root.theme.workspaceInk
            }
        }
    }
}