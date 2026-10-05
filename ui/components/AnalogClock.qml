pragma ComponentBehavior: Bound

import QtQuick

Item
{
    id: root
    objectName: "homeAnalogClock"
    required property var theme
    property date now: new Date()
    readonly property real hourAngle: (now.getHours() % 12) * 30 + now.getMinutes() * 0.5
        + now.getSeconds() / 120
    readonly property real minuteAngle: now.getMinutes() * 6 + now.getSeconds() * 0.1
    readonly property real secondAngle: now.getSeconds() * 6
    readonly property real diameter: Math.min(width, height)
    implicitWidth: theme.homeDialSize
    implicitHeight: implicitWidth
    Accessible.name: "当前时间 " + Qt.formatTime(now, "hh:mm:ss")

    Rectangle
    {
        id: dial
        anchors.centerIn: parent
        width: root.diameter
        height: width
        radius: width / 2
        color: root.theme.homeDialFace
        border.width: 1
        border.color: root.theme.homeRule
        Repeater
        {
            model: 60
            Item
            {
                id: tick
                required property int index
                anchors.fill: parent
                rotation: index * 6
                Rectangle
                {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height * 0.055
                    width: tick.index % 5 === 0 ? 2 : 1
                    height: parent.height * (tick.index % 5 === 0 ? 0.055 : 0.022)
                    radius: width / 2
                    color: tick.index % 5 === 0 ? root.theme.homeInk : root.theme.homeDialTick
                }
            }
        }
        Repeater
        {
            model: [12, 3, 6, 9]
            Text
            {
                required property int modelData
                readonly property real angle: modelData * Math.PI / 6 - Math.PI / 2
                x: dial.width / 2 + Math.cos(angle) * dial.width * 0.32 - width / 2
                y: dial.height / 2 + Math.sin(angle) * dial.height * 0.32 - height / 2
                text: modelData
                color: root.theme.homeInk
                font.family: root.theme.homeNumberFont
                font.pixelSize: dial.width * 0.073
                font.weight: Font.Medium
            }
        }
        Item
        {
            objectName: "clockHourHand"
            anchors.fill: parent
            rotation: root.hourAngle
            Rectangle
            {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.26
                width: parent.width * 0.025
                height: parent.height * 0.28
                radius: width / 2
                color: root.theme.homeInk
            }
        }
        Item
        {
            objectName: "clockMinuteHand"
            anchors.fill: parent
            rotation: root.minuteAngle
            Rectangle
            {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.15
                width: parent.width * 0.017
                height: parent.height * 0.39
                radius: width / 2
                color: root.theme.homeInk
            }
        }
        Item
        {
            objectName: "clockSecondHand"
            anchors.fill: parent
            rotation: root.secondAngle
            Rectangle
            {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.12
                width: 1.5
                height: parent.height * 0.47
                radius: 1
                color: root.theme.homeDialAccent
            }
        }
        Rectangle
        {
            anchors.centerIn: parent
            width: parent.width * 0.047
            height: width
            radius: width / 2
            color: root.theme.homeDialAccent
            border.width: 2
            border.color: root.theme.homeDialFace
        }
    }
}
