import QtQuick
import QtQuick.Window
import QtQuick.Effects

Rectangle
{
    id: root
    required property var theme
    property bool frosted: true
    property color paperColor: theme.surfaceColor
    property Item backdrop: null
    property point backdropOrigin: Qt.point(0, 0)
    readonly property bool frostActive: frosted && backdrop !== null && visible
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized
    radius: theme.workspaceRadius
    color: paperColor

    Rectangle
    {
        id: frostMask
        width: root.width
        height: root.height
        radius: root.radius
        color: root.theme.whiteColor
        visible: false
        layer.enabled: root.frostActive
    }
    ShaderEffectSource
    {
        id: backgroundSample
        sourceItem: root.frostActive ? root.backdrop : null
        sourceRect: Qt.rect(root.backdropOrigin.x + root.x, root.backdropOrigin.y + root.y,
            root.width, root.height)
        textureSize: Qt.size(Math.max(1, root.width / 2), Math.max(1, root.height / 2))
        live: root.frostActive
        smooth: true
        visible: false
    }
    MultiEffect
    {
        anchors.fill: parent
        visible: root.frostActive
        source: backgroundSample
        blurEnabled: root.frostActive
        blur: root.theme.workspaceGlassBlur
        blurMax: 16
        autoPaddingEnabled: false
        maskEnabled: root.frostActive
        maskSource: frostMask
    }
    Rectangle
    {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient
        {
            GradientStop { position: 0; color: root.frosted ? root.theme.workspaceGlassTop : root.paperColor }
            GradientStop { position: 1; color: root.frosted ? root.theme.workspaceGlassBottom : root.paperColor }
        }
        border.width: 1
        border.color: root.frosted ? root.theme.workspaceGlassBorder : root.theme.homeRule
    }
    Rectangle
    {
        anchors.fill: parent
        anchors.margins: 2
        radius: root.radius - 2
        color: root.theme.transparentColor
        border.width: 1
        border.color: root.theme.whiteColor
        opacity: root.frosted ? 0.5 : 0
    }
}