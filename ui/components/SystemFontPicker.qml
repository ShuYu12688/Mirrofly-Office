pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item
{
    id: root
    objectName: "systemFontPicker"

    property var theme
    property var chineseFontFamilies: []
    property var systemFontFamilies: []
    property bool allowLocalEnumeration: true
    property bool chineseOnly: false
    property string family: ""
    readonly property var families: systemFontFamilies.length > 0 ? systemFontFamilies
        : allowLocalEnumeration
            ? Qt.fontFamilies().filter(function(name) { return name.charAt(0) !== "@"; }) : []
    signal familySelected(string family)

    implicitWidth: 240
    implicitHeight: 36

    ActionButton
    {
        anchors.fill: parent
        theme: root.theme
        text: root.family.length > 0 ? root.family : "选择系统字体"
        iconName: "search"
        compact: true
        onClicked: chooser.open()
    }

    Popup
    {
        id: chooser

        width: Math.max(320, Math.min(400, root.width + 100))
        height: 420
        y: root.height + 6
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: search.forceActiveFocus()

        background: Rectangle
        {
            color: root.theme.surfaceColor
            radius: root.theme.radius
            border.color: root.theme.borderColor

            SpectrumStroke
            {
                anchors.fill: parent
                theme: root.theme
            }
        }

        contentItem: ColumnLayout
        {
            spacing: 10

            TextField
            {
                id: search

                Layout.fillWidth: true
                placeholderText: "搜索本机字体名称"
                Accessible.name: "搜索系统字体"
                selectByMouse: true
                onAccepted:
                {
                    if (fontList.count > 0)
                    {
                        root.familySelected(fontList.model[Math.max(0, fontList.currentIndex)]);
                        chooser.close();
                    }
                }
                Keys.onDownPressed: fontList.forceActiveFocus()
            }

            Label
            {
                text: "本机 " + root.families.length + " 种字体 · 搜索结果 " + fontList.count
                color: root.theme.textSecondary
                font.pixelSize: root.theme.fontSize - 2
            }

            RowLayout
            {
                ActionButton { theme: root.theme; text: "全部字体"; compact: true; primary: !root.chineseOnly; onClicked: root.chineseOnly = false }
                ActionButton { theme: root.theme; text: "支持中文"; compact: true; primary: root.chineseOnly; enabled: root.chineseFontFamilies.length > 0; onClicked: root.chineseOnly = true }
            }

            ListView
            {
                id: fontList

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                reuseItems: true
                model: root.families.filter(function(name)
                {
                    return (!root.chineseOnly || root.chineseFontFamilies.indexOf(name) >= 0)
                        && name.toLocaleLowerCase().indexOf(search.text.trim().toLocaleLowerCase()) !== -1;
                })
                currentIndex: 0
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar {}
                Keys.onReturnPressed:
                {
                    if (currentIndex >= 0 && currentIndex < count)
                    {
                        root.familySelected(model[currentIndex]);
                        chooser.close();
                    }
                }

                delegate: ItemDelegate
                {
                    id: fontOption

                    required property string modelData
                    required property int index

                    width: fontList.width
                    height: 43
                    highlighted: ListView.isCurrentItem
                    text: modelData
                    onClicked:
                    {
                        root.familySelected(modelData);
                        chooser.close();
                    }
                    contentItem: Text
                    {
                        text: fontOption.modelData
                        textFormat: Text.PlainText
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize + 1
                        color: root.theme.textPrimary
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }

            Label
            {
                Layout.fillWidth: true
                text: "演示文字  Aa 0123"
                font.family: fontList.model[fontList.currentIndex] || root.theme.fontFamily
                font.pixelSize: root.theme.fontSize + 3
                color: root.theme.textPrimary
                elide: Text.ElideRight
            }

            Label
            {
                Layout.fillWidth: true
                text: "中文字体需包含中文字形；英文字体的中文会自动回退。"
                wrapMode: Text.Wrap
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }
        }
    }
}
