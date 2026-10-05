import QtQuick
import "components"

FocusScope
{
    id: root

    property var theme
    property bool ready: false
    property Item destinationMark: null
    property real ringProgress: 0
    property real ringScale: 1
    property real readyOpacity: 0
    property real travelProgress: 0
    property real backgroundOpacity: 1
    property real controlsOpacity: 0
    property bool finished: false
    property string phase: "loading"
    property real destinationX: 65
    property real destinationY: 69
    readonly property real targetScale: destinationMark ? destinationMark.width / 132 : 0.28

    visible: !finished
    enabled: visible
    focus: visible
    Keys.onPressed: function(event) { event.accepted = true; }
    Keys.onReleased: function(event) { event.accepted = true; }

    function updateDestination()
    {
        if (!destinationMark) return;
        const point = destinationMark.mapToItem(root,
            destinationMark.width / 2, destinationMark.height / 2);
        destinationX = point.x;
        destinationY = point.y;
    }

    function reset()
    {
        reveal.stop();
        phase = "loading";
        finished = false;
        ringProgress = 0;
        ringScale = 1;
        readyOpacity = 0;
        travelProgress = 0;
        backgroundOpacity = 1;
        controlsOpacity = 0;
        updateDestination();
    }

    function finishWhenReady()
    {
        if (!ready || phase !== "loading") return;
        updateDestination();
        if (!theme.motionEnabled)
        {
            ringProgress = 1;
            ringScale = 0.91;
            travelProgress = 1;
            backgroundOpacity = 0;
            controlsOpacity = 1;
            phase = "complete";
            finished = true;
            return;
        }
        phase = "ring";
        reveal.start();
    }

    onReadyChanged:
    {
        if (!ready) reset();
        else Qt.callLater(finishWhenReady);
    }
    onWidthChanged: updateDestination()
    onHeightChanged: updateDestination()
    Component.onCompleted:
    {
        updateDestination();
        if (ready) Qt.callLater(finishWhenReady);
    }

    Rectangle
    {
        anchors.fill: parent
        color: root.theme.startupBackground
        opacity: root.backgroundOpacity
    }

    MouseArea
    {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true; }
    }

    Item
    {
        id: emblem
        objectName: "startupEmblem"
        width: 224
        height: 224
        x: root.width / 2 - width / 2 + root.travelProgress * (root.destinationX - root.width / 2)
        y: root.height / 2 - height / 2 + root.travelProgress * (root.destinationY - root.height / 2)
        scale: 1 + root.travelProgress * (root.targetScale - 1)

        Canvas
        {
            id: orbit
            objectName: "startupOrbit"
            anchors.fill: parent
            scale: root.ringScale
            opacity: 1 - root.travelProgress
            onPaint:
            {
                const context = getContext("2d");
                context.clearRect(0, 0, width, height);
                context.lineCap = "round";
                context.lineWidth = 2.5;
                const cx = width / 2;
                const cy = height / 2;
                const radius = Math.min(width, height) / 2 - 5;
                for (let index = 0; index < 48; ++index)
                {
                    const begin = -Math.PI / 2 + index * Math.PI / 24;
                    context.beginPath();
                    context.arc(cx, cy, radius, begin, begin + Math.PI / 46);
                    context.strokeStyle = root.theme.startupRingFaint;
                    context.stroke();
                }
                if (root.ringProgress > 0)
                {
                    context.beginPath();
                    context.arc(cx, cy, radius, -Math.PI / 2,
                        -Math.PI / 2 + root.ringProgress * Math.PI * 2);
                    context.strokeStyle = root.theme.startupRingBright;
                    context.stroke();
                }
            }
            Connections
            {
                target: root
                function onRingProgressChanged() { orbit.requestPaint(); }
                function onThemeChanged() { orbit.requestPaint(); }
            }
        }

        LoadingEmblem
        {
            anchors.centerIn: parent
            width: 132
            height: 132
            theme: root.theme
            homeOpacity: 1 - root.backgroundOpacity
        }

        Text
        {
            objectName: "startupReadyText"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: 16
            text: "就绪"
            color: root.theme.startupReadyText
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize + 2
            font.weight: Font.Medium
            opacity: root.readyOpacity * Math.max(0, 1 - root.travelProgress * 2)
        }
    }

    Text
    {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        text: "开发者：镜蝶科技 研发部 舒宇"
        color: root.theme.startupCreditText
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        opacity: root.backgroundOpacity * (1 - root.travelProgress)
    }

    SequentialAnimation
    {
        id: reveal
        ScriptAction { script: root.phase = "ring"; }
        NumberAnimation
        {
            target: root
            property: "ringProgress"
            to: 1
            duration: root.theme.startupRingDuration
            easing.type: Easing.OutCubic
        }
        ScriptAction { script: root.phase = "seal"; }
        NumberAnimation
        {
            target: root
            property: "ringScale"
            to: 0.91
            duration: root.theme.startupSealDuration
            easing.type: Easing.OutCubic
        }
        ScriptAction { script: root.phase = "ready"; }
        NumberAnimation
        {
            target: root
            property: "readyOpacity"
            to: 1
            duration: root.theme.startupReadyFadeDuration
            easing.type: Easing.OutCubic
        }
        PauseAnimation { duration: root.theme.startupReadyHoldDuration }
        ScriptAction { script: root.phase = "travel"; }
        NumberAnimation
        {
            target: root
            property: "travelProgress"
            to: 1
            duration: root.theme.startupTravelDuration
            easing.type: Easing.InOutCubic
        }
        ScriptAction { script: root.phase = "background"; }
        NumberAnimation
        {
            target: root
            property: "backgroundOpacity"
            to: 0
            duration: root.theme.startupBackgroundDuration
            easing.type: Easing.InOutSine
        }
        ScriptAction { script: root.phase = "controls"; }
        NumberAnimation
        {
            target: root
            property: "controlsOpacity"
            to: 1
            duration: root.theme.startupControlsDuration
            easing.type: Easing.OutCubic
        }
        ScriptAction
        {
            script:
            {
                root.phase = "complete";
                root.finished = true;
            }
        }
    }
}
