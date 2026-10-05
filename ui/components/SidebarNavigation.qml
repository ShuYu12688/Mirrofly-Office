pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item
{
    id: root

    property var theme
    property string currentRoute: "home"
    property Item selectedButton: null
    property bool brandVisible: true
    property real controlsOpacity: 1
    readonly property Item brandMark: brandImage

    signal routeRequested(string route)
    signal replayRequested()

    implicitWidth: 224
    opacity: root.theme.surfaceOpacity
    onCurrentRouteChanged: updateSelectedButton()
    Component.onCompleted: updateSelectedButton()

    function updateSelectedButton()
    {
        if (!navigationRepeater)
        {
            selectedButton = null;
            return;
        }
        for (let index = 0; index < navigationRepeater.count; index++)
        {
            const button = navigationRepeater.itemAt(index);
            if (button && navigationRepeater.model[index].route === currentRoute)
            {
                selectedButton = button;
                return;
            }
        }
        selectedButton = null;
    }

    NavNotch
    {
        id: sidebarSurface
        objectName: "sidebarSurface"
        opacity: root.controlsOpacity

        anchors.fill: parent
        fillColor: root.theme.navigationBackground
        cornerRadius: root.theme.radius * 1.72
        shoulderRadius: root.theme.radius * 1.43
        leadingRadius: root.theme.radius * 1.72
        cutoutEnabled: root.selectedButton !== null
        cutoutX: sidebarContent.x + navigationList.x
            + (root.selectedButton ? root.selectedButton.x : 0)
        cutoutY: sidebarContent.y + navigationList.y
            + (root.selectedButton ? root.selectedButton.y : 0) - shoulderRadius
        cutoutWidth: width - cutoutX
        cutoutHeight: (root.selectedButton ? root.selectedButton.height : 0) + shoulderRadius * 2
    }

    ColumnLayout
    {
        id: sidebarContent
        opacity: root.controlsOpacity

        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 28
        anchors.bottomMargin: 20
        spacing: 0

        RowLayout
        {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 12

            Rectangle
            {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 13
                color: root.theme.surfaceColor

                Image
                {
                    id: brandImage
                    anchors.centerIn: parent
                    width: 36
                    height: 36
                    source: "../../assets/mirrorfly-mark.svg"
                    sourceSize.width: 72
                    sourceSize.height: 72
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    visible: root.brandVisible
                }
            }

            ColumnLayout
            {
                Layout.fillWidth: true
                spacing: 2

                Text
                {
                    Layout.fillWidth: true
                    text: "Mirrorfly"
                    color: root.theme.navigationText
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize + 6
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text
                {
                    Layout.fillWidth: true
                    text: "Office"
                    color: root.theme.navigationMuted
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                    elide: Text.ElideRight
                }
            }
        }

        Text
        {
            Layout.topMargin: 38
            Layout.bottomMargin: 14
            Layout.leftMargin: 14
            text: "工作空间"
            color: root.theme.navigationMuted
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 3
        }

        ColumnLayout
        {
            id: navigationList

            Layout.fillWidth: true
            spacing: 8

            Repeater
            {
                id: navigationRepeater

                onItemAdded: root.updateSelectedButton()
                model:
                [
                    { "route": "home", "label": "首页", "description": "时间 · 用量 · 专注", "icon": "home" },
                    { "route": "create", "label": "开始创作", "description": "文字 · 表格 · 演示", "icon": "plus" },
                    { "route": "recent", "label": "最近文件", "description": "搜索与接续工作", "icon": "clock" },
                    { "route": "ai", "label": "AI 模型", "description": "连接你的模型", "icon": "ai" }
                ]

                AbstractButton
                {
                    id: navigationButton

                    required property var modelData
                    readonly property bool selected: root.currentRoute === modelData.route
                    readonly property bool filled: !selected && (hovered || down)

                    Layout.fillWidth: true
                    implicitHeight: Math.max(76, contentItem.implicitHeight + 24)
                    leftPadding: 14
                    rightPadding: 12
                    topPadding: 12
                    bottomPadding: 12
                    hoverEnabled: true
                    activeFocusOnTab: true
                    checkable: true
                    autoExclusive: true
                    checked: selected
                    Accessible.name: modelData.label
                    Accessible.description: modelData.description
                    onClicked: root.routeRequested(modelData.route)

                    HoverHandler
                    {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Item
                    {
                        SpectrumStroke
                        {
                            anchors.fill: parent
                            theme: root.theme
                            radius: root.theme.radius * 1.14
                            opacity: navigationButton.hovered || navigationButton.down ? 1 : 0
                            visible: !navigationButton.selected
                        }

                        Rectangle
                        {
                            anchors.fill: parent
                            anchors.margins: 3
                            radius: Math.max(0, root.theme.radius * 1.14 - 3)
                            color: root.theme.transparentColor
                            border.width: navigationButton.visualFocus ? 2 : 0
                            border.color: navigationButton.selected
                                ? root.theme.accent : root.theme.navigationText
                        }
                    }

                    contentItem: RowLayout
                    {
                        spacing: 12

                        UiIcon
                        {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            symbol: navigationButton.modelData.icon
                            iconColor: navigationButton.selected ? root.theme.accent
                                : (navigationButton.filled ? root.theme.onAccent : root.theme.navigationText)
                        }

                        ColumnLayout
                        {
                            Layout.fillWidth: true
                            spacing: 5

                            Text
                            {
                                Layout.fillWidth: true
                                text: navigationButton.modelData.label
                                color: navigationButton.selected ? root.theme.textPrimary
                                    : (navigationButton.filled ? root.theme.onAccent : root.theme.navigationText)
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.fontSize - 1
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }

                            Text
                            {
                                Layout.fillWidth: true
                                text: navigationButton.modelData.description
                                color: navigationButton.selected ? root.theme.textSecondary
                                    : (navigationButton.filled ? root.theme.onAccentMuted : root.theme.navigationMuted)
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.fontSize - 3
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }

        Item
        {
            Layout.fillHeight: true
            Layout.minimumHeight: 24
        }

        AbstractButton
        {
            id: replayButton

            Layout.fillWidth: true
            implicitHeight: Math.max(52, contentItem.implicitHeight + 20)
            leftPadding: 14
            rightPadding: 12
            topPadding: 10
            bottomPadding: 10
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.name: "重播启动画面"
            onClicked: root.replayRequested()

            HoverHandler
            {
                cursorShape: Qt.PointingHandCursor
            }

            background: Rectangle
            {
                radius: root.theme.radius
                color: replayButton.hovered || replayButton.down
                    ? root.theme.navigationHover : root.theme.transparentColor
                border.width: replayButton.visualFocus ? 2 : 0
                border.color: root.theme.navigationText

                Behavior on color
                {
                    ColorAnimation
                    {
                        duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                    }
                }
            }

            contentItem: RowLayout
            {
                spacing: 12

                UiIcon
                {
                    Layout.preferredWidth: 19
                    Layout.preferredHeight: 19
                    symbol: "play"
                    iconColor: replayButton.hovered || replayButton.down
                        ? root.theme.onAccent : root.theme.navigationMuted
                }

                Text
                {
                    Layout.fillWidth: true
                    text: "重播启动画面"
                    color: replayButton.hovered || replayButton.down
                        ? root.theme.onAccent : root.theme.navigationMuted
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                    wrapMode: Text.Wrap
                }
            }
        }

        RowLayout
        {
            Layout.leftMargin: 14
            Layout.topMargin: 17
            Layout.fillWidth: true
            spacing: 9

            Image
            {
                Layout.preferredWidth: 19
                Layout.preferredHeight: 19
                source: "../../Logo/logo1.svg"
                sourceSize.width: 38
                sourceSize.height: 38
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Text
            {
                Layout.fillWidth: true
                text: "开发者：舒宇--镜蝶科技研发部"
                color: root.theme.navigationMuted
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 4
                wrapMode: Text.Wrap
            }
        }
    }
}
