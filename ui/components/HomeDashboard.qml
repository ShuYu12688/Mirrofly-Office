pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

GridLayout
{
    id: root
    required property var theme
    required property var agent
    property Item backdrop: null
    property point backdropOrigin: Qt.point(0, 0)
    property int recentCount: 0
    property date now: new Date()
    readonly property string greeting: greetingAt(now.getHours())
    readonly property var usage: agent.usageStats
    readonly property bool narrow: width < theme.homeDashboardBreakpoint
    signal openRequested()
    signal createRequested()
    columns: narrow ? 1 : 12
    columnSpacing: theme.dashboardGap
    rowSpacing: theme.dashboardGap

    function greetingAt(hour)
    {
        if (hour < 6) return "夜深了，早些休息";
        if (hour < 11) return "早上好，开启新的一天";
        if (hour < 14) return "中午好，记得适当休息";
        if (hour < 18) return "下午好，继续今天的创作";
        return "晚上好，享受片刻宁静";
    }
    function number(value)
    {
        return Number(value || 0).toLocaleString(Qt.locale(), "f", 0);
    }
    Timer
    {
        interval: 1000
        running: root.visible && root.Window.window !== null && root.Window.window.visible
            && root.Window.window.visibility !== Window.Minimized
        repeat: true
        onTriggered: root.now = new Date()
    }
    onVisibleChanged: if (visible) now = new Date()

    DashboardSurface
    {
        id: clockCard
        objectName: "dashboardClockPanel"
        readonly property bool stacked: width < root.theme.homeClockStackBreakpoint
        Layout.row: 0
        Layout.column: 0
        Layout.columnSpan: root.narrow ? 1 : 7
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: stacked ? root.theme.homeClockStackHeight : root.theme.homeClockHeight
        theme: root.theme
        backdrop: root.backdrop
        backdropOrigin: root.backdropOrigin
        ColumnLayout
        {
            anchors.fill: parent
            anchors.margins: root.theme.homeNotePadding
            spacing: 14
            Text
            {
                text: "此刻"
                color: root.theme.homeInk
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize + 3
                font.weight: Font.Medium
            }
            GridLayout
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: clockCard.stacked ? 1 : 2
                columnSpacing: 24
                rowSpacing: 10
                AnalogClock
                {
                    Layout.alignment: Qt.AlignCenter
                    Layout.preferredWidth: root.theme.homeDialSize
                    Layout.preferredHeight: root.theme.homeDialSize
                    theme: root.theme
                    now: root.now
                }
                ColumnLayout
                {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: clockCard.stacked ? Qt.AlignHCenter : Qt.AlignVCenter
                    spacing: 8
                    Text
                    {
                        Layout.fillWidth: true
                        text: Qt.formatDate(root.now, "dddd")
                        color: root.theme.homeMuted
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize
                        horizontalAlignment: clockCard.stacked ? Text.AlignHCenter : Text.AlignLeft
                    }
                    Text
                    {
                        objectName: "dashboardClock"
                        Layout.fillWidth: true
                        text: Qt.formatTime(root.now, "hh:mm")
                        color: root.theme.homeInk
                        font.family: root.theme.homeNumberFont
                        font.pixelSize: root.theme.homeClockSize
                        font.weight: Font.Medium
                        horizontalAlignment: clockCard.stacked ? Text.AlignHCenter : Text.AlignLeft
                    }
                    Text
                    {
                        Layout.fillWidth: true
                        text: Qt.formatDate(root.now, "yyyy 年 M 月 d 日")
                        color: root.theme.homeMuted
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize - 2
                        elide: Text.ElideRight
                        horizontalAlignment: clockCard.stacked ? Text.AlignHCenter : Text.AlignLeft
                    }
                }
            }
        }
    }
    PomodoroCard
    {
        Layout.row: root.narrow ? 1 : 0
        Layout.column: root.narrow ? 0 : 7
        Layout.columnSpan: root.narrow ? 1 : 5
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: root.theme.homeFocusHeight
        theme: root.theme
        backdrop: root.backdrop
        backdropOrigin: root.backdropOrigin
    }
    DashboardSurface
    {
        objectName: "dashboardNotePanel"
        Layout.row: root.narrow ? 2 : 1
        Layout.column: 0
        Layout.columnSpan: root.narrow ? 1 : 7
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: root.theme.homeNoteHeight
        theme: root.theme
        backdrop: root.backdrop
        backdropOrigin: root.backdropOrigin
        ColumnLayout
        {
            anchors.fill: parent
            anchors.margins: root.theme.homeNotePadding
            spacing: 10
            RowLayout
            {
                Layout.fillWidth: true
                Text
                {
                    Layout.fillWidth: true
                    text: "随手记"
                    color: root.theme.homeInk
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize + 3
                    font.weight: Font.Medium
                }
                Text
                {
                    text: note.length + " 字"
                    color: root.theme.homeMuted
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                }
            }
            ScrollView
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea
                {
                    id: note
                    objectName: "dashboardNote"
                    placeholderText: "写下今天的想法…"
                    wrapMode: TextEdit.Wrap
                    color: root.theme.homeInk
                    placeholderTextColor: root.theme.homeMuted
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize
                    padding: 0
                    selectByMouse: true
                    background: Item {}
                }
            }
            RowLayout
            {
                Layout.fillWidth: true
                HomeToolButton { theme: root.theme; text: "打开文件"; onClicked: root.openRequested() }
                HomeToolButton { theme: root.theme; text: "开始创作"; onClicked: root.createRequested() }
                Item { Layout.fillWidth: true }
            }
        }
    }
    DashboardSurface
    {
        objectName: "dashboardUsageStrip"
        Layout.row: root.narrow ? 3 : 1
        Layout.column: root.narrow ? 0 : 7
        Layout.columnSpan: root.narrow ? 1 : 5
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: root.theme.homeUsageHeight
        theme: root.theme
        backdrop: root.backdrop
        backdropOrigin: root.backdropOrigin
        ColumnLayout
        {
            anchors.fill: parent
            anchors.margins: root.theme.homeNotePadding
            spacing: 12
            Text
            {
                text: "AI 用量"
                color: root.theme.homeInk
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize + 3
                font.weight: Font.Medium
            }
            Text
            {
                objectName: "dashboardTokenTotal"
                Layout.fillWidth: true
                text: root.number(root.usage.total) + " token"
                color: root.theme.homeInk
                font.family: root.theme.homeNumberFont
                font.pixelSize: root.theme.dashboardTokenSize - 6
                elide: Text.ElideRight
            }
            RowLayout
            {
                Layout.fillWidth: true
                spacing: 12
                Repeater
                {
                    model: [ { "label": "输入", "value": root.usage.input },
                        { "label": "输出", "value": root.usage.output },
                        { "label": "缓存", "value": root.usage.cached } ]
                    ColumnLayout
                    {
                        id: usageColumn
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 6
                        Text
                        {
                            text: usageColumn.modelData.label
                            color: root.theme.homeMuted
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.fontSize - 2
                        }
                        Text
                        {
                            Layout.fillWidth: true
                            text: root.number(usageColumn.modelData.value)
                            color: root.theme.homeInk
                            font.family: root.theme.homeNumberFont
                            font.pixelSize: root.theme.fontSize
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Text
            {
                text: "本次启动统计"
                color: root.theme.homeMuted
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }
        }
    }
}
