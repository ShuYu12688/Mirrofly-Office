pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var slideSections: []
    property int currentSlide: 0
    readonly property var currentSection:
    {
        for (const section of slideSections)
            if (currentSlide >= section.firstSlide &&
                currentSlide < section.firstSlide + section.slideCount)
                return section;
        return null;
    }
    readonly property bool canCreate: !currentSection || currentSlide > currentSection.firstSlide
    signal editRequested(string action, var options)

    spacing: 8

    onCurrentSectionChanged: sectionName.text = currentSection ? currentSection.name : ""

    Text
    {
        Layout.fillWidth: true
        text: root.currentSection
            ? "当前属于“" + root.currentSection.name + "”；新建节会从当前页开始。"
            : "从当前页开始建立节；之前的页面会自动归入“默认节”。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    RowLayout
    {
        Layout.fillWidth: true
        spacing: 8

        TextField
        {
            id: sectionName
            objectName: "presentationSectionName"
            Layout.preferredWidth: 240
            maximumLength: 64
            placeholderText: "节名称"
            Accessible.name: "演示文稿节名称"
        }

        ActionButton
        {
            theme: root.theme
            text: "从当前页新建节"
            compact: true
            enabled: root.canCreate && sectionName.text.trim().length > 0
            onClicked: root.editRequested("createSection", {name: sectionName.text.trim()})
        }
        ActionButton
        {
            theme: root.theme
            text: "重命名当前节"
            compact: true
            enabled: root.currentSection !== null && sectionName.text.trim().length > 0
            onClicked: root.editRequested("renameSection", {name: sectionName.text.trim()})
        }
        ActionButton
        {
            theme: root.theme
            text: "移除节划分"
            compact: true
            enabled: root.currentSection !== null
            onClicked: root.editRequested("removeSection", {})
        }
        Item { Layout.fillWidth: true }
    }
}
