pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mirrorfly.Native
import "components"

FocusScope
{
    id: root
    objectName: "WordPage"
    property real zoom: 1
    property real rasterZoom: 1
    signal zoomRequested(real value)

    onZoomChanged:
    {
        const bounded = Math.max(0.25, Math.min(4, root.zoom));
        if (bounded < root.rasterZoom)
        {
            wordRasterTimer.stop();
            root.rasterZoom = bounded;
        }
        else
        {
            wordRasterTimer.restart();
        }
    }

    Timer
    {
        id: wordRasterTimer
        interval: root.theme.motionEnabled ? 160 : 0
        repeat: false
        onTriggered: root.rasterZoom = Math.max(0.25, Math.min(4, root.zoom))
    }
    WheelHandler
    {
        acceptedModifiers: Qt.ControlModifier
        onWheel: function(event)
        {
            root.zoomRequested(Math.max(0.25, Math.min(4, root.zoom + (event.angleDelta.y > 0 ? 0.1 : -0.1))));
            event.accepted = true;
        }
    }
    property var theme
    property real chromeInset: 32
    property string documentName: "未命名.docx"
    property bool modified: false
    property bool busy: false
    property bool readOnly: false
    property int revision: 0
    property string message: ""
    property var statistics: ({characters: 0, paragraphs: 1, canUndo: false, canRedo: false})
    property var chineseFonts: []
    property var systemFontFamilies: []
    property var selection: ({family: "Microsoft YaHei", size: 12, bold: false, italic: false, underline: false, heading: 0, list: 0, listLevel: 0, leftIndent: 0, firstLineIndent: 0, spaceBefore: 0, spaceAfter: 5})
    property string group: ""
    property string section: ""
    readonly property bool modalActive: copyDialog.opened || supportDialog.opened || textMenu.opened
    readonly property var automationSelection: ({start: editor.selectionStart, end: editor.selectionEnd, cursor: editor.cursorPosition, composing: editor.inputMethodComposing})
    readonly property bool editable: !busy && !readOnly
    property bool editorReady: false
    property bool changingEditorAccess: false

    property bool formatReady: false
    signal copySelectionRequested(int start, int end, bool cut)
    signal copyFormatRequested(int position)
    signal pasteFormatRequested(int start, int end)
    signal replaceAllRequested(string query, string replacement)
    signal loadRequested(var document)
    signal inspectRequested(int position)
    signal formatRequested(int start, int end, string action, var value)
    signal findRequested(string query, int from, bool backward)
    signal replaceRequested(int start, int end, string expected, string replacement)
    signal pasteRequested(int start, int end)
    signal pastePlainRequested(int start, int end)
    signal paragraphRequested(int start, int end)
    signal templateRequested(int position, string kind)
    signal homeRequested()
    signal openRequested()
    signal newRequested()
    signal saveRequested()
    signal pdfRequested()
    signal saveAsRequested()
    signal editableCopyRequested()
    signal undoRequested()
    signal redoRequested()

    function focusEditor() { if (visible && !busy) editor.forceActiveFocus(); }
    function refreshSelection()
    {
        if (changingEditorAccess) return;
        inspectRequested(editor.selectionStart < editor.selectionEnd ? editor.selectionStart + 1 : editor.cursorPosition);
    }
    function syncEditorAccess()
    {
        if (!editorReady || editor.readOnly === !editable) return;
        const position = editor.cursorPosition;
        const anchor = position === editor.selectionStart ? editor.selectionEnd : editor.selectionStart;
        const top = scroll.contentY;
        // Qt 6.8 moves the cursor to the document end when readOnly changes.
        changingEditorAccess = true;
        editor.readOnly = !editable;
        editor.select(anchor, position);
        changingEditorAccess = false;
        scroll.contentY = top;
        refreshSelection();
    }
    onEditableChanged: syncEditorAccess()
    function selectMatch(match)
    {
        if (match.start === undefined) return;
        editor.select(match.start, match.end);
        editor.forceActiveFocus();
        revealCursor();
    }
    function applyFormat(action, value)
    {
        applyToolFormat(action, value, false);
    }
    function applyToolFormat(action, value, keepFocus)
    {
        formatRequested(editor.selectionStart, editor.selectionEnd, action, value);
        if (!keepFocus) editor.forceActiveFocus();
        refreshSelection();
        syncTypingFormat();
    }
    function syncTypingFormat()
    {
        if (root.editable && selection.emptyParagraph && selection.font !== undefined)
            editor.cursorSelection.font = selection.font;
    }
    function finishParagraph(position)
    {
        if (position < 0) return;
        editor.cursorPosition = position;
        refreshSelection();
        syncTypingFormat();
    }
    function revealCursor()
    {
        const y = paper.y + (editor.y + editor.cursorRectangle.y) * root.zoom;
        if (y < scroll.contentY + 20) scroll.contentY = Math.max(0, y - 20);
        else if (y + 40 > scroll.contentY + scroll.height)
            scroll.contentY = Math.min(Math.max(0, scroll.contentHeight - scroll.height), y + 40 - scroll.height);
    }
    function load()
    {
        root.loadRequested(editor.textDocument);
        editor.cursorPosition = 0;
        scroll.contentY = 0;
        refreshSelection();
    }
    onRevisionChanged: Qt.callLater(root.load)
    onGroupChanged: section = ""
    Component.onCompleted: Qt.callLater(root.load)

    Rectangle { anchors.fill: parent; color: root.theme.backgroundColor }

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 24
        anchors.topMargin: root.chromeInset + 12
        spacing: 12

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 14
            ActionButton { theme: root.theme; text: "首页"; iconName: "home"; compact: true; enabled: !root.busy; onClicked: root.homeRequested() }
            ColumnLayout
            {
                Layout.fillWidth: true
                spacing: 2
                Text { Layout.fillWidth: true; text: root.documentName + (root.modified ? " · 未保存" : ""); elide: Text.ElideMiddle; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize + 5 }
                Text { text: root.readOnly ? "Word · 正文预览" : "Word · 专注书写"; color: root.theme.textSecondary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize - 1 }
            }
            ActionButton { theme: root.theme; text: "打开"; iconName: "folder"; compact: true; enabled: !root.busy; onClicked: root.openRequested() }
            ActionButton { theme: root.theme; text: root.readOnly ? "创建可编辑副本" : "保存"; iconName: "check"; primary: true; compact: true; enabled: !root.busy; onClicked: root.readOnly ? copyDialog.open() : root.saveRequested() }
        }

        Rectangle
        {
            Layout.fillWidth: true
            implicitHeight: tools.implicitHeight + 20
            radius: root.theme.radius
            color: root.theme.surfaceColor
            border.color: root.theme.borderColor

            ColumnLayout
            {
                id: tools
                x: 12; y: 10; width: parent.width - 24
                spacing: 8
                RowLayout
                {
                    Layout.fillWidth: true
                    Repeater
                    {
                        model: [{key: "write", label: "文字"}, {key: "paragraph", label: "段落"}, {key: "templates", label: "实用模板"}, {key: "review", label: "查找与修改"}, {key: "file", label: "文件"}]
                        delegate: ActionButton
                        {
                            required property var modelData
                            theme: root.theme; text: modelData.label; iconName: ""; compact: true
                            primary: root.group === modelData.key
                            enabled: !root.busy
                            onClicked: root.group = root.group === modelData.key ? "" : modelData.key
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: root.readOnly ? "原件保持只读" : "选中文字，或设置当前段落"; color: root.theme.textSecondary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize - 1 }
                }
                Item
                {
                    id: subFrame
                    Layout.fillWidth: true
                    implicitHeight: subTools.implicitHeight + 14
                    visible: root.group.length > 0
                    RowLayout
                    {
                        id: subTools
                        x: 9; y: 7; width: parent.width - 18
                        Repeater
                        {
                            model: root.group === "write" ? [{key: "font", label: "字体与字号"}, {key: "emphasis", label: "强调样式"}, {key: "ruby", label: "拼音指南"}, {key: "colors", label: "颜色"}, {key: "clipboard", label: "剪贴板"}, {key: "styles", label: "样式预设"}]
                                : root.group === "paragraph" ? [{key: "heading", label: "标题层级"}, {key: "list", label: "列表"}, {key: "align", label: "对齐与行距"}, {key: "indent", label: "缩进与段距"}, {key: "tabs", label: "制表位"}, {key: "sort", label: "段落排序"}, {key: "decoration", label: "底纹与边框"}, {key: "cell", label: "单元格样式"}]
                                : root.group === "templates" ? [{key: "templates", label: "插入结构"}]
                                : root.group === "review" ? [{key: "find", label: "查找与替换"}, {key: "history", label: "撤销与重做"}]
                                : [{key: "document", label: "新建与另存"}, {key: "support", label: "支持范围"}]
                            delegate: ActionButton
                            {
                                required property var modelData
                                theme: root.theme; text: modelData.label; iconName: "arrow"; compact: true
                                primary: root.section === modelData.key
                                onClicked:
                                {
                                    root.section = root.section === modelData.key ? "" : modelData.key;
                                    if (modelData.key === "support") supportDialog.open();
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
                Item
                {
                    id: parameters
                    Layout.fillWidth: true
                    visible: root.section.length > 0 && root.section !== "support"
                    implicitHeight: parameterStack.implicitHeight + 16
                    ColumnLayout
                    {
                        id: parameterStack
                        WordStartTools
                        {
                            Layout.fillWidth: true
                            theme: root.theme; section: root.section; selection: root.selection
                            editable: root.editable; formatReady: root.formatReady
                            onFormatRequested: function(action, value) { root.applyFormat(action, value); }
                            onCommandRequested: function(action)
                            {
                                if (action === "copy" || action === "cut") root.copySelectionRequested(editor.selectionStart, editor.selectionEnd, action === "cut");
                                else if (action === "paste") root.pasteRequested(editor.selectionStart, editor.selectionEnd);
                                else if (action === "pastePlain") root.pastePlainRequested(editor.selectionStart, editor.selectionEnd);
                                else if (action === "selectAll") editor.selectAll();
                                else if (action === "copyFormat") root.copyFormatRequested(editor.selectionStart < editor.selectionEnd ? editor.selectionStart + 1 : editor.cursorPosition);
                                else if (action === "pasteFormat") root.pasteFormatRequested(editor.selectionStart, editor.selectionEnd);
                                root.focusEditor(); root.refreshSelection();
                            }
                        }
                        x: 10; y: 8; width: parent.width - 20
                        Flow
                        {
                            Layout.fillWidth: true
                            spacing: root.theme.spacing
                            visible: root.section === "font"
                            enabled: root.editable
                            SystemFontPicker { theme: root.theme; family: root.selection.family || ""; chineseFontFamilies: root.chineseFonts; systemFontFamilies: root.systemFontFamilies; onFamilySelected: function(family) { root.applyFormat("font", family); } }
                            ComboBox
                            {
                                readonly property var pointSizes: [0, 42, 36, 26, 24, 22, 18, 16, 15, 14, 12, 10.5, 9, 7.5, 6.5]
                                model: ["中文字号", "初号", "小初", "一号", "小一", "二号", "小二", "三号", "小三", "四号", "小四", "五号", "小五", "六号", "小六"]
                                currentIndex: Math.max(0, pointSizes.indexOf(root.selection.size))
                                onActivated: if (currentIndex > 0) root.applyFormat("size", pointSizes[currentIndex]);
                            }
                            ActionButton { theme: root.theme; text: "增大"; compact: true; onClicked: root.applyFormat("grow", 1) }
                            ActionButton { theme: root.theme; text: "减小"; compact: true; onClicked: root.applyFormat("shrink", 1) }
                            Text { text: "字号（pt）"; color: root.theme.textSecondary }
                            SpinBox
                            {
                                objectName: "wordFontSize"
                                from: 60; to: 960; stepSize: 5; editable: true
                                value: Math.round((root.selection.size || 12) * 10)
                                validator: DoubleValidator { bottom: 6; top: 96; decimals: 1 }
                                textFromValue: function(value, locale) { return Number(value / 10).toLocaleString(locale, "f", 1); }
                                valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10); }
                                onValueModified: root.applyToolFormat("size", value / 10, true)
                            }
                        }
                        RowLayout
                        {
                            visible: root.section === "emphasis"
                            enabled: root.editable
                            ActionButton { iconName: ""; theme: root.theme; text: "加粗"; compact: true; primary: root.selection.bold || false; onClicked: root.applyFormat("bold", !root.selection.bold) }
                            ActionButton { iconName: ""; theme: root.theme; text: "斜体"; compact: true; primary: root.selection.italic || false; onClicked: root.applyFormat("italic", !root.selection.italic) }
                            ActionButton { iconName: ""; theme: root.theme; text: "下划线"; compact: true; primary: root.selection.underline || false; onClicked: root.applyFormat("underline", !root.selection.underline) }
                        }
                        WordLineTools
                        {
                            Layout.fillWidth: true
                            visible: root.section === "emphasis"
                            theme: root.theme; selection: root.selection; editable: root.editable
                            onFormatRequested: function(action, value) { root.applyToolFormat(action, value, false); }
                        }
                        Flow
                        {
                            Layout.fillWidth: true; spacing: 6; visible: root.section === "emphasis"; enabled: root.editable
                            ActionButton { theme: root.theme; text: "删除线"; compact: true; primary: root.selection.strike || false; onClicked: root.applyFormat("strike", !root.selection.strike) }
                            ActionButton { theme: root.theme; text: "空心文字"; compact: true; primary: root.selection.outline || false; onClicked: root.applyFormat("outline", !root.selection.outline) }
                            ActionButton { theme: root.theme; text: "上标"; compact: true; primary: root.selection.script === 1; onClicked: root.applyFormat("script", root.selection.script === 1 ? 0 : 1) }
                            ActionButton { theme: root.theme; text: "下标"; compact: true; primary: root.selection.script === -1; onClicked: root.applyFormat("script", root.selection.script === -1 ? 0 : -1) }
                            ActionButton { theme: root.theme; text: "清除文字格式"; compact: true; onClicked: root.applyFormat("clear", true) }
                            Text { text: "字符间距（pt）"; color: root.theme.textSecondary }
                            SpinBox
                            {
                                objectName: "wordCharacterSpacing"
                                from: -30; to: 200; stepSize: 5; editable: true
                                value: Math.round((root.selection.characterSpacing || 0) * 10)
                                validator: DoubleValidator { bottom: -3; top: 20; decimals: 1 }
                                textFromValue: function(value, locale) { return Number(value / 10).toLocaleString(locale, "f", 1); }
                                valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10); }
                                onValueModified: root.applyToolFormat("characterSpacing", value / 10, true)
                            }
                        }
                        RowLayout
                        {
                            visible: root.section === "heading"
                            enabled: root.editable
                            Repeater
                            {
                                model: ["正文", "一级标题", "二级标题", "三级标题"]
                                delegate: ActionButton { required property string modelData; required property int index; theme: root.theme; iconName: ""; text: modelData; compact: true; primary: root.selection.heading === index; onClicked: root.applyFormat("heading", index) }
                            }
                        }
                        RowLayout
                        {
                            visible: root.section === "list"
                            enabled: root.editable
                            ActionButton { iconName: ""; theme: root.theme; text: "普通段落"; compact: true; primary: root.selection.list === 0; onClicked: root.applyFormat("list", 0) }
                            ActionButton { iconName: ""; theme: root.theme; text: "项目符号"; compact: true; primary: root.selection.list === 1; onClicked: root.applyFormat("list", 1) }
                            ActionButton { iconName: ""; theme: root.theme; text: "编号列表"; compact: true; primary: root.selection.list === 2; onClicked: root.applyFormat("list", 2) }
                            Text { text: "层级"; color: root.theme.textSecondary }
                            Repeater
                            {
                                model: [1, 2, 3]
                                delegate: ActionButton { required property int modelData; required property int index; iconName: ""; theme: root.theme; text: modelData.toString(); compact: true; primary: root.selection.listLevel === index; enabled: root.selection.list > 0; onClicked: root.applyFormat("listLevel", index) }
                            }
                        }
                        WordListTools
                        {
                            Layout.fillWidth: true
                            visible: root.section === "list"
                            theme: root.theme
                            selection: root.selection
                            editable: root.editable
                            onFormatRequested: function(action, value) { root.applyToolFormat(action, value, true); }
                        }
                        RowLayout
                        {
                            visible: root.section === "sort"
                            enabled: root.editable
                            ComboBox { id: paragraphSortKind; model: ["文字（自然顺序）", "数字"] }
                            ActionButton { theme: root.theme; text: "升序"; compact: true; onClicked: root.applyFormat("sort", paragraphSortKind.currentIndex ? "numberAscending" : "textAscending") }
                            ActionButton { theme: root.theme; text: "降序"; compact: true; onClicked: root.applyFormat("sort", paragraphSortKind.currentIndex ? "numberDescending" : "textDescending") }
                            Text { text: "排序选中的完整段落；未选中文字时排序全文。"; color: root.theme.textSecondary }
                        }
                        WordTabTools
                        {
                            Layout.fillWidth: true
                            visible: root.section === "tabs"
                            theme: root.theme
                            selection: root.selection
                            editable: root.editable
                            onFormatRequested: function(action, value) { root.applyToolFormat(action, value, true); }
                        }
                        WordParagraphTools
                        {
                            Layout.fillWidth: true
                            theme: root.theme; section: root.section; selection: root.selection; editable: root.editable
                            onFormatRequested: function(action, value, keepFocus) { root.applyToolFormat(action, value, keepFocus); }
                        }
                        RowLayout
                        {
                            visible: root.section === "templates"
                            enabled: root.editable
                            Text { text: "从当前段落后插入，字段均需填写"; color: root.theme.textSecondary }
                            ActionButton { iconName: ""; theme: root.theme; text: "来源登记"; compact: true; onClicked: root.templateRequested(editor.cursorPosition, "source") }
                            ActionButton { iconName: ""; theme: root.theme; text: "会议纪要"; compact: true; onClicked: root.templateRequested(editor.cursorPosition, "meeting") }
                            ActionButton { iconName: ""; theme: root.theme; text: "周报"; compact: true; onClicked: root.templateRequested(editor.cursorPosition, "weekly") }
                        }
                        RowLayout
                        {
                            Layout.fillWidth: true
                            visible: root.section === "find"
                            TextField { id: query; Layout.preferredWidth: 160; placeholderText: "查找内容"; maximumLength: 1024; onAccepted: root.findRequested(text, editor.selectionEnd, false) }
                            ActionButton { iconName: ""; theme: root.theme; text: "上一处"; compact: true; enabled: query.text.length > 0; onClicked: root.findRequested(query.text, editor.selectionStart, true) }
                            ActionButton { iconName: ""; theme: root.theme; text: "下一处"; compact: true; enabled: query.text.length > 0; onClicked: root.findRequested(query.text, editor.selectionEnd, false) }
                            ActionButton { theme: root.theme; text: "全部替换"; compact: true; enabled: root.editable && query.text.length > 0; onClicked: root.replaceAllRequested(query.text, replacement.text) }
                            TextField { id: replacement; Layout.preferredWidth: 160; placeholderText: "替换为"; maximumLength: 1024; enabled: root.editable }
                            ActionButton { iconName: ""; theme: root.theme; text: "替换选中项"; compact: true; enabled: root.editable && query.text.length > 0; onClicked: root.replaceRequested(editor.selectionStart, editor.selectionEnd, query.text, replacement.text) }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout
                        {
                            visible: root.section === "history"
                            ActionButton { iconName: ""; theme: root.theme; text: "撤销"; compact: true; enabled: root.editable && root.statistics.canUndo; onClicked: root.undoRequested() }
                            ActionButton { iconName: ""; theme: root.theme; text: "重做"; compact: true; enabled: root.editable && root.statistics.canRedo; onClicked: root.redoRequested() }
                        }
                        RowLayout
                        {
                            visible: root.section === "document"
                            ActionButton { iconName: ""; theme: root.theme; text: "新建 Word 文档"; compact: true; onClicked: root.newRequested() }
                            ActionButton { iconName: ""; theme: root.theme; text: "另存为…"; compact: true; enabled: root.editable; onClicked: root.saveAsRequested() }
                            ActionButton { theme: root.theme; text: "导出 PDF"; compact: true; onClicked: root.pdfRequested() }
                        }
                    }
                }
            }
            SpectrumStroke
            {
                x: tools.x
                y: tools.y + (parameters.visible ? parameters.y : subFrame.y)
                width: tools.width
                height: parameters.visible ? parameters.height : subFrame.height
                visible: subFrame.visible
                theme: root.theme
                radius: root.theme.radius * 0.6
                Behavior on y { NumberAnimation { duration: root.theme.motionEnabled ? root.theme.motionDuration : 0; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: root.theme.motionEnabled ? root.theme.motionDuration : 0 } }
            }
        }
        Text
        {
            Layout.fillWidth: true
            visible: root.readOnly || root.message.length > 0
            text: root.message.length > 0 ? root.message : "正文、表格与图片预览；复杂环绕、文本框及精确分页仍有限制。"
            textFormat: Text.PlainText
            color: root.theme.textSecondary
            font.pixelSize: root.theme.fontSize - 1
            wrapMode: Text.Wrap
        }
        Flickable
        {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: Math.max(width, paper.width * root.zoom + 40)
            contentHeight: paper.height * root.zoom + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            ScrollBar.horizontal: ScrollBar { }
            Rectangle
            {
                id: paper
                objectName: "wordPaper"
                x: Math.max(20, (scroll.width - width * root.zoom) / 2); y: 12
                scale: root.zoom
                transformOrigin: Item.TopLeft
                width: Math.min(820, scroll.width - 40)
                height: Math.max(scroll.height - 32, editor.contentHeight + 96)
                radius: root.theme.radius * 0.5
                color: root.theme.surfaceColor
                border.color: root.theme.borderColor
                TextArea
                {
                    id: editor
                    objectName: "wordEditor"
                    opacity: 0
                    x: 48; y: 40; width: parent.width - 96; height: parent.height - 80
                    padding: 0
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    persistentSelection: true
                    readOnly: true
                    Component.onCompleted: { root.editorReady = true; root.syncEditorAccess(); }
                    color: root.theme.textPrimary
                    selectionColor: root.theme.accent
                    selectedTextColor: root.theme.onAccent
                    background: null
                    Accessible.name: "Word 正文编辑区"
                    TapHandler
                    {
                        acceptedButtons: Qt.RightButton
                        onTapped: function(point)
                        {
                            const position = editor.positionAt(point.position.x, point.position.y);
                            if (position < editor.selectionStart || position > editor.selectionEnd) editor.cursorPosition = position;
                            textMenu.popup(editor, point.position.x, point.position.y);
                        }
                    }
                    onCursorPositionChanged:
                    {
                        cursorBlink.on = true;
                        if (cursorBlink.running) cursorBlink.restart();
                        if (!root.changingEditorAccess) { root.refreshSelection(); root.revealCursor(); }
                    }
                    onSelectionStartChanged: root.refreshSelection()
                    onSelectionEndChanged: root.refreshSelection()
                    Keys.onPressed: function(event)
                    {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            && event.modifiers === Qt.NoModifier && !inputMethodComposing && root.selection.heading > 0)
                        {
                            root.paragraphRequested(selectionStart, selectionEnd);
                            event.accepted = true;
                        }
                        else if (event.matches(StandardKey.Paste))
                        {
                            root.pasteRequested(selectionStart, selectionEnd);
                            event.accepted = true;
                        }
                        else if (event.matches(StandardKey.Undo)) { root.undoRequested(); event.accepted = true; }
                        else if (event.matches(StandardKey.Redo)) { root.redoRequested(); event.accepted = true; }
                    }
                }
                WordViewport
                {
                    id: wordViewport
                    objectName: "wordViewport"
                    x: editor.x; y: editor.y + documentTop
                    width: editor.width
                    height: Math.max(0, Math.min(editor.height - documentTop,
                        scroll.height / Math.max(0.25, Math.min(root.zoom, root.rasterZoom))))
                    documentTop: Math.max(0, (scroll.contentY - paper.y) / root.zoom - editor.y)
                    textDocument: editor.textDocument
                    renderScale: Math.max(1, root.rasterZoom)
                    selection: ({start: editor.selectionStart, end: editor.selectionEnd,
                        color: editor.color, background: editor.selectionColor, foreground: editor.selectedTextColor})
                }
                Rectangle
                {
                    x: editor.x + editor.cursorRectangle.x
                    y: editor.y + editor.cursorRectangle.y
                    width: 1; height: editor.cursorRectangle.height
                    color: root.theme.textPrimary
                    visible: root.editable && editor.activeFocus && editor.selectionStart === editor.selectionEnd && cursorBlink.on
                }
                Timer
                {
                    id: cursorBlink
                    property bool on: true
                    interval: 500; repeat: true; running: root.editable && editor.activeFocus
                    onTriggered: on = !on
                    onRunningChanged: on = true
                }
            }
        }
        ZoomControl { Layout.alignment: Qt.AlignRight; theme: root.theme; zoom: root.zoom; enabled: !root.busy; onZoomRequested: function(value) { root.zoomRequested(value); } }
        RowLayout
        {
            Layout.fillWidth: true
            Text { text: root.statistics.characters + " 字符 · " + root.statistics.paragraphs + " 段"; color: root.theme.textSecondary; font.pixelSize: root.theme.fontSize - 1 }
            Item { Layout.fillWidth: true }
            Text { text: "连续正文视图 · 保留原文档分节设置"; color: root.theme.textSecondary; font.pixelSize: root.theme.fontSize - 1 }
        }
    }

    EditorContextMenu
    {
        id: textMenu
        editable: root.editable
        hasSelection: editor.selectionStart !== editor.selectionEnd
        canUndo: root.statistics.canUndo || false
        canRedo: root.statistics.canRedo || false
        onCopyRequested: editor.copy()
        onCutRequested: editor.cut()
        onPasteRequested: root.pasteRequested(editor.selectionStart, editor.selectionEnd)
        onSelectAllRequested: editor.selectAll()
        onUndoRequested: root.undoRequested()
        onRedoRequested: root.redoRequested()
    }

    RoundedDialog
    {
        id: copyDialog
        theme: root.theme
        x: (root.width - width) / 2; y: (root.height - height) / 2
        width: 540
        title: "创建可编辑副本"
        modal: true
        contentItem: ColumnLayout
        {
            spacing: 18
            Label { Layout.fillWidth: true; text: "副本保留原包中的表格、图片和其他部件，支持正文与基础格式的局部修改。域、书签和分节边界等复杂结构的拆分、合并暂受限。部分内容可能尚未完整显示，原文件不会被覆盖。"; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            RowLayout
            {
                ActionButton { iconName: ""; theme: root.theme; text: "选择新文件名"; primary: true; onClicked: { copyDialog.close(); root.editableCopyRequested(); } }
                ActionButton { iconName: ""; theme: root.theme; text: "继续预览"; onClicked: copyDialog.close() }
            }
        }
    }
    RoundedDialog
    {
        id: supportDialog
        theme: root.theme
        x: (root.width - width) / 2; y: (root.height - height) / 2
        width: 540
        title: "Word 基础排版"
        modal: true
        standardButtons: Dialog.Ok
        contentItem: Label
        {
            text: "支持正文、三级标题、系统字体与中文字号、删除线与上下标、文字颜色与底纹、段落边框、格式刷、样式预设、列表、缩进与行距、全部替换。普通正文和表格单元格中的段落支持拆分、合并与局部写回。当前单元格支持底色、垂直对齐和四边内边距编辑，保留原有合并结构。页眉和页码预设只设置文字样式，不插入页眉区域或自动页码。\n\n导入文件先只读，编辑需另建副本。副本保留原 DOCX 包中的其他部件；表格行列、图片增删移动、域及分节等复杂结构的修改仍受限，保存时会阻止无法安全写回的操作。\n\n正文最多 2 MiB、32768 段，每段最多 8 KiB。表格和图片支持预览，当前采用连续正文视图；复杂环绕、文本框、继承样式和精确分页仍未完整支持。"
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
