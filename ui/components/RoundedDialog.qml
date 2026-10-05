pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Dialog
{
    id: root

    property var theme
    property real overlayRadius: theme.windowRadius

    popupType: Popup.Item
    modal: true
    focus: true
    padding: 24
    spacing: 20
    font.family: theme.fontFamily
    font.pixelSize: theme.fontSize

    background: Rectangle
    {
        radius: root.theme.windowRadius
        color: root.theme.surfaceColor
        opacity: root.theme.surfaceOpacity
        border.width: 1
        border.color: root.theme.borderColor
    }

    header: Text
    {
        text: root.title
        visible: root.title.length > 0
        leftPadding: root.leftPadding
        rightPadding: root.rightPadding
        topPadding: root.topPadding
        bottomPadding: 0
        color: root.theme.textPrimary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize + 4
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
    }

    Overlay.modal: Rectangle
    {
        radius: root.overlayRadius
        color: root.theme.noticeBackground
        opacity: root.theme.dialogOverlayOpacity
        antialiasing: true
    }

    enter: Transition
    {
        NumberAnimation
        {
            property: "opacity"
            from: 0
            to: 1
            duration: root.theme.motionEnabled ? root.theme.motionDuration : 0
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition
    {
        NumberAnimation
        {
            property: "opacity"
            from: 1
            to: 0
            duration: root.theme.motionEnabled ? root.theme.motionDuration : 0
            easing.type: Easing.OutCubic
        }
    }
}
