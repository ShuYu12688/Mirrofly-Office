import QtQuick

Item
{
    id: root

    property var theme
    property bool motionActive: false
    property real homeOpacity: 0

    implicitWidth: 80
    implicitHeight: 80

    Image
    {
        anchors.fill: parent
        source: "../../assets/mirrorfly-mark-light.svg"
        sourceSize.width: 256
        sourceSize.height: 256
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: 1 - root.homeOpacity
    }

    Image
    {
        anchors.fill: parent
        source: "../../assets/mirrorfly-mark.svg"
        sourceSize.width: 256
        sourceSize.height: 256
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: root.homeOpacity
    }
}
