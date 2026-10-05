import QtQuick
import QtQuick.Controls

AbstractButton
{
    id: root
    required property var theme
    property bool primary: false
    property bool compact: true
    property string iconName: ""
    implicitWidth: Math.max(78, label.implicitWidth + 30)
    implicitHeight: 34
    hoverEnabled: true
    activeFocusOnTab: true
    padding: 12
    background: Rectangle
    {
        radius: root.theme.homeButtonRadius
        gradient: Gradient
        {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: root.theme.spectrumBlue }
            GradientStop { position: 0.25; color: root.theme.spectrumSage }
            GradientStop { position: 0.5; color: root.theme.spectrumGold }
            GradientStop { position: 0.75; color: root.theme.spectrumCoral }
            GradientStop { position: 1; color: root.theme.spectrumViolet }
        }
        Rectangle
        {
            anchors.fill: parent
            anchors.margins: root.visualFocus ? 2 : root.theme.homeButtonBorderWidth
            radius: Math.max(0, root.theme.homeButtonRadius - anchors.margins)
            color: root.primary ? root.theme.homeInk
                : (root.hovered ? root.theme.homeFocusPaper : root.theme.homePaper)
        }
        opacity: root.down ? 0.8 : 1
    }
    contentItem: Text
    {
        id: label
        text: root.text
        color: root.primary ? root.theme.whiteColor : root.theme.homeInk
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}