pragma ComponentBehavior: Bound

import QtQuick

Item
{
    id: root
    required property var theme
    property string kind: "slides"
    property bool loading: false
    property real actualProgress: 0
    property string status: ""
    property bool animating: false
    property real orbitAngle: 0
    property real sweep: 0
    readonly property color accent: theme[kind + "Accent"]

    Rectangle
    {
        x: parent.width - 112
        y: 9
        width: 66
        height: 86
        radius: 14
        rotation: 12
        color: root.theme.featureLoadingPaperBottom
        border.color: root.theme.featureLoadingBorder
        Column
        {
            anchors.centerIn: parent
            spacing: 8
            Repeater
            {
                model: 3
                Rectangle { width: 34; height: 2; radius: 1; color: root.accent; opacity: 0.22 }
            }
        }
    }
    Canvas
    {
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint:
        {
            const c = getContext("2d");
            c.reset();
            c.beginPath();
            c.moveTo(38, 37);
            c.lineTo(width - 126, 37);
            c.quadraticCurveTo(width - 110, 37, width - 106, 53);
            c.quadraticCurveTo(width - 102, 68, width - 82, 68);
            c.lineTo(width - 38, 68);
            c.quadraticCurveTo(width - 8, 68, width - 8, 98);
            c.lineTo(width - 8, height - 57);
            c.quadraticCurveTo(width - 8, height - 22, width - 44, height - 22);
            c.lineTo(145, height - 22);
            c.quadraticCurveTo(117, height - 22, 109, height - 41);
            c.quadraticCurveTo(101, height - 60, 77, height - 60);
            c.lineTo(38, height - 60);
            c.quadraticCurveTo(8, height - 60, 8, height - 90);
            c.lineTo(8, 68);
            c.quadraticCurveTo(8, 37, 38, 37);
            c.closePath();
            const paper = c.createLinearGradient(0, 37, width, height);
            paper.addColorStop(0, root.theme.featureLoadingPaperTop);
            paper.addColorStop(1, root.theme.featureLoadingPaperBottom);
            c.fillStyle = paper;
            c.fill();
            c.strokeStyle = root.theme.featureLoadingBorder;
            c.lineWidth = 1;
            c.stroke();
        }
    }
    Rectangle
    {
        x: 22
        y: 0
        width: 88
        height: 88
        radius: 44
        color: root.theme.surfaceColor
        border.color: root.theme.featureLoadingBorder
        CreativeIcon
        {
            anchors.centerIn: parent
            width: 43
            height: 43
            kind: root.kind
            ink: root.accent
        }
        Canvas
        {
            anchors.fill: parent
            anchors.margins: -7
            onPaint:
            {
                const c = getContext("2d");
                c.reset();
                c.strokeStyle = root.accent;
                c.globalAlpha = 0.6;
                c.lineWidth = 2;
                c.lineCap = "round";
                c.beginPath();
                c.arc(width / 2, height / 2, width / 2 - 2, -0.8, 1.9);
                c.stroke();
            }
            rotation: root.orbitAngle
        }
    }
    Column
    {
        x: (parent.width - width) / 2
        y: parent.height * (1 - root.theme.featureLoadingContentRatio)
        width: parent.width * root.theme.featureLoadingContentRatio
        spacing: 12
        Text
        {
            width: parent.width
            text: root.kind === "word" ? "准备文档" : root.kind === "slides" ? "准备演示"
                : root.kind === "sheets" ? "准备表格" : root.kind === "pdf" ? "准备阅读"
                    : root.kind === "mindmap" ? "展开思路" : "准备书写"
            color: root.theme.workspaceInk
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize + 11
            font.weight: Font.DemiBold
        }
        Text
        {
            objectName: "featureLoadingStatus"
            width: parent.width
            text: (root.status.length > 0 ? root.status : "正在准备工作空间")
                + (root.loading ? " · " + Math.round(Math.max(0, Math.min(1, root.actualProgress)) * 100) + "%" : "")
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
            elide: Text.ElideMiddle
        }
        Rectangle
        {
            width: parent.width
            height: 5
            radius: 2.5
            color: root.theme.featureLoadingBorder
            Rectangle
            {
                x: root.loading ? 0 : root.sweep * (parent.width - width)
                width: root.loading ? parent.width * Math.max(0, Math.min(1, root.actualProgress))
                    : parent.width * (1 - root.theme.featureLoadingContentRatio)
                height: parent.height
                radius: parent.radius
                color: root.accent
                Behavior on width
                {
                    enabled: root.animating && root.loading
                    NumberAnimation
                    {
                        duration: root.theme.featureLoadingProgressDuration
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }
    SpectrumRibbon
    {
        x: -30
        y: parent.height - 44
        width: parent.width + 60
        height: 62
        theme: root.theme
        opacity: 0.8
    }
    NumberAnimation
    {
        target: root
        property: "orbitAngle"
        from: 0
        to: 360
        duration: root.theme.featureLoadingOrbitDuration
        loops: Animation.Infinite
        running: root.animating
    }
    SequentialAnimation
    {
        running: root.animating && !root.loading
        loops: Animation.Infinite
        NumberAnimation
        {
            target: root
            property: "sweep"
            from: 0
            to: 1
            duration: root.theme.featureLoadingSweepDuration
            easing.type: Easing.InOutSine
        }
        NumberAnimation
        {
            target: root
            property: "sweep"
            from: 1
            to: 0
            duration: root.theme.featureLoadingSweepDuration
            easing.type: Easing.InOutSine
        }
    }
}
