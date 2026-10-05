pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var selection: ({})
    property int slideCount: 0
    property int currentSlide: 0
    property string pendingKind: "slide"
    signal editRequested(string action, var options)

    spacing: 8

    onSelectionChanged:
    {
        const click = selection.clickAction || {};
        pendingKind = click.kind || "slide";
        targetPage.value = click.targetSlide >= 0
            ? click.targetSlide + 1 : Math.min(slideCount, currentSlide + 2);
    }

    Text
    {
        Layout.fillWidth: true
        text: "放映时点击当前对象，跳转到指定页面或执行放映导航。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    RowLayout
    {
        Layout.fillWidth: true
        spacing: 8

        Label { text: "点击后"; color: root.theme.textSecondary }
        ComboBox
        {
            id: actionKind
            objectName: "presentationLinkKind"
            Layout.preferredWidth: 160
            textRole: "label"
            valueRole: "kind"
            model: [{label: "跳转到页面", kind: "slide"},
                {label: "下一页", kind: "nextslide"},
                {label: "上一页", kind: "previousslide"},
                {label: "第一页", kind: "firstslide"},
                {label: "最后一页", kind: "lastslide"},
                {label: "上次浏览页", kind: "lastslideviewed"},
                {label: "结束放映", kind: "endshow"}]
            currentIndex:
            {
                for (let index = 0; index < model.length; ++index)
                    if (model[index].kind === root.pendingKind)
                        return index;
                return 0;
            }
            Accessible.name: "对象点击后的放映操作"
            onActivated: root.pendingKind = currentValue
        }

        Label
        {
            visible: root.pendingKind === "slide"
            text: "目标页"
            color: root.theme.textSecondary
        }
        SpinBox
        {
            id: targetPage
            objectName: "presentationLinkTargetPage"
            visible: root.pendingKind === "slide"
            from: 1
            to: Math.max(1, root.slideCount)
            value: 1
            editable: true
            Accessible.name: "超链接目标页码"
        }
        ActionButton
        {
            theme: root.theme
            text: "设置跳转"
            compact: true
            enabled: root.slideCount > 0
            onClicked: root.editRequested("setClickAction",
                {kind: root.pendingKind, targetSlide: targetPage.value - 1})
        }
        ActionButton
        {
            theme: root.theme
            text: "清除跳转"
            compact: true
            enabled: Boolean((root.selection.clickAction || {}).kind)
            onClicked: root.editRequested("setClickAction", {kind: ""})
        }
        Item { Layout.fillWidth: true }
    }
}
