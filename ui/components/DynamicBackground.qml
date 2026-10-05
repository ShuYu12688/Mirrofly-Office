import QtQuick
import QtQuick.Window

Item
{
    id: root

    property var theme
    property bool animated: theme.backgroundMode === "dynamic" && theme.motionEnabled
    readonly property bool motionActive: root.animated && root.visible && root.enabled && root.theme.motionEnabled
        && root.Window.window !== null && root.Window.window.visible
        && root.Window.window.visibility !== Window.Hidden
        && root.Window.window.visibility !== Window.Minimized

    clip: true

    Rectangle
    {
        anchors.fill: parent
        color: root.theme.backgroundColor
    }

    GradientSurface
    {
        anchors.fill: parent
        theme: root.theme
        radius: 0
        opacity: root.theme.backgroundOpacity
        flowing: root.motionActive
    }

    Image
    {
        anchors.fill: parent
        source: root.theme.backgroundImage
        visible: source.toString().length > 0
        fillMode: Image.PreserveAspectCrop
        opacity: root.theme.backgroundOpacity
        asynchronous: true
    }
}
