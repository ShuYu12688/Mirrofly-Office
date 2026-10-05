import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item
{
    id: root
    objectName: "TextEditorPage"

    property var theme
    property real chromeInset: 0
    property real zoom: 1
    signal zoomRequested(real value)
    WheelHandler
    {
        acceptedModifiers: Qt.ControlModifier
        onWheel: function(event)
        {
            root.zoomRequested(Math.max(0.25, Math.min(4, root.zoom + (event.angleDelta.y > 0 ? 0.1 : -0.1))));
            event.accepted = true;
        }
    }
    property string content: ""
    property int revision: 0
    property string documentName: ""
    property string documentPath: ""
    property bool modified: false
    property bool markdown: false
    property bool outlineVisible: true
    property bool documentEditable: true
    property string documentError: ""
    property var documentState: ({ outline: [], inCode: false, inTable: false, inList: false, inLink: false })
    property real overlayRadius: theme.windowRadius
    readonly property bool modalActive: markdownToolbar.modalActive || textMenu.opened
    property bool busy: false
    property string formatLabel: ""
    property string message: ""
    property bool applyingContent: true
    property bool componentReady: false
    property int currentLine: 1
    property int currentColumn: 1
    property int characterCount: 0
    readonly property var automationSelection: ({start: editor.selectionStart, end: editor.selectionEnd, cursor: editor.cursorPosition, composing: editor.inputMethodComposing})
    property bool lastMarkdownActionHandled: false

    signal documentLoadRequested(var document, string source, bool markdown)
    signal documentStateRequested(var document, int position)
    signal homeRequested()
    signal newRequested()
    signal openRequested()
    signal saveRequested()
    signal pdfRequested()
    signal saveAsRequested()
    signal dismissMessage()
    signal markdownActionRequested(string action, var document, int start, int end, var options)

    function applyMarkdownEdit(edit)
    {
        lastMarkdownActionHandled = edit.handled !== false;
        if (!edit.valid)
        {
            documentError = edit.error || "当前位置暂时无法执行此操作。";
            return;
        }
        if (edit.handled === false)
        {
            return;
        }
        if (busy || applyingContent || !markdown)
        {
            return;
        }
        documentError = "";
        editor.select(edit.selectionStart, edit.selectionEnd);
        documentStateRequested(editor.textDocument, editor.selectionStart);
        focusEditor();
    }

    function finishDocumentLoad(result)
    {
        documentEditable = result.valid;
        documentError = result.error || "";
        editor.cursorPosition = 0;
        editor.deselect();
        editorScrollBar.position = 0;
        applyingContent = false;
        documentStateRequested(editor.textDocument, 0);
        metricsTimer.restart();
        Qt.callLater(focusEditor);
    }

    function applyDocumentState(state)
    {
        root.documentState = state;
        root.documentError = state.error || "";
    }

    function refreshDocumentState()
    {
        documentStateTimer.stop();
        if (!applyingContent)
        {
            documentStateRequested(editor.textDocument, editor.selectionStart);
        }
    }

    function jumpToHeading(position)
    {
        editor.cursorPosition = Math.max(0, Math.min(position, editor.length));
        editor.deselect();
        focusEditor();
        const positionRatio = (editor.cursorRectangle.y * root.zoom - editorScroll.availableHeight * 0.25)
            / Math.max(1, editor.height * root.zoom);
        editorScrollBar.position = Math.max(0, Math.min(1 - editorScrollBar.size, positionRatio));
    }

    function focusEditor()
    {
        if (visible && !busy && !applyingContent)
        {
            editor.forceActiveFocus();
        }
    }

    function reloadContent()
    {
        if (!componentReady)
        {
            return;
        }

        applyingContent = true;
        root.documentLoadRequested(editor.textDocument, content, markdown);
    }

    function refreshMetrics()
    {
        const value = editor.getText(0, editor.length);
        const cursor = editor.cursorPosition;
        let line = 1;
        let column = 1;
        let count = 0;

        for (let offset = 0; offset < value.length; ++offset)
        {
            const code = value.charCodeAt(offset);
            if (offset < cursor)
            {
                if (code === 10)
                {
                    ++line;
                    column = 1;
                }
                else
                {
                    ++column;
                }
            }

            ++count;
            if (code >= 0xD800 && code <= 0xDBFF && offset + 1 < value.length)
            {
                const next = value.charCodeAt(offset + 1);
                if (next >= 0xDC00 && next <= 0xDFFF)
                {
                    ++offset;
                }
            }
        }

        currentLine = line;
        currentColumn = column;
        characterCount = count;
    }

    onMarkdownChanged:
    {
        if (componentReady)
        {
            applyingContent = true;
            Qt.callLater(reloadContent);
        }
    }

    onRevisionChanged:
    {
        if (componentReady)
        {
            applyingContent = true;
            Qt.callLater(reloadContent);
        }
    }

    onVisibleChanged:
    {
        if (visible)
        {
            Qt.callLater(focusEditor);
        }
    }

    onBusyChanged:
    {
        if (!busy)
        {
            Qt.callLater(focusEditor);
        }
    }

    Component.onCompleted:
    {
        componentReady = true;
        Qt.callLater(reloadContent);
    }

    Timer
    {
        id: metricsTimer

        interval: 90
        repeat: false
        onTriggered: root.refreshMetrics()
    }

    Timer
    {
        id: documentStateTimer

        interval: 0
        repeat: false
        onTriggered: root.refreshDocumentState()
    }

    Rectangle
    {
        anchors.fill: parent
        color: root.theme.backgroundColor
    }

    ColumnLayout
    {
        anchors.fill: parent
        anchors.leftMargin: 34
        anchors.rightMargin: 34
        anchors.topMargin: 27 + root.chromeInset
        anchors.bottomMargin: 19
        spacing: 19

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 15

            Rectangle
            {
                Layout.preferredWidth: 43
                Layout.preferredHeight: 47
                radius: root.theme.radius * 0.75
                color: root.theme.writerSurface

                UiIcon
                {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    symbol: "writer"
                    iconColor: root.theme.writerAccent
                }
            }

            ColumnLayout
            {
                Layout.fillWidth: true
                spacing: 6

                Text
                {
                    Layout.fillWidth: true
                    text: root.documentName.length > 0 ? root.documentName : "未命名文本"
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize + 8
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Text
                {
                    Layout.fillWidth: true
                    text: root.documentPath.length > 0 ? root.documentPath : "尚未选择保存位置"
                    color: root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 2
                    elide: Text.ElideMiddle
                }
            }

            Rectangle
            {
                Layout.preferredWidth: documentState.implicitWidth + 24
                Layout.preferredHeight: 29
                radius: root.theme.radius * 0.56
                color: root.modified ? root.theme.accentSoft : root.theme.surfaceColor
                border.width: 1
                border.color: root.modified ? root.theme.accent : root.theme.borderColor

                Text
                {
                    id: documentState

                    anchors.centerIn: parent
                    text: root.busy ? "正在处理…"
                        : (root.modified ? "有未保存的修改" : (root.documentPath.length > 0 ? "已保存" : "新文档"))
                    color: root.modified ? root.theme.accent : root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 3
                }
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 10
            enabled: !root.busy && !root.applyingContent && !root.modalActive

            ActionButton
            {
                theme: root.theme
                text: "返回首页"
                iconName: "home"
                compact: true
                onClicked: root.homeRequested()
            }

            Rectangle
            {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 20
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                color: root.theme.borderColor
            }

            ActionButton
            {
                theme: root.theme
                text: "新建"
                iconName: "plus"
                compact: true
                onClicked: root.newRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "打开"
                iconName: "folder"
                compact: true
                onClicked: root.openRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "保存"
                iconName: "check"
                primary: true
                compact: true
                onClicked: root.saveRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "另存为"
                iconName: "writer"
                compact: true
                onClicked: root.saveAsRequested()
            }
            ActionButton { theme: root.theme; text: "导出 PDF"; compact: true; enabled: !root.busy; onClicked: root.pdfRequested() }

            Item
            {
                Layout.fillWidth: true
            }

            Text
            {
                visible: !root.markdown
                text: "Ctrl + S 保存"
                color: root.theme.mutedColor
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 3
            }

            ActionButton
            {
                visible: root.markdown
                theme: root.theme
                text: root.outlineVisible ? "收起大纲" : "显示大纲"
                iconName: ""
                compact: true
                onClicked: root.outlineVisible = !root.outlineVisible
            }
        }

        MarkdownToolbar
        {
            id: markdownToolbar

            Layout.fillWidth: true
            theme: root.theme
            overlayRadius: root.overlayRadius
            contextInfo: root.documentState
            selectionStart: editor.selectionStart
            selectionEnd: editor.selectionEnd
            visible: root.markdown
            enabled: !root.busy && !root.applyingContent && root.documentEditable
            onContextRequested: root.refreshDocumentState()
            onActionRequested: function(action, options)
            {
                root.markdownActionRequested(action, editor.textDocument, editor.selectionStart, editor.selectionEnd, options);
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 150
            spacing: 16

            Rectangle
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 150
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                radius: root.theme.radius
                color: root.theme.surfaceColor
                opacity: root.theme.surfaceOpacity
                border.width: editor.activeFocus ? 2 : 1
                border.color: editor.activeFocus ? root.theme.accent : root.theme.borderColor

                ScrollView
                {
                    id: editorScroll

                    anchors.fill: parent
                    anchors.margins: 3
                    contentWidth: availableWidth
                    contentHeight: editor.implicitHeight * root.zoom
                    clip: true
                    enabled: !root.busy && !root.applyingContent
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ScrollBar.vertical: ScrollBar
                    {
                        id: editorScrollBar

                        width: 10
                        policy: ScrollBar.AsNeeded
                        hoverEnabled: true

                        contentItem: Rectangle
                        {
                            implicitWidth: 6
                            radius: 3
                            color: editorScrollBar.hovered || editorScrollBar.pressed
                                ? root.theme.accent : root.theme.mutedColor
                        }

                        background: Rectangle
                        {
                            color: root.theme.transparentColor
                        }
                    }

                    TextArea
                    {
                        id: editor
                        objectName: "textEditorArea"

                        width: editorScroll.availableWidth / root.zoom
                        height: implicitHeight
                        scale: root.zoom
                        transformOrigin: Item.TopLeft
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        color: root.theme.textPrimary
                        selectionColor: root.theme.accent
                        selectedTextColor: root.theme.onAccent
                        font.family: root.markdown ? root.theme.fontFamily : root.theme.editorFontFamily
                        font.pixelSize: root.theme.editorFontSize
                        leftPadding: 28
                        rightPadding: 28
                        topPadding: 24
                        bottomPadding: 24
                        background: null
                        selectByMouse: true
                        persistentSelection: true
                        activeFocusOnTab: true
                        readOnly: root.busy || root.applyingContent || !root.documentEditable
                        Accessible.name: root.markdown ? "Markdown 可视编辑区" : "纯文本编辑区"
                        Accessible.description: "支持选中、复制、粘贴、撤销和重做。文本自动换行。"
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
                        onTextChanged:
                        {
                            metricsTimer.restart();
                        }
                        onCursorPositionChanged:
                        {
                            metricsTimer.restart();
                            documentStateTimer.restart();
                        }
                        onSelectionStartChanged: documentStateTimer.restart()
                        onSelectionEndChanged: documentStateTimer.restart()

                        Keys.onPressed: function(event)
                        {
                            if (!root.markdown || editor.readOnly)
                            {
                                return;
                            }
                            let action = "";
                            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                            {
                                if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                                    return;
                                const backward = event.key === Qt.Key_Backtab || event.modifiers & Qt.ShiftModifier;
                                root.refreshDocumentState();
                                if (root.documentState.inTable)
                                    action = backward ? "tablePreviousCell" : "tableNextCell";
                                else if (root.documentState.inList)
                                    action = backward ? "listOutdent" : "listIndent";
                                else
                                {
                                    return;
                                }
                            }
                            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            {
                                if (editor.inputMethodComposing ||
                                    event.modifiers & (Qt.ControlModifier |
                                        Qt.AltModifier | Qt.MetaModifier))
                                {
                                    return;
                                }
                                if (event.modifiers & Qt.ShiftModifier)
                                {
                                    root.refreshDocumentState();
                                    if (root.documentState.inCode) return;
                                    action = "hardBreak";
                                }
                                else action = "enter";
                            }
                            if (action.length > 0)
                            {
                                root.lastMarkdownActionHandled = false;
                                root.markdownActionRequested(action, editor.textDocument,
                                    editor.selectionStart, editor.selectionEnd, {});
                                event.accepted = root.lastMarkdownActionHandled;
                            }
                        }
                    }
                }
            }

            MarkdownOutline
            {
                Layout.fillHeight: true
                Layout.preferredWidth: 210
                theme: root.theme
                entries: root.documentState.outline || []
                currentPosition: editor.cursorPosition
                visible: root.markdown && root.outlineVisible
                onPositionRequested: function(position)
                {
                    root.jumpToHeading(position);
                }
            }
        }

        ZoomControl { Layout.alignment: Qt.AlignRight; theme: root.theme; zoom: root.zoom; enabled: !root.busy; onZoomRequested: function(value) { root.zoomRequested(value); } }

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 18

            Text
            {
                Layout.fillWidth: true
                text: root.formatLabel.length > 0 ? root.formatLabel : "纯文本"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 3
                elide: Text.ElideRight
            }

            Text
            {
                text: "第 " + root.currentLine + " 行，第 " + root.currentColumn + " 列"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 3
            }

            Text
            {
                text: root.characterCount + " 个字符"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 3
            }

            Text
            {
                text: "自动换行"
                color: root.theme.mutedColor
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 3
            }
        }
    }

    Rectangle
    {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 63
        width: Math.min(parent.width - 100, 660)
        height: messageText.implicitHeight + 28
        radius: root.theme.radius * 0.7
        color: root.theme.noticeBackground
        opacity: root.theme.surfaceOpacity
        visible: root.message.length > 0 || root.documentError.length > 0

        RowLayout
        {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 16

            Text
            {
                id: messageText

                Layout.fillWidth: true
                text: root.documentError.length > 0 ? root.documentError : root.message
                color: root.theme.noticeForeground
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
            }

            AbstractButton
            {
                id: dismissButton

                implicitWidth: 25
                implicitHeight: 25
                enabled: !root.busy
                hoverEnabled: true
                activeFocusOnTab: true
                padding: 2
                Accessible.name: "关闭提示"
                onClicked:
                {
                    if (root.documentEditable)
                    {
                        root.documentError = "";
                    }
                    root.dismissMessage();
                }

                HoverHandler
                {
                    cursorShape: Qt.PointingHandCursor
                }

                contentItem: UiIcon
                {
                    symbol: "close"
                    iconColor: root.theme.noticeForeground
                }

                background: Rectangle
                {
                    color: dismissButton.hovered || dismissButton.down
                        ? root.theme.navigationHover : root.theme.transparentColor
                    border.width: dismissButton.visualFocus ? 1 : 0
                    border.color: root.theme.noticeForeground
                    radius: 4
                }
            }
        }
    }
    EditorContextMenu
    {
        id: textMenu
        editable: !editor.readOnly
        hasSelection: editor.selectionStart !== editor.selectionEnd
        canUndo: editor.canUndo
        canRedo: editor.canRedo
        onCopyRequested: editor.copy()
        onCutRequested: editor.cut()
        onPasteRequested: editor.paste()
        onSelectAllRequested: editor.selectAll()
        onUndoRequested: editor.undo()
        onRedoRequested: editor.redo()
    }
}
