pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property string groupId: ""
    property bool canUngroup: false
    property bool canExtend: false
    property var layerOptions: ({})
    property int previousIndex: -1
    property int nextIndex: -1
    signal editRequested(string action, var options)

    spacing: 8

    Label
    {
        text: root.groupId.length > 0
            ? (root.canExtend ? "移动或调整组合层次，也可把相邻图层对象加入组合。"
                : "移动、调整层次或取消当前组合，内部对象保持相对位置。")
            : "把当前对象与相邻图层的对象组合。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 7
        visible: root.groupId.length > 0

        Repeater
        {
            model: [{label: "左移", x: -12, y: 0}, {label: "右移", x: 12, y: 0},
                {label: "上移", x: 0, y: -12}, {label: "下移", x: 0, y: 12}]
            ActionButton
            {
                required property var modelData
                theme: root.theme
                text: modelData.label
                compact: true
                enabled: root.groupId.length > 0
                onClicked: root.editRequested("moveGroup", {groupId: root.groupId,
                    x: modelData.x, y: modelData.y})
            }
        }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 7
        visible: root.groupId.length > 0

        Repeater
        {
            model: [{label: "置于底层", position: "back"},
                {label: "后移一层", position: "backward"},
                {label: "前移一层", position: "forward"},
                {label: "置于顶层", position: "front"}]
            ActionButton
            {
                required property var modelData
                objectName: "presentationGroupLayer_" + modelData.position
                theme: root.theme
                text: modelData.label
                compact: true
                enabled: root.layerOptions[modelData.position] === true
                onClicked: root.editRequested("reorderGroup",
                    {groupId: root.groupId, position: modelData.position})
            }
        }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 7
        visible: root.groupId.length === 0

        ActionButton
        {
            theme: root.theme
            text: "与下一层组合"
            compact: true
            enabled: root.previousIndex >= 0
            onClicked: root.editRequested("groupAdjacent", {targetIndex: root.previousIndex})
        }
        ActionButton
        {
            theme: root.theme
            text: "与上一层组合"
            compact: true
            enabled: root.nextIndex >= 0
            onClicked: root.editRequested("groupAdjacent", {targetIndex: root.nextIndex})
        }
    }

    Flow
    {
        Layout.fillWidth: true
        spacing: 7
        visible: root.groupId.length > 0 && root.canExtend
            && (root.previousIndex >= 0 || root.nextIndex >= 0)

        ActionButton
        {
            objectName: "presentationGroupPrependAction"
            theme: root.theme
            text: "加入下一层对象"
            compact: true
            visible: root.previousIndex >= 0
            onClicked: root.editRequested("addToGroup",
                {groupId: root.groupId, targetIndex: root.previousIndex})
        }
        ActionButton
        {
            objectName: "presentationGroupAppendAction"
            theme: root.theme
            text: "加入上一层对象"
            compact: true
            visible: root.nextIndex >= 0
            onClicked: root.editRequested("addToGroup",
                {groupId: root.groupId, targetIndex: root.nextIndex})
        }
    }

    ActionButton
    {
        objectName: "presentationUngroupAction"
        theme: root.theme
        text: "取消组合"
        compact: true
        visible: root.groupId.length > 0
        enabled: root.canUngroup
        onClicked: root.editRequested("ungroup", {groupId: root.groupId})
    }
}
