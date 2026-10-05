pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var settings: ({})
    property real slideWidth: 0
    property real slideHeight: 0
    signal settingsRequested(var patch)

    spacing: 4

    Text
    {
        Layout.fillWidth: true
        text: "标尺、网格和参考线仅辅助编辑，不写入演示文稿；拖动对象时可按边缘吸附。单位：pt。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 8

        CheckBox
        {
            objectName: "presentationShowRulers"
            text: "显示标尺"
            checked: root.settings.showRulers === true
            onClicked: root.settingsRequested({showRulers: checked})
        }
        CheckBox
        {
            objectName: "presentationShowGrid"
            text: "显示网格"
            checked: root.settings.showGrid === true
            onClicked: root.settingsRequested({showGrid: checked})
        }
        CheckBox
        {
            objectName: "presentationShowGuides"
            text: "显示参考线"
            checked: root.settings.showGuides === true
            onClicked: root.settingsRequested({showGuides: checked})
        }
        CheckBox
        {
            objectName: "presentationSnapGrid"
            text: "移动时吸附网格"
            checked: root.settings.snapToGrid === true
            onClicked: root.settingsRequested({snapToGrid: checked})
        }
        CheckBox
        {
            objectName: "presentationSnapGuides"
            text: "移动时吸附参考线"
            checked: root.settings.snapToGuides === true
            onClicked: root.settingsRequested({snapToGuides: checked})
        }
    }

    RowLayout
    {
        spacing: 8
        Label { text: "网格间距"; color: root.theme.textSecondary }
        SpinBox
        {
            objectName: "presentationGridSpacing"
            from: 2
            to: 100
            value: Math.round(root.settings.gridSpacingPt || 12)
            editable: true
            Accessible.name: "网格间距，点"
            onValueModified: root.settingsRequested({gridSpacingPt: value})
        }
        Label { text: "pt"; color: root.theme.textSecondary }
    }

    RowLayout
    {
        Layout.fillWidth: true
        spacing: 8
        Label { text: "竖线位置"; color: root.theme.textSecondary }
        SpinBox
        {
            id: verticalPosition
            objectName: "presentationVerticalGuidePosition"
            from: 0
            to: Math.max(0, Math.floor(root.slideWidth))
            value: Math.round(root.slideWidth / 2)
            editable: true
            Accessible.name: "竖参考线位置，点"
        }
        ActionButton
        {
            theme: root.theme
            text: "加竖线"
            compact: true
            enabled: (root.settings.verticalGuidesPt || []).length < 16
            onClicked: root.settingsRequested({verticalGuidesPt:
                (root.settings.verticalGuidesPt || []).concat([verticalPosition.value])})
        }
        Label { text: "横线位置"; color: root.theme.textSecondary }
        SpinBox
        {
            id: horizontalPosition
            objectName: "presentationHorizontalGuidePosition"
            from: 0
            to: Math.max(0, Math.floor(root.slideHeight))
            value: Math.round(root.slideHeight / 2)
            editable: true
            Accessible.name: "横参考线位置，点"
        }
        ActionButton
        {
            theme: root.theme
            text: "加横线"
            compact: true
            enabled: (root.settings.horizontalGuidesPt || []).length < 16
            onClicked: root.settingsRequested({horizontalGuidesPt:
                (root.settings.horizontalGuidesPt || []).concat([horizontalPosition.value])})
        }
        Item { Layout.fillWidth: true }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 7
        Repeater
        {
            model: root.settings.verticalGuidesPt || []
            ActionButton
            {
                required property real modelData
                theme: root.theme
                text: "竖 " + Math.round(modelData) + " pt ×"
                compact: true
                onClicked: root.settingsRequested({verticalGuidesPt:
                    (root.settings.verticalGuidesPt || []).filter(function(value) { return value !== modelData; })})
            }
        }
        Repeater
        {
            model: root.settings.horizontalGuidesPt || []
            ActionButton
            {
                required property real modelData
                theme: root.theme
                text: "横 " + Math.round(modelData) + " pt ×"
                compact: true
                onClicked: root.settingsRequested({horizontalGuidesPt:
                    (root.settings.horizontalGuidesPt || []).filter(function(value) { return value !== modelData; })})
            }
        }
    }
}
