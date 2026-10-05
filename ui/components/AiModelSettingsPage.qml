pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item
{
    id: root
    objectName: "aiModelSettingsPage"
    required property var theme
    required property var agent
    property string islandStatus: ""
    signal openIslandRequested()
    implicitHeight: content.implicitHeight

    function applyConfiguration()
    {
        if (!root.agent.configure(addressField.text, modelField.text, keyField.text,
            root.agent.thinkingEffort))
            return false;
        keyField.clear();
        return true;
    }
    ColumnLayout
    {
        id: content
        width: Math.min(parent.width, 740)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 16
        DashboardSurface
        {
            Layout.fillWidth: true
            Layout.preferredHeight: form.implicitHeight + 48
            theme: root.theme
            ColumnLayout
            {
                id: form
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12
                RowLayout
                {
                    Layout.fillWidth: true
                    Text
                    {
                        Layout.fillWidth: true
                        text: "模型连接"
                        color: root.theme.workspaceInk
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize + 5
                        font.weight: Font.DemiBold
                    }
                    Text
                    {
                        text: root.agent.configured ? "● 已配置" : "○ 待连接"
                        color: root.agent.configured ? root.theme.sheetsAccent : root.theme.textSecondary
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize - 1
                    }
                }
                Text
                {
                    Layout.fillWidth: true
                    text: "默认 DeepSeek，也可以连接兼容 Chat Completions 的服务。"
                    color: root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                    wrapMode: Text.Wrap
                }
                Label
                {
                    Layout.topMargin: 8
                    text: "接口地址"
                    color: root.theme.workspaceInk
                    font.family: root.theme.fontFamily
                }
                RoundedField
                {
                    id: addressField
                    objectName: "aiAddressField"
                    Layout.fillWidth: true
                    theme: root.theme
                    text: root.agent.modelAddress
                    placeholderText: "https://api.deepseek.com"
                    enabled: !root.agent.busy
                    Accessible.name: "AI 模型接口地址"
                }
                Label
                {
                    text: "模型名称"
                    color: root.theme.workspaceInk
                    font.family: root.theme.fontFamily
                }
                RoundedField
                {
                    id: modelField
                    objectName: "aiModelField"
                    Layout.fillWidth: true
                    theme: root.theme
                    text: root.agent.modelName
                    placeholderText: "deepseek-flash"
                    enabled: !root.agent.busy
                    Accessible.name: "AI 模型名称"
                }
                Label
                {
                    text: "访问密钥"
                    color: root.theme.workspaceInk
                    font.family: root.theme.fontFamily
                }
                RoundedField
                {
                    id: keyField
                    objectName: "aiKeyField"
                    Layout.fillWidth: true
                    theme: root.theme
                    placeholderText: root.agent.configured ? "已保存，留空沿用；更换服务需输入新密钥" : "输入访问密钥"
                    echoMode: TextInput.Password
                    enabled: !root.agent.busy
                    Accessible.name: "AI 模型访问密钥"
                }
                RowLayout
                {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 10
                    ActionButton
                    {
                        objectName: "aiSaveConfiguration"
                        theme: root.theme
                        text: "保存"
                        primary: true
                        enabled: !root.agent.busy && (root.agent.configured || keyField.text.length > 0)
                        onClicked: root.applyConfiguration()
                    }
                    ActionButton
                    {
                        objectName: "aiTestConnection"
                        theme: root.theme
                        text: "测试连接"
                        enabled: !root.agent.busy && (root.agent.configured || keyField.text.length > 0)
                        onClicked: if (root.applyConfiguration()) root.agent.testConnection()
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton
                    {
                        theme: root.theme
                        text: "打开灵动岛"
                        enabled: root.agent.configured
                        onClicked: root.openIslandRequested()
                    }
                }
                Text
                {
                    Layout.fillWidth: true
                    text: root.agent.status
                    color: root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }
        Text
        {
            Layout.fillWidth: true
            text: "配置自动保存 · 思考强度在灵动岛顶部调整"
            color: root.theme.mutedColor
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
            horizontalAlignment: Text.AlignHCenter
        }
        Text
        {
            Layout.fillWidth: true
            visible: root.islandStatus.length > 0 && !root.agent.configured
            text: root.islandStatus
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
            wrapMode: Text.Wrap
        }
    }
}