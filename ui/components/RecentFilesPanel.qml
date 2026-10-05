pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item
{
    id: root
    property var theme
    property var files: []
    property string query: ""
    property string category: "all"
    signal openRequested()
    signal queryEdited(string query)
    signal categorySelected(string category)
    signal fileRequested(string path)
    signal starRequested(string path)
    implicitHeight: content.implicitHeight

    function focusSearch()
    {
        searchField.forceActiveFocus();
        searchField.selectAll();
    }
    Column
    {
        id: content
        width: parent.width
        spacing: root.theme.workspaceGap
        RowLayout
        {
            width: parent.width
            spacing: 12
            RoundedField
            {
                id: searchField
                objectName: "recentSearchField"
                Layout.fillWidth: true
                theme: root.theme
                text: root.query
                placeholderText: "搜索文件名称或路径 · Ctrl K"
                Accessible.name: "搜索最近文件"
                onTextEdited: root.queryEdited(text)
            }
            ActionButton
            {
                theme: root.theme
                text: "打开文件"
                iconName: "folder"
                onClicked: root.openRequested()
            }
        }
        RowLayout
        {
            width: parent.width
            spacing: 10
            Repeater
            {
                model: [ { "label": "全部", "value": "all" }, { "label": "收藏", "value": "starred" } ]
                ActionButton
                {
                    id: filterButton
                    required property var modelData
                    theme: root.theme
                    text: modelData.label
                    primary: root.category === modelData.value
                    compact: true
                    onClicked: root.categorySelected(modelData.value)
                }
            }
            ComboBox
            {
                id: formatFilter
                Layout.preferredWidth: 128
                implicitHeight: 34
                model: ["所有格式", "Word", "演示", "表格", "Markdown", "纯文本", "PDF", "导图"]
                readonly property var values: ["all", "word", "slides", "sheets", "markdown", "writer", "pdf", "mindmap"]
                currentIndex: Math.max(0, values.indexOf(root.category))
                Accessible.name: "最近文件格式筛选"
                onActivated: root.categorySelected(values[currentIndex])
                background: Rectangle
                {
                    radius: root.theme.workspaceFieldRadius
                    color: root.theme.surfaceColor
                    border.width: 1
                    border.color: root.theme.borderColor
                }
                palette.text: root.theme.workspaceInk
                palette.buttonText: root.theme.workspaceInk
                palette.base: root.theme.surfaceColor
                palette.highlight: root.theme.accent
            }
            Item { Layout.fillWidth: true }
            Text
            {
                text: root.files.length + " 项"
                color: root.theme.mutedColor
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }
        }
        DashboardSurface
        {
            width: parent.width
            height: 220
            visible: root.files.length === 0
            theme: root.theme
            Column
            {
                anchors.centerIn: parent
                spacing: 14
                UiIcon
                {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 32
                    height: 32
                    symbol: root.query.length > 0 ? "search" : "folder"
                    iconColor: root.theme.mutedColor
                }
                Text
                {
                    text: root.query.length > 0 ? "没有匹配的文件"
                        : (root.category === "starred" ? "收藏喜欢的文件，留待下次" : "打开一份文件，从这里继续")
                    color: root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize
                }
            }
        }
        ListView
        {
            id: fileList
            width: parent.width
            height: Math.min(root.theme.recentListHeight,
                root.files.length * (root.theme.recentRowHeight + 10))
            visible: root.files.length > 0
            model: root.files
            spacing: 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: AbstractButton
            {
                id: fileRow
                required property var modelData
                objectName: "recentFileRow"
                width: fileList.width - 12
                height: root.theme.recentRowHeight
                padding: 18
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: modelData.name
                Accessible.description: modelData.path
                onClicked: root.fileRequested(modelData.path)
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: modelData.path
                background: Rectangle
                {
                    radius: root.theme.workspaceRadius
                    color: fileRow.hovered ? root.theme.workspaceFieldSurface : root.theme.surfaceColor
                    border.width: 1
                    border.color: root.theme.workspaceGlassBorder
                    SpectrumStroke
                    {
                        anchors.fill: parent
                        theme: root.theme
                        radius: parent.radius
                        visible: fileRow.hovered || fileRow.visualFocus
                    }
                }
                contentItem: RowLayout
                {
                    spacing: 16
                    Rectangle
                    {
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 56
                        radius: 16
                        color: root.theme["create" + fileRow.modelData.kind + "Color"] || root.theme.createwriterColor
                        CreativeIcon
                        {
                            anchors.centerIn: parent
                            width: 32
                            height: 32
                            kind: fileRow.modelData.kind
                            ink: fileRow.modelData.kind === "word" ? root.theme.whiteColor : root.theme.workspaceInk
                        }
                    }
                    ColumnLayout
                    {
                        Layout.fillWidth: true
                        spacing: 5
                        Text
                        {
                            Layout.fillWidth: true
                            text: fileRow.modelData.name
                            color: root.theme.workspaceInk
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.fontSize + 1
                            font.weight: Font.Medium
                            elide: Text.ElideMiddle
                            textFormat: Text.PlainText
                        }
                        Text
                        {
                            Layout.fillWidth: true
                            text: fileRow.modelData.path
                            color: root.theme.mutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.fontSize - 3
                            elide: Text.ElideMiddle
                            textFormat: Text.PlainText
                        }
                    }
                    Text
                    {
                        Layout.preferredWidth: fileList.width > 620 ? 120 : 70
                        text: fileRow.modelData.modified + "\n" + fileRow.modelData.sizeText
                        color: root.theme.textSecondary
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize - 3
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }
                    AbstractButton
                    {
                        id: starButton
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        padding: 8
                        hoverEnabled: true
                        Accessible.name: fileRow.modelData.starred ? "取消收藏" : "收藏文件"
                        onClicked: root.starRequested(fileRow.modelData.path)
                        background: Rectangle
                        {
                            radius: 12
                            color: starButton.hovered ? root.theme.accentSoft : root.theme.transparentColor
                        }
                        contentItem: UiIcon
                        {
                            symbol: fileRow.modelData.starred ? "starFilled" : "star"
                            iconColor: fileRow.modelData.starred ? root.theme.accent : root.theme.mutedColor
                        }
                    }
                }
            }
        }
    }
}