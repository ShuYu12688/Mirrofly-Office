pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item
{
    id: root

    property var theme
    property var contextInfo: ({ inCode: false, inTable: false, inList: false, inLink: false })
    property int selectionStart: 0
    property int selectionEnd: 0
    readonly property bool canRemoveInlineCode: !!contextInfo.inInlineCode
        && selectionStart >= contextInfo.inlineCodeStart && selectionEnd <= contextInfo.inlineCodeEnd
    property real overlayRadius: theme.windowRadius
    readonly property bool modalActive: codeDialog.visible || tableDialog.visible || headingMenu.visible || linkDialog.visible || imageDialog.visible
    signal actionRequested(string action, var options)
    signal contextRequested()

    implicitHeight: toolbarColumn.implicitHeight

    ColumnLayout
    {
        id: toolbarColumn

        width: parent.width
        spacing: 9

        Flow
        {
            Layout.fillWidth: true
            spacing: 7
            enabled: !root.contextInfo.inCode

            ActionButton
            {
                id: headingButton

                theme: root.theme
                text: root.contextInfo.headingLevel > 0 ? root.contextInfo.headingLevel + "级标题" : "标题"
                iconName: ""
                compact: true
                enabled: !root.contextInfo.inTable
                onClicked: headingMenu.open()

                Menu
                {
                    id: headingMenu
                    objectName: "markdownHeadingMenu"

                    y: headingButton.height + 5
                    width: 170
                    padding: 6
                    background: Rectangle
                    {
                        radius: root.theme.radius
                        color: root.theme.surfaceColor
                        border.color: root.theme.borderColor
                    }

                    Repeater
                    {
                        model: ["正文", "一级标题", "二级标题", "三级标题", "四级标题", "五级标题", "六级标题"]
                        MenuItem
                        {
                            required property string modelData
                            required property int index

                            text: modelData
                            objectName: "markdownHeadingLevel" + index
                            checkable: true
                            checked: index === (root.contextInfo.headingLevel || 0)
                            enabled: index === 0 || root.contextInfo.canStyleHeading !== false
                            font.family: root.theme.fontFamily
                            onTriggered: root.actionRequested(index === 0 ? "paragraph" : "heading", { headingLevel: index })
                        }
                    }
                }
            }

            Repeater
            {
                model:
                [
                    { label: "加粗", action: "bold" },
                    { label: "斜体", action: "italic" },
                    { label: "删除线", action: "strike" },
                    { label: "行内代码", action: "inlineCode" },
                    { label: "无序列表", action: "bullet" },
                    { label: "有序列表", action: "ordered" },
                    { label: "待办", action: "task" },
                    { label: "引用", action: "quote" }
                ]

                ActionButton
                {
                    required property var modelData
                    objectName: "markdownInline-" + modelData.action

                    theme: root.theme
                    text: modelData.action === "inlineCode" && root.canRemoveInlineCode
                        ? "取消行内代码" : modelData.label
                    iconName: ""
                    compact: true
                    enabled: !root.contextInfo.inTable || modelData.action === "bold" || modelData.action === "italic"
                        || modelData.action === "strike" || modelData.action === "inlineCode"
                    onClicked: root.actionRequested(modelData.action === "inlineCode" && root.canRemoveInlineCode
                        ? "removeInlineCode" : modelData.action, {})
                }
            }

            ActionButton
            {
                objectName: "markdownLinkButton"
                theme: root.theme
                text: root.contextInfo.inLink ? "编辑链接" : "链接"
                iconName: ""
                compact: true
                onClicked:
                {
                    root.contextRequested();
                    linkDialog.open();
                }
            }

            ActionButton
            {
                objectName: "markdownImageButton"
                theme: root.theme
                text: root.contextInfo.inImage ? "编辑图片" : "图片"
                iconName: ""
                compact: true
                onClicked:
                {
                    root.contextRequested();
                    imageDialog.open();
                }
            }

            ActionButton
            {
                objectName: "markdownRemoveImage"
                theme: root.theme
                text: "移除图片"
                iconName: ""
                compact: true
                visible: !!root.contextInfo.inImage
                onClicked: root.actionRequested("removeImage", {})
            }

            ActionButton
            {
                objectName: "markdownThematicBreak"
                theme: root.theme
                text: "分隔线"
                iconName: ""
                compact: true
                enabled: !!root.contextInfo.canThematicBreak
                onClicked: root.actionRequested("thematicBreak", {})
            }

            ActionButton
            {
                objectName: "markdownHardBreak"
                theme: root.theme
                text: "换行"
                iconName: ""
                compact: true
                enabled: !!root.contextInfo.canHardBreak
                Accessible.name: "段内换行，Shift加Enter"
                onClicked: root.actionRequested("hardBreak", {})
            }

            ActionButton
            {
                objectName: "markdownUnlink"
                theme: root.theme
                text: "取消链接"
                iconName: ""
                compact: true
                enabled: !!root.contextInfo.inLink
                onClicked: root.actionRequested("unlink", {})
            }

            ActionButton
            {
                theme: root.theme
                text: "代码块"
                iconName: ""
                compact: true
                enabled: !root.contextInfo.inTable
                onClicked: codeDialog.open()
            }

            ActionButton
            {
                theme: root.theme
                text: "表格"
                iconName: "grid"
                compact: true
                enabled: !root.contextInfo.inTable
                onClicked: tableDialog.open()
            }
        }

        Flow
        {
            Layout.fillWidth: true
            spacing: 7

            Text
            {
                text: "引用层级"
                height: quoteLevel.height
                verticalAlignment: Text.AlignVCenter
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }

            ComboBox
            {
                id: quoteLevel
                objectName: "markdownQuoteLevel"
                width: root.theme.markdownQuoteLevelWidth
                readonly property int minimumLevel: Number(root.contextInfo.quoteMinimum || 0)
                model: ["无引用", "一级引用", "二级引用", "三级引用", "四级引用",
                    "五级引用", "六级引用", "七级引用", "八级引用"].slice(minimumLevel)
                currentIndex: Math.max(0, Number(root.contextInfo.quoteLevel || 0) - minimumLevel)
                onActivated: root.actionRequested("quoteSet", { quoteLevel: currentIndex + minimumLevel })
                Accessible.name: "当前引用容器层级"
            }
        }

        Flow
        {
            Layout.fillWidth: true
            visible: !!root.contextInfo.inList && !root.contextInfo.inCode && !root.contextInfo.inTable
            spacing: 7

            Text
            {
                text: "列表第 " + Number(root.contextInfo.listLevel || 1) + " 层"
                height: listIndentButton.height
                verticalAlignment: Text.AlignVCenter
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }

            ActionButton
            {
                id: listIndentButton
                objectName: "markdownListIndent"
                theme: root.theme
                text: "增加层级"
                iconName: ""
                compact: true
                enabled: !!root.contextInfo.canIndentList
                onClicked: root.actionRequested("listIndent", {})
            }

            ActionButton
            {
                objectName: "markdownListOutdent"
                theme: root.theme
                text: "减少层级"
                iconName: ""
                compact: true
                enabled: !!root.contextInfo.canOutdentList
                onClicked: root.actionRequested("listOutdent", {})
            }

            ActionButton
            {
                objectName: "markdownTaskState"
                theme: root.theme
                text: root.contextInfo.taskChecked ? "标记未完成" : "标记完成"
                iconName: "check"
                compact: true
                visible: !!root.contextInfo.taskItem
                onClicked: root.actionRequested("taskSet", { checked: !root.contextInfo.taskChecked })
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            visible: root.contextInfo.inCode
            spacing: 10

            Text
            {
                text: "代码语言"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }

            ComboBox
            {
                id: currentLanguage

                Layout.preferredWidth: 150
                model: ["text", "cpp", "c", "python", "java", "javascript", "sql", "bash", "json", "html", "css"]
                currentIndex: Math.max(0, model.indexOf(root.contextInfo.codeLanguage || "text"))
                onActivated: root.actionRequested("codeLanguage", { language: currentText })
                Accessible.name: "当前代码块语言"
            }

            ActionButton
            {
                theme: root.theme
                text: "退出代码块"
                iconName: "arrow"
                compact: true
                onClicked: root.actionRequested("exitCode", {})
            }

            Item
            {
                Layout.fillWidth: true
            }
        }

        Flow
        {
            Layout.fillWidth: true
            visible: root.contextInfo.inTable && !root.contextInfo.inCode
            spacing: 7

            Text
            {
                text: "第 " + (Number(root.contextInfo.tableColumn || 0) + 1) + " 列对齐"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
                height: columnAlignment.height
                verticalAlignment: Text.AlignVCenter
            }

            ComboBox
            {
                id: columnAlignment
                objectName: "markdownColumnAlignment"
                width: root.theme.markdownColumnAlignmentWidth
                readonly property var alignmentValues: ["default", "left", "center", "right"]
                model: ["默认", "左对齐", "居中", "右对齐"]
                currentIndex: Math.max(0, alignmentValues.indexOf(root.contextInfo.tableAlignment))
                onActivated: root.actionRequested("tableAlign", { alignment: alignmentValues[currentIndex] })
                Accessible.name: "当前表格列对齐"
            }

            Repeater
            {
                model:
                [
                    { label: "加一行", action: "rowAdd" },
                    { label: "删除当前行", action: "rowRemove" },
                    { label: "加一列", action: "columnAdd" },
                    { label: "删除当前列", action: "columnRemove" },
                    { label: "退出表格", action: "exitTable" }
                ]

                ActionButton
                {
                    required property var modelData
                    objectName: "markdownInline-" + modelData.action

                    theme: root.theme
                    text: modelData.label
                    iconName: ""
                    compact: true
                    enabled: (modelData.action !== "rowRemove" || root.contextInfo.tableRows > 2)
                        && (modelData.action !== "columnRemove" || root.contextInfo.tableColumns > 1)
                        && (modelData.action !== "rowAdd" || root.contextInfo.tableRows < 128)
                        && (modelData.action !== "columnAdd" || root.contextInfo.tableColumns < 32)
                    onClicked: root.actionRequested(modelData.action, {})
                }
            }
        }

        Text
        {
            Layout.fillWidth: true
            text: root.contextInfo.inCode ? "直接在整块代码区域内输入，完成后可退出代码块。"
                : (root.contextInfo.inTable ? "点击单元格即可编辑。Enter 到下一行，Tab 切换单元格；末尾自动添加一行。"
                    : root.contextInfo.inList ? "Tab 增加列表层级，Shift+Tab 减少层级；子项跟随父项移动。"
                    : "直接编辑排版后的内容；右侧大纲可跳转到标题。")
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 2
            wrapMode: Text.Wrap
        }
    }

    MarkdownImageDialog
    {
        id: imageDialog
        theme: root.theme
        contextInfo: root.contextInfo
        overlayRadius: root.overlayRadius
        onImageApplied: function(options) { root.actionRequested("image", options); }
    }

    RoundedDialog
    {
        id: linkDialog
        objectName: "markdownLinkDialog"

        theme: root.theme
        title: "设置链接"
        overlayRadius: root.overlayRadius
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 80 : 440)
        modal: true
        closePolicy: Popup.CloseOnEscape
        onOpened:
        {
            linkAddress.text = String(root.contextInfo.linkUrl || "");
            linkTitle.text = String(root.contextInfo.linkTitle || "");
        }

        contentItem: ColumnLayout
        {
            spacing: 18

            Text
            {
                text: "链接地址"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 1
            }

            TextField
            {
                id: linkAddress
                objectName: "markdownLinkAddress"

                Layout.fillWidth: true
                placeholderText: "https://…、相对路径或 #锚点"
                selectByMouse: true
                Accessible.name: "链接地址"
            }

            Text
            {
                text: "链接提示（可选）"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 1
            }
            TextField
            {
                id: linkTitle
                objectName: "markdownLinkTitle"
                Layout.fillWidth: true
                selectByMouse: true
                Accessible.name: "链接提示"
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 10

                Item
                {
                    Layout.fillWidth: true
                }

                ActionButton
                {
                    theme: root.theme
                    text: "取消"
                    iconName: ""
                    onClicked: linkDialog.close()
                }

                ActionButton
                {
                    theme: root.theme
                    text: "应用链接"
                    objectName: "markdownLinkApply"
                    iconName: "check"
                    primary: true
                    enabled: linkAddress.text.trim().length > 0
                    onClicked:
                    {
                        linkDialog.close();
                        root.actionRequested("link", { url: linkAddress.text.trim(), title: linkTitle.text });
                    }
                }
            }
        }
    }

    RoundedDialog
    {
        id: codeDialog

        theme: root.theme
        title: "插入代码块"
        overlayRadius: root.overlayRadius
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 80 : 440)
        modal: true
        closePolicy: Popup.CloseOnEscape

        contentItem: ColumnLayout
        {
            spacing: 18

            Text
            {
                Layout.fillWidth: true
                text: "选中的内容会放进代码块；没有选中内容时，插入空代码块。"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 1
                wrapMode: Text.Wrap
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 16

                Text
                {
                    text: "代码语言"
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily
                }

                ComboBox
                {
                    id: languageChoice

                    Layout.fillWidth: true
                    model: ["text", "cpp", "c", "python", "java", "javascript", "sql", "bash", "json", "html", "css"]
                    font.family: root.theme.editorFontFamily
                    Accessible.name: "代码语言"
                }
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 10

                Item
                {
                    Layout.fillWidth: true
                }

                ActionButton
                {
                    theme: root.theme
                    text: "取消"
                    iconName: ""
                    onClicked: codeDialog.close()
                }

                ActionButton
                {
                    theme: root.theme
                    text: "插入代码块"
                    iconName: "plus"
                    primary: true
                    onClicked:
                    {
                        codeDialog.close();
                        root.actionRequested("code", { language: languageChoice.currentText });
                    }
                }
            }
        }
    }

    RoundedDialog
    {
        id: tableDialog

        theme: root.theme
        title: "插入表格"
        overlayRadius: root.overlayRadius
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 80 : 440)
        modal: true
        closePolicy: Popup.CloseOnEscape

        contentItem: ColumnLayout
        {
            spacing: 18

            Text
            {
                Layout.fillWidth: true
                text: "用表格整理知识点、实验结果或复习计划。行数不含表头。"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 1
                wrapMode: Text.Wrap
            }

            GridLayout
            {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 12
                columnSpacing: 16

                Text
                {
                    text: "内容行数"
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily
                }

                SpinBox
                {
                    id: tableRows

                    Layout.fillWidth: true
                    from: 1
                    to: 12
                    value: 3
                    Accessible.name: "表格内容行数"
                }

                Text
                {
                    text: "列数"
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily
                }

                SpinBox
                {
                    id: tableColumns

                    Layout.fillWidth: true
                    from: 2
                    to: 6
                    value: 3
                    Accessible.name: "表格列数"
                }
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 10

                Item
                {
                    Layout.fillWidth: true
                }

                ActionButton
                {
                    theme: root.theme
                    text: "取消"
                    iconName: ""
                    onClicked: tableDialog.close()
                }

                ActionButton
                {
                    theme: root.theme
                    text: "插入表格"
                    iconName: "plus"
                    primary: true
                    onClicked:
                    {
                        tableDialog.close();
                        root.actionRequested("table", { rows: tableRows.value, columns: tableColumns.value });
                    }
                }
            }
        }
    }
}
