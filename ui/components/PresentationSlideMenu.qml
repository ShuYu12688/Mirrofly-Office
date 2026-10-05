pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Menu
{
    id: root

    property var theme
    property int targetSlide: -1
    property int slideCount: 0
    property bool editable: false
    property bool busy: false
    property bool slideHidden: false
    signal actionRequested(string action, var options)
    signal editableCopyRequested()

    width: 208
    padding: 6
    modal: true

    background: Rectangle
    {
        color: root.theme.surfaceColor
        radius: root.theme.radius
        border.color: root.theme.borderColor
    }

    delegate: MenuItem
    {
        id: entry

        implicitHeight: 36
        contentItem: Text
        {
            text: entry.text
            textFormat: Text.PlainText
            color: entry.enabled ? root.theme.textPrimary : root.theme.mutedColor
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 1
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle
        {
            radius: root.theme.radius * 0.5
            color: entry.highlighted ? root.theme.hoverColor : root.theme.transparentColor
        }
    }

    Action
    {
        text: "在前面插入空白页"
        enabled: root.editable && !root.busy && root.slideCount < 200
        onTriggered: root.actionRequested("addSlide", {layout: "blank", before: true})
    }
    Action
    {
        text: "在后面插入空白页"
        enabled: root.editable && !root.busy && root.slideCount < 200
        onTriggered: root.actionRequested("addSlide", {layout: "blank"})
    }
    Action
    {
        text: "复制此页"
        enabled: root.editable && !root.busy && root.slideCount < 200
        onTriggered: root.actionRequested("duplicateSlide", {})
    }
    MenuSeparator { }
    Action
    {
        text: "向前移动"
        enabled: root.editable && !root.busy && root.targetSlide > 0
        onTriggered: root.actionRequested("moveSlide", {offset: -1})
    }
    Action
    {
        text: "向后移动"
        enabled: root.editable && !root.busy && root.targetSlide + 1 < root.slideCount
        onTriggered: root.actionRequested("moveSlide", {offset: 1})
    }
    Action
    {
        text: root.slideHidden ? "在放映中显示" : "在放映中隐藏"
        enabled: root.editable && !root.busy
        onTriggered: root.actionRequested("slideHidden", {hidden: !root.slideHidden})
    }
    MenuSeparator { }
    Action
    {
        text: "删除此页"
        enabled: root.editable && !root.busy && root.slideCount > 1
        onTriggered: root.actionRequested("deleteSlide", {})
    }
    Action
    {
        text: root.editable ? "正在编辑文稿" : "创建可编辑副本…"
        enabled: !root.editable && !root.busy
        onTriggered: root.editableCopyRequested()
    }
}
