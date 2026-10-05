pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DashboardSurface
{
    id: root
    objectName: "pomodoroCard"
    property bool paperMode: false
    frosted: !paperMode
    paperColor: paperMode ? theme.homeFocusPaper : theme.surfaceColor
    radius: paperMode ? theme.homeFocusRadius : theme.workspaceRadius
    property bool resting: false
    property bool running: false
    property int completed: 0
    readonly property int durationSeconds: resting ? theme.pomodoroBreakSeconds : theme.pomodoroFocusSeconds
    property int remaining: theme.pomodoroFocusSeconds
    property double deadline: 0
    readonly property real progress: 1 - remaining / Math.max(1, durationSeconds)
    readonly property string clockText: Math.floor(remaining / 60).toString().padStart(2, "0")
        + ":" + (remaining % 60).toString().padStart(2, "0")
    implicitHeight: theme.dashboardToolHeight

    function selectMode(breakTime)
    {
        running = false;
        resting = breakTime;
        remaining = durationSeconds;
    }
    function toggleTimer()
    {
        if (running)
        {
            tick(Date.now());
            running = false;
        }
        else
        {
            deadline = Date.now() + remaining * 1000;
            running = true;
        }
    }
    function tick(now)
    {
        if (!running) return;
        remaining = Math.max(0, Math.ceil((deadline - now) / 1000));
        if (remaining === 0)
        {
            running = false;
            if (!resting) completed++;
            resting = !resting;
            remaining = durationSeconds;
        }
    }
    Timer
    {
        interval: 250
        running: root.running
        repeat: true
        onTriggered: root.tick(Date.now())
    }
    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: root.theme.workspaceTilePadding
        spacing: 10
        RowLayout
        {
            Layout.fillWidth: true
            Text
            {
                Layout.fillWidth: true
                text: "专注片刻"
                color: root.theme.workspaceInk
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize + 3
                font.weight: Font.DemiBold
            }
            Text
            {
                text: root.completed + " 次完成"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }
        }
        RowLayout
        {
            Layout.alignment: Qt.AlignHCenter
            Repeater
            {
                model: ["专注 25 分钟", "休息 5 分钟"]
                AbstractButton
                {
                    id: modeButton
                    required property string modelData
                    required property int index
                    readonly property bool selected: root.resting === (index === 1)
                    text: modelData
                    implicitWidth: root.paperMode ? 94 : 108
                    implicitHeight: 30
                    hoverEnabled: true
                    onClicked: root.selectMode(index === 1)
                    background: Rectangle
                    {
                        radius: 15
                        color: modeButton.selected ? root.theme.pomodoroSoft : root.theme.transparentColor
                    }
                    contentItem: Text
                    {
                        text: modeButton.text
                        color: modeButton.selected ? root.theme.pomodoroInk : root.theme.textSecondary
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize - 2
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
        Item
        {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.paperMode ? 194 : 160
            Layout.preferredHeight: root.paperMode ? 194 : 160
            Canvas
            {
                id: ring
                anchors.fill: parent
                property real progress: root.progress
                onProgressChanged: requestPaint()
                onPaint:
                {
                    const c = getContext("2d");
                    c.reset();
                    c.lineWidth = 7;
                    c.lineCap = "round";
                    c.strokeStyle = root.theme.pomodoroSoft;
                    c.beginPath(); c.arc(width / 2, height / 2, width / 2 - 9, 0, Math.PI * 2); c.stroke();
                    if (progress > 0)
                    {
                        c.strokeStyle = root.theme.pomodoroInk;
                        c.beginPath();
                        c.arc(width / 2, height / 2, width / 2 - 9, -Math.PI / 2,
                            -Math.PI / 2 + progress * Math.PI * 2);
                        c.stroke();
                    }
                }
            }
            Column
            {
                anchors.centerIn: parent
                spacing: 5
                Text
                {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.clockText
                    color: root.theme.workspaceInk
                    font.family: root.theme.homeNumberFont
                    font.pixelSize: root.paperMode ? 42 : 34
                    font.weight: Font.Medium
                }
                Text
                {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.running ? "专注进行中" : (root.resting ? "休息一下" : "25 分钟，只做一件事")
                    color: root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: 10
                }
            }
        }
        RowLayout
        {
            Layout.alignment: Qt.AlignHCenter
            HomeToolButton
            {
                objectName: "pomodoroToggle"
                theme: root.theme
                text: root.running ? "暂停" : "开始"
                primary: true
                compact: true
                onClicked: root.toggleTimer()
            }
            HomeToolButton
            {
                theme: root.theme
                text: "重置"
                compact: true
                onClicked: root.selectMode(root.resting)
            }
        }
    }
}
