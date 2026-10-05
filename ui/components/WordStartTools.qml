pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Flow
{
    id: root
    property var theme
    property string section
    property var selection
    property bool editable
    property bool formatReady
    signal formatRequested(string action, var value)
    signal commandRequested(string action)
    spacing: 6
    visible: ["clipboard", "colors", "decoration", "styles", "ruby"].indexOf(section) >= 0
    ColorDialog
    {
        id: customColor
        property string action
        title: "选择颜色"
        selectedColor: root.theme.textPrimary
        onAccepted: root.formatRequested(action, String(selectedColor))
    }

    ActionButton { visible: root.section === "ruby"; theme: root.theme; text: "按单字生成拼音"; compact: true; enabled: root.editable; onClicked: root.formatRequested("rubyAuto", true) }
    TextField { id: rubyText; visible: root.section === "ruby"; placeholderText: "选择一个字，输入校正拼音"; maximumLength: 48; enabled: root.editable }
    ActionButton { visible: root.section === "ruby"; theme: root.theme; text: "应用校正"; compact: true; enabled: root.editable && rubyText.text.length > 0; onClicked: root.formatRequested("ruby", rubyText.text) }
    ActionButton { visible: root.section === "ruby"; theme: root.theme; text: "清除拼音"; compact: true; enabled: root.editable; onClicked: root.formatRequested("rubyClear", true) }
    Text { visible: root.section === "ruby"; text: "离线字音，多音字可手工校正；注音时留出双倍行距。"; color: root.theme.textSecondary }

    Repeater
    {
        model: root.section === "clipboard" ? [{text: "复制", action: "copy"}, {text: "剪切", action: "cut"}, {text: "粘贴", action: "paste"}, {text: "仅粘贴文本", action: "pastePlain"}, {text: "全选", action: "selectAll"}, {text: "拾取格式", action: "copyFormat"}, {text: "应用格式刷", action: "pasteFormat"}]
            : root.section === "styles" ? [{text: "正文", action: "normal"}, {text: "普通（网站）", action: "web"}, {text: "默认段落字体", action: "defaultFont"}, {text: "要点", action: "points"}, {text: "强调", action: "emphasis"}, {text: "引用", action: "quote"}, {text: "页眉样式", action: "header"}, {text: "页码样式", action: "pageNumber"}]
            : []
        delegate: ActionButton
        {
            required property var modelData
            theme: root.theme; iconName: ""; compact: true; text: modelData.text
            enabled: ["copy", "selectAll", "copyFormat"].indexOf(modelData.action) >= 0 || (root.editable && (modelData.action !== "pasteFormat" || root.formatReady))
            onClicked: root.section === "styles" ? root.formatRequested("style", modelData.action) : root.commandRequested(modelData.action)
        }
    }
    ComboBox
    {
        id: target
        visible: root.section === "colors" || root.section === "decoration"
        model: root.section === "colors" ? ["文字颜色", "文字底纹", "字符边框"] : ["段落底纹", "段落方框", "段落下边框"]
    }
    Repeater
    {
        model: root.section === "colors" || root.section === "decoration" ? [root.theme.textPrimary, root.theme.accent, root.theme.spectrumCoral, root.theme.spectrumBlue, root.theme.spectrumGold, root.theme.accentSoft] : []
        delegate: Button
        {
            id: colorButton
            required property color modelData
            width: 36; height: 32; enabled: root.editable
            Accessible.name: "应用颜色 " + modelData
            contentItem: Rectangle { color: colorButton.modelData; radius: 3 }
            onClicked: root.formatRequested(root.section === "colors" ? ["color", "highlight", "characterBorder"][target.currentIndex] : ["paragraphFill", "paragraphBorder", "paragraphBottomBorder"][target.currentIndex], modelData.toString())
        }
    }
    ActionButton
    {
        visible: root.section === "colors" || root.section === "decoration"
        theme: root.theme; text: "自选颜色…"; compact: true; enabled: root.editable
        onClicked:
        {
            customColor.action = root.section === "colors" ? ["color", "highlight", "characterBorder"][target.currentIndex] : ["paragraphFill", "paragraphBorder", "paragraphBottomBorder"][target.currentIndex];
            customColor.open();
        }
    }
    ActionButton
    {
        visible: root.section === "colors" || root.section === "decoration"
        theme: root.theme; text: "清除所选颜色或边框"; iconName: ""; compact: true; enabled: root.editable
        onClicked: root.formatRequested(root.section === "colors" ? ["color", "highlight", "characterBorder"][target.currentIndex] : ["paragraphFill", "paragraphBorder", "paragraphBottomBorder"][target.currentIndex], "")
    }
}
