pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mirrorfly.Native 1.0

ColumnLayout
{
    id: root
    property var theme
    property string group: "academic"
    property var previews: ({})
    property string selectedKey: ""
    property string selectedDescription: ""
    readonly property Item focusTarget: selectedKey.length > 0 ? insertRow : choices
    signal insertRequested(string key)
    spacing: 8
    onGroupChanged: selectedKey = ""
    onVisibleChanged: if (!visible) selectedKey = ""

    RowLayout
    {
        id: choices
        Layout.fillWidth: true
        spacing: 12
        Repeater
        {
            model: root.group === "academic"
                ? [{key: "researchStudio", title: "研究路径", detail: "核心问题 + 三步研究过程"},
                    {key: "evidenceBoard", title: "证据与结论", detail: "主要发现 + 两组证据 + 来源"}]
                : [{key: "projectDashboard", title: "项目看板", detail: "关键指标 + 进展 + 风险与支持"},
                    {key: "deliveryRoadmap", title: "交付路线", detail: "三阶段交付 + 负责人 + 验收标准"}]
            delegate: AbstractButton
            {
                id: card
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 72
                Accessible.name: modelData.title
                hoverEnabled: true
                onClicked:
                {
                    root.selectedKey = modelData.key;
                    root.selectedDescription = modelData.title + " · " + modelData.detail;
                }
                background: Rectangle
                {
                    color: root.theme.surfaceColor
                    radius: root.theme.radius * 0.6
                    border.color: root.selectedKey === card.modelData.key || card.activeFocus ? root.theme.accent : root.theme.borderColor
                    border.width: root.selectedKey === card.modelData.key ? 2 : 1
                }
                contentItem: RowLayout
                {
                    spacing: 12
                    SlideView
                    {
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 54
                        Layout.leftMargin: 10
                        document: root.previews[card.modelData.key]
                        theme: root.theme
                    }
                    ColumnLayout
                    {
                        Layout.fillWidth: true
                        Layout.rightMargin: 10
                        Text { Layout.fillWidth: true; text: card.modelData.title; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize + 1; font.bold: true }
                        Text { Layout.fillWidth: true; text: card.modelData.detail; wrapMode: Text.Wrap; color: root.theme.textSecondary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize - 2 }
                    }
                }
            }
        }
    }

    RowLayout
    {
        id: insertRow
        objectName: "presentationTemplateInsert"
        Layout.fillWidth: true
        visible: root.selectedKey.length > 0
        Text { Layout.fillWidth: true; text: root.selectedDescription; wrapMode: Text.Wrap; color: root.theme.textSecondary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize - 1 }
        ActionButton { theme: root.theme; text: "插入这套版式"; compact: true; iconName: "plus"; onClicked: root.insertRequested(root.selectedKey) }
    }
}
