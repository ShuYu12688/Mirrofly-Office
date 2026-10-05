import QtQuick

Item
{
    id: root
    required property var theme
    property string heading: ""
    property string subtitle: ""
    property bool ribbonCycling: false
    implicitHeight: theme.workspaceHeaderHeight
    WorkspaceRibbon
    {
        anchors.right: parent.right
        anchors.top: parent.top
        width: Math.min(parent.width * 0.76, root.theme.workspaceRibbonMaxWidth)
        height: parent.height
        theme: root.theme
        cycling: root.ribbonCycling
    }
    Column
    {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width * 0.42
        spacing: 9
        Text
        {
            width: parent.width
            text: root.heading
            color: root.theme.workspaceInk
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.workspaceHeadingSize
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text
        {
            width: parent.width
            text: root.subtitle
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize
            elide: Text.ElideRight
        }
    }
}
