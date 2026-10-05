pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window
{
    id: root
    required property var screenGeometry
    required property var theme
    required property bool glowing
    property real breath: 1
    readonly property bool motionActive: glowing && visible && theme.motionEnabled
        && visibility !== Window.Minimized && visibility !== Window.Hidden
    readonly property int depth: Math.max(24,
        Math.min(theme.islandGlowDepth, Math.min(width, height) / 3))

    function glowAlpha(distance)
    {
        const d = Math.max(0, Math.min(1, distance));
        return root.theme.islandGlowOpacity * Math.exp(-root.theme.islandGlowFalloff * d * d)
            * (1 - d * d * (3 - 2 * d));
    }

    function rgba(color, alpha)
    {
        const value = String(color);
        const red = parseInt(value.slice(1, 3), 16);
        const green = parseInt(value.slice(3, 5), 16);
        const blue = parseInt(value.slice(5, 7), 16);
        return "rgba(" + red + "," + green + "," + blue + "," + alpha + ")";
    }

    function paintEdge(canvas, startColor, middleColor, endColor, horizontal, reverse)
    {
        const context = canvas.getContext("2d");
        context.clearRect(0, 0, canvas.width, canvas.height);
        context.globalCompositeOperation = "source-over";

        const rainbow = context.createLinearGradient(0, 0,
            horizontal ? canvas.width : 0, horizontal ? 0 : canvas.height);
        rainbow.addColorStop(0, startColor);
        rainbow.addColorStop(0.5, middleColor);
        rainbow.addColorStop(1, endColor);
        context.fillStyle = rainbow;
        context.fillRect(0, 0, canvas.width, canvas.height);

        context.globalCompositeOperation = "destination-in";
        const fade = context.createLinearGradient(
            horizontal ? 0 : (reverse ? canvas.width : 0),
            horizontal ? (reverse ? canvas.height : 0) : 0,
            horizontal ? 0 : (reverse ? 0 : canvas.width),
            horizontal ? (reverse ? 0 : canvas.height) : 0);
        for (let step = 0; step <= 32; ++step)
            fade.addColorStop(step / 32, "rgba(255,255,255," + root.glowAlpha(step / 32) + ")");
        context.fillStyle = fade;
        context.fillRect(0, 0, canvas.width, canvas.height);
        context.globalCompositeOperation = "source-over";
    }

    function paintCorner(canvas, color, corner)
    {
        const context = canvas.getContext("2d");
        const radius = canvas.width;
        const centerX = corner === 0 || corner === 3 ? radius : 0;
        const centerY = corner === 0 || corner === 1 ? radius : 0;
        context.clearRect(0, 0, radius, radius);

        const fade = context.createRadialGradient(centerX, centerY, 0,
            centerX, centerY, radius);
        for (let step = 0; step <= 32; ++step)
            fade.addColorStop(step / 32, root.rgba(color, root.glowAlpha(1 - step / 32)));
        context.fillStyle = fade;
        context.fillRect(0, 0, radius, radius);
    }

    x: screenGeometry.x
    y: screenGeometry.y
    width: screenGeometry.width
    height: screenGeometry.height
    objectName: "aiScreenGlow"
    transientParent: null
    visible: glowing
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus

    SequentialAnimation
    {
        running: root.motionActive
        loops: Animation.Infinite
        NumberAnimation
        {
            target: root
            property: "breath"
            from: 1
            to: root.theme.islandGlowBreathMinimum || 0.92
            duration: (root.theme.islandGlowBreathPeriod || 4200) / 2
            easing.type: Easing.InOutSine
        }
        NumberAnimation
        {
            target: root
            property: "breath"
            from: root.theme.islandGlowBreathMinimum || 0.92
            to: 1
            duration: (root.theme.islandGlowBreathPeriod || 4200) / 2
            easing.type: Easing.InOutSine
        }
    }

    Item
    {
        anchors.fill: parent
        opacity: root.glowing ? 1 : 0
        Behavior on opacity
        {
            NumberAnimation
            {
                duration: root.theme.motionEnabled ? root.theme.disclosureDuration : 0
                easing.type: Easing.OutCubic
            }
        }

        Item
        {
            anchors.fill: parent
            opacity: root.motionActive ? root.breath : 1
            Canvas
            {
                x: root.depth
                y: 0
                width: root.width - root.depth * 2
                height: root.depth
                onPaint: root.paintEdge(this, root.theme.islandBlue, root.theme.islandPink,
                    root.theme.islandPurple, true, false)
            }
            Canvas
            {
                x: root.depth
                y: root.height - root.depth
                width: root.width - root.depth * 2
                height: root.depth
                onPaint: root.paintEdge(this, root.theme.islandAmber, root.theme.islandBlue,
                    root.theme.islandPink, true, true)
            }
            Canvas
            {
                x: 0
                y: root.depth
                width: root.depth
                height: root.height - root.depth * 2
                onPaint: root.paintEdge(this, root.theme.islandBlue, root.theme.islandPurple,
                    root.theme.islandAmber, false, false)
            }
            Canvas
            {
                x: root.width - root.depth
                y: root.depth
                width: root.depth
                height: root.height - root.depth * 2
                onPaint: root.paintEdge(this, root.theme.islandPurple, root.theme.islandBlue,
                    root.theme.islandPink, false, true)
            }
            Canvas
            {
                x: 0
                y: 0
                width: root.depth
                height: root.depth
                onPaint: root.paintCorner(this, root.theme.islandBlue, 0)
            }
            Canvas
            {
                x: root.width - root.depth
                y: 0
                width: root.depth
                height: root.depth
                onPaint: root.paintCorner(this, root.theme.islandPurple, 1)
            }
            Canvas
            {
                x: root.width - root.depth
                y: root.height - root.depth
                width: root.depth
                height: root.depth
                onPaint: root.paintCorner(this, root.theme.islandPink, 2)
            }
            Canvas
            {
                x: 0
                y: root.height - root.depth
                width: root.depth
                height: root.depth
                onPaint: root.paintCorner(this, root.theme.islandAmber, 3)
            }
        }
    }
}
