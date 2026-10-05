import QtQuick
import QtQuick.Controls

TextField
{
    id: root
    required property var theme
    implicitHeight: theme.workspaceFieldHeight
    leftPadding: 16
    rightPadding: 16
    color: theme.textPrimary
    placeholderTextColor: theme.mutedColor
    selectionColor: theme.accent
    selectedTextColor: theme.whiteColor
    font.family: theme.fontFamily
    font.pixelSize: theme.fontSize
    selectByMouse: true
    activeFocusOnTab: true
    background: Rectangle
    {
        radius: root.theme.workspaceFieldRadius
        color: root.theme.workspaceFieldSurface
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? root.theme.accent : root.theme.borderColor
        Behavior on border.color
        {
            ColorAnimation { duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0 }
        }
    }
}
