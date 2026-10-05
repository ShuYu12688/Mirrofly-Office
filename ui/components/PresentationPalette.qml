pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout
{
    id: root

    property var theme
    signal colorSelected(string color)

    spacing: 7

    Repeater
    {
        model: [root.theme.slidesPaper, root.theme.slidesInk, root.theme.spectrumCoral,
            root.theme.spectrumGold, root.theme.spectrumSage, root.theme.spectrumBlue, root.theme.spectrumViolet]

        AbstractButton
        {
            id: swatch

            required property string modelData

            implicitWidth: 29
            implicitHeight: 29
            hoverEnabled: true
            Accessible.name: "颜色 " + modelData
            onClicked: root.colorSelected(modelData)
            background: Rectangle
            {
                radius: 10
                color: swatch.modelData
                border.width: swatch.hovered || swatch.visualFocus ? 2 : 1
                border.color: root.theme.borderColor
                rotation: swatch.hovered && root.theme.motionEnabled ? -6 : 0
            }
        }
    }

    TextField
    {
        id: customColor

        Layout.preferredWidth: 106
        placeholderText: "#RRGGBB"
        Accessible.name: "自定义颜色"
        maximumLength: 7
        onAccepted: if (/^#[0-9a-fA-F]{6}$/.test(text)) root.colorSelected(text)
    }

    ActionButton
    {
        theme: root.theme
        text: "应用"
        iconName: ""
        compact: true
        enabled: /^#[0-9a-fA-F]{6}$/.test(customColor.text)
        onClicked: root.colorSelected(customColor.text)
    }

    Item { Layout.fillWidth: true }
}
