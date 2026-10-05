import QtQuick
import QtQuick.Window

Rectangle
{
    id: root

    property var theme
    property bool saturated: false
    property bool highlighted: false
    property bool flowing: false
    readonly property bool motionActive: root.flowing && root.visible && root.enabled
        && root.theme.motionEnabled && root.Window.window !== null
        && root.Window.window.visible && root.Window.window.visibility !== Window.Hidden
        && root.Window.window.visibility !== Window.Minimized
    readonly property int flowDuration: Math.max(1600, root.theme.sheenDuration)
    property real lightPosition: 0.2

    radius: theme.radius
    color: theme.surfaceColor
    opacity: theme.surfaceOpacity
    clip: false

    gradient: Gradient
    {
        orientation: Gradient.Horizontal

        GradientStop
        {
            position: 0
            color: root.theme.gradientSoftStart
        }

        GradientStop
        {
            position: 0.52
            color: root.theme.gradientSoftMiddle
        }

        GradientStop
        {
            position: 1
            color: root.theme.gradientSoftEnd
        }
    }

    Rectangle
    {
        anchors.fill: parent
        radius: root.radius
        opacity: root.saturated ? 1 : 0

        gradient: Gradient
        {
            orientation: Gradient.Horizontal

            GradientStop
            {
                position: 0
                color: root.theme.gradientStart
            }

            GradientStop
            {
                position: 0.52
                color: root.theme.gradientMiddle
            }

            GradientStop
            {
                position: 1
                color: root.theme.gradientEnd
            }
        }

        Behavior on opacity
        {
            NumberAnimation
            {
                duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle
    {
        anchors.fill: parent
        radius: root.radius
        opacity: root.flowing ? root.theme.sheenOpacity : 0

        gradient: Gradient
        {
            orientation: Gradient.Horizontal

            GradientStop
            {
                position: 0
                color: root.theme.transparentColor
            }

            GradientStop
            {
                position: Math.max(0, root.lightPosition - 0.2)
                color: root.theme.transparentColor
            }

            GradientStop
            {
                position: root.lightPosition
                color: root.theme.sheenColor
            }

            GradientStop
            {
                position: Math.min(1, root.lightPosition + 0.2)
                color: root.theme.transparentColor
            }

            GradientStop
            {
                position: 1
                color: root.theme.transparentColor
            }
        }
    }

    SpectrumStroke
    {
        anchors.fill: parent
        theme: root.theme
        radius: root.radius
        opacity: root.highlighted || root.flowing ? 1 : 0

        Behavior on opacity
        {
            NumberAnimation { duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0 }
        }
    }

    SequentialAnimation
    {
        running: root.motionActive
        loops: Animation.Infinite

        NumberAnimation
        {
            target: root
            property: "lightPosition"
            from: 0.2
            to: 0.8
            duration: root.flowDuration / 2
            easing.type: Easing.InOutSine
        }

        NumberAnimation
        {
            target: root
            property: "lightPosition"
            from: 0.8
            to: 0.2
            duration: root.flowDuration / 2
            easing.type: Easing.InOutSine
        }
    }
}
