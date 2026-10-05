pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mirrorfly.Native
import "components"

FocusScope
{
    id: root
    objectName: "SpreadsheetPage"
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

    property var theme
    property var systemFontFamilies: []
    property real chromeInset: 0
    property var layoutInfo: ({})
    signal startToolRequested(string action, var args)
    property var formatInfo: ({})
    property int layoutRevision: 0
    function columnWidthFor(index) { return 132; }
    function rowHeightFor(index) { return 34; }
    property var tableModel
    property var sheetNames: []
    property int currentSheet: 0
    property var rangeInfo: ({firstRow: 0, lastRow: 0, firstColumn: 0, lastColumn: 0, address: "A1", count: 1, numbers: 0, nonempty: 0, sum: "0", average: "—"})
    property var cellInfo: ({row: 0, column: 0, address: "A1", inputText: "", kind: "auto", canEdit: false})
    property string documentName: ""
    property string documentPath: ""
    property string error: ""
    property string compatibilitySummary: ""
    property bool modified: false
    property bool busy: false
    property bool canUndo: false
    property bool canRedo: false
    property bool editing: false
    property string draft: ""
    property string draftKind: "auto"
    property bool editAccepted: false
    property string activeGroup: ""
    property string activeSection: ""
    property string pendingNavigation: ""
    readonly property bool modalActive: compatibilityDialog.opened || sheetNameDialog.opened || cellMenu.opened || sheetMenu.opened || draggingCells || draggingBand || pendingNavigation.length > 0
    property int bandAnchor: 0
    property bool draggingBand: false
    property bool bandRows: true
    property bool draggingCells: false
    property point dragPoint: Qt.point(0, 0)

    signal formatRequested(var patch)
    signal styleRequested(string preset, var palette)
    signal sizeRequested(bool columns, real size)
    signal sortRequested(bool descending, bool header)
    signal templateRequested(string name)
    onLayoutRevisionChanged: { grid.forceLayout(); frozenTop.forceLayout(); frozenLeft.forceLayout(); frozenCorner.forceLayout(); }

    signal homeRequested()
    signal newRequested()
    signal openRequested()
    signal saveRequested()
    signal pdfRequested()
    signal saveAsRequested()
    signal undoRequested()
    signal redoRequested()
    signal sheetRequested(int index)
    signal addSheetRequested(string name)
    signal renameSheetRequested(string name)
    signal bandRequested(int first, int last, bool rows)
    signal cellRequested(int row, int column, bool extend)
    signal cellEditRequested(int row, int column, string value, string kind)
    signal findRequested(string query, bool backwards)
    signal addressRequested(string address)
    signal copyRequested()
    signal pasteRequested()
    signal clearRequested()
    signal inputCommitRequested()
    signal dismissError()
    signal navigationReady(string action)

    function prepareNavigation(action)
    {
        if (busy || pendingNavigation.length > 0) return false;
        if (finishCellEditing()) return true;
        pendingNavigation = action;
        if (root.Window.window) draftDialog.open();
        return false;
    }

    function resolveDraftNavigation(discard)
    {
        const action = pendingNavigation;
        pendingNavigation = "";
        draftDialog.close();
        if (discard && action.length > 0)
        {
            cancelCellEditing();
            dismissError();
            resumeNavigation(action);
        }
        else valueEditor.forceActiveFocus();
    }

    function resumeNavigation(action)
    {
        if (action === "home") homeRequested();
        else if (action === "open") openRequested();
        else if (action === "new") newRequested();
        else navigationReady(action);
    }

    function navigate(action)
    {
        if (prepareNavigation(action)) resumeNavigation(action);
    }

    function syncDraft()
    {
        if (!editing)
        {
            draft = cellInfo.inputText || "";
            draftKind = cellInfo.kind === "text" ? "text" : "auto";
        }
    }

    function startEditing()
    {
        if (busy || !cellInfo.canEdit)
        {
            return false;
        }
        if (!editing)
        {
            syncDraft();
            editing = true;
        }
        return true;
    }

    function acceptCellEdit(accepted)
    {
        editAccepted = accepted;
    }

    function finishCellEditing()
    {
        if (!editing)
        {
            return true;
        }
        inputCommitRequested();
        editAccepted = false;
        const unchangedTypedValue = draft === cellInfo.inputText && draftKind === "auto"
            && (cellInfo.kind === "number" || cellInfo.kind === "boolean");
        cellEditRequested(cellInfo.row, cellInfo.column, draft, unchangedTypedValue ? cellInfo.kind : draftKind);
        if (!editAccepted)
        {
            valueEditor.forceActiveFocus();
            return false;
        }
        editing = false;
        syncDraft();
        return true;
    }

    function cancelCellEditing()
    {
        editing = false;
        syncDraft();
        grid.forceActiveFocus();
    }

    function chooseCell(row, column, extend = false)
    {
        if (busy || !finishCellEditing())
        {
            return false;
        }
        cellRequested(row, column, extend);
        return true;
    }

    function extendPointerSelection(x, y)
    {
        dragPoint = Qt.point(x, y);
        const position = grid.cellAtPosition(Qt.point(grid.contentX + Math.max(1, Math.min(grid.width - 1, x)),
            grid.contentY + Math.max(1, Math.min(grid.height - 1, y))), true);
        if (position.x >= 0 && position.y >= 0) cellRequested(position.y, position.x, true);
    }

    Timer
    {
        interval: 45
        repeat: true
        running: root.draggingCells
        onTriggered:
        {
            const dx = root.dragPoint.x < 24 ? -24 : root.dragPoint.x > grid.width - 24 ? 24 : 0;
            const dy = root.dragPoint.y < 24 ? -24 : root.dragPoint.y > grid.height - 24 ? 24 : 0;
            if (!dx && !dy) return;
            grid.contentX = Math.max(0, Math.min(Math.max(0, grid.contentWidth - grid.width), grid.contentX + dx));
            grid.contentY = Math.max(0, Math.min(Math.max(0, grid.contentHeight - grid.height), grid.contentY + dy));
            root.extendPointerSelection(root.dragPoint.x, root.dragPoint.y);
        }
    }

    function moveCell(rowStep, columnStep, extend = false)
    {
        let row = Math.max(0, Math.min(grid.rows - 1, cellInfo.row + rowStep));
        let column = Math.max(0, Math.min(grid.columns - 1, cellInfo.column + columnStep));
        for (const merge of (layoutInfo.merges || []))
        {
            if (row < merge.row || row > merge.lastRow || column < merge.column || column > merge.lastColumn) continue;
            if (!extend)
            {
                if (cellInfo.row === merge.row && cellInfo.column === merge.column)
                {
                    if (rowStep > 0) row = Math.min(grid.rows - 1, merge.lastRow + 1);
                    if (columnStep > 0) column = Math.min(grid.columns - 1, merge.lastColumn + 1);
                }
                else { row = merge.row; column = merge.column; }
            }
        }
        while (rowStep && row >= 0 && row < grid.rows && root.rowHeightFor(row) === 0) row += rowStep;
        while (columnStep && column >= 0 && column < grid.columns && root.columnWidthFor(column) === 0) column += columnStep;
        if (row < 0 || row >= grid.rows || column < 0 || column >= grid.columns) return;
        if (chooseCell(row, column, extend))
        {
            grid.positionViewAtCell(Qt.point(column, row), TableView.Contain);
        }
    }

    function revealCell()
    {
        grid.positionViewAtCell(Qt.point(cellInfo.column, cellInfo.row), TableView.Contain);
        grid.forceActiveFocus();
    }

    function focusGrid()
    {
        grid.forceActiveFocus();
    }

    function commitAndMove(rowStep, columnStep)
    {
        if (finishCellEditing())
        {
            moveCell(rowStep, columnStep);
            grid.forceActiveFocus();
        }
    }

    onCellInfoChanged: syncDraft()
    onZoomChanged: { grid.forceLayout(); frozenTop.forceLayout(); frozenLeft.forceLayout(); frozenCorner.forceLayout(); revealCell(); }
    onCurrentSheetChanged: grid.positionViewAtCell(Qt.point(0, 0), TableView.AlignLeft | TableView.AlignTop)
    onActiveGroupChanged: activeSection = ""
    Component.onCompleted: syncDraft()

    Component
    {
        id: gridCell
        Rectangle
        {
            id: cell
            required property int row
            required property int column
            required property var display
            required property bool formulaCell
            required property bool editableCell
            required property var cellFormat
            property bool pooled: false
            TableView.onPooled: pooled = true
            TableView.onReused: pooled = false
            readonly property bool selected: root.cellInfo.row === row && root.cellInfo.column === column
            readonly property bool inRange: row >= root.rangeInfo.firstRow && row <= root.rangeInfo.lastRow
                && column >= root.rangeInfo.firstColumn && column <= root.rangeInfo.lastColumn
            implicitWidth: 132
            implicitHeight: 34
            objectName: "spreadsheetCell_" + row + "_" + column
            color: inRange ? root.theme.sheetsSelectionFill : cellFormat.fill && cellFormat.fill !== "none" ? cellFormat.fill : root.theme.surfaceColor
            border.width: selected ? 2 : 1
            border.color: selected ? root.theme.accent : root.theme.sheetsGridLine
            SpreadsheetBorderRenderer { anchors.fill: parent; format: cell.cellFormat; zoom: root.zoom; visible: !cell.selected && cell.cellFormat.border === "1" }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 2; color: root.theme.sheetsSelectionBorder; visible: cell.inRange && cell.row === root.rangeInfo.firstRow }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 2; color: root.theme.sheetsSelectionBorder; visible: cell.inRange && cell.row === root.rangeInfo.lastRow }
            Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 2; color: root.theme.sheetsSelectionBorder; visible: cell.inRange && cell.column === root.rangeInfo.firstColumn }
            Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 2; color: root.theme.sheetsSelectionBorder; visible: cell.inRange && cell.column === root.rangeInfo.lastColumn }


            Text
            {
                id: cellText
                anchors.fill: parent
                anchors.margins: 7 * root.zoom
                text: cell.display
                textFormat: Text.PlainText
                elide: Text.ElideRight
                verticalAlignment: cell.cellFormat.valign === "top" ? Text.AlignTop : cell.cellFormat.valign === "center" ? Text.AlignVCenter : Text.AlignBottom
                leftPadding: Number(cell.cellFormat.indent || 0) * 10 * root.zoom
                color: cell.inRange ? root.theme.sheetsSelectionText : cell.cellFormat.text || root.theme.textPrimary
                font.family: cell.cellFormat.font || root.theme.fontFamily
                font.pixelSize: Math.max(8, Math.min(128, Number(cell.cellFormat.size || 11) * 4 / 3)) * root.zoom
                font.bold: cell.cellFormat.bold === "1"
                font.italic: cell.cellFormat.italic === "1"
                font.underline: cell.cellFormat.underline === "1"
                font.strikeout: cell.cellFormat.strike === "1"
                wrapMode: cell.cellFormat.wrap === "1" ? Text.Wrap : Text.NoWrap
                horizontalAlignment: cell.cellFormat.align === "center" ? Text.AlignHCenter : cell.cellFormat.align === "right" ? Text.AlignRight : Text.AlignLeft
                clip: true
                visible: !inlineEditor.visible && Number(cell.cellFormat.textRotation || 0) === 0 && cell.cellFormat.shrinkToFit !== "1" && ["justify", "distributed"].indexOf(cell.cellFormat.align) < 0
            }
            SpreadsheetTextRenderer
            {
                anchors.fill: parent
                anchors.margins: 7 * root.zoom
                anchors.leftMargin: 7 * root.zoom + cellText.leftPadding
                text: cell.display
                font: cellText.font
                color: cellText.color
                format: cell.cellFormat
                visible: !inlineEditor.visible && !cellText.visible
            }
            MouseArea
            {
                anchors.fill: parent
                enabled: !inlineEditor.visible
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
                onPressed: function(mouse)
                {
                    if (mouse.button === Qt.RightButton && cell.inRange)
                    {
                        if (!root.finishCellEditing()) return;
                        cellMenu.popup();
                        return;
                    }
                    if (!root.chooseCell(cell.row, cell.column, Boolean(mouse.modifiers & Qt.ShiftModifier)))
                    { mouse.accepted = false; return; }
                    grid.forceActiveFocus();
                    if (mouse.button === Qt.RightButton) cellMenu.popup();
                    else
                    {
                        root.dragPoint = mapToItem(grid, mouse.x, mouse.y);
                        root.draggingCells = cell.TableView.view === grid;
                    }
                }
                onPositionChanged: function(mouse)
                {
                    if (!root.draggingCells || !(pressedButtons & Qt.LeftButton)) return;
                    const p = mapToItem(grid, mouse.x, mouse.y);
                    root.extendPointerSelection(p.x, p.y);
                }
                onReleased: root.draggingCells = false
                onCanceled: root.draggingCells = false
                onDoubleClicked:
                {
                    if (root.chooseCell(cell.row, cell.column) && root.startEditing())
                    {
                        root.draggingCells = false;
                        inlineEditor.forceActiveFocus();
                        inlineEditor.selectAll();
                    }
                }
            }
            TextField
            {
                id: inlineEditor
                objectName: "spreadsheetInlineEditor"
                anchors.fill: parent
                anchors.margins: 1
                visible: !cell.pooled && cell.selected && root.editing
                readOnly: !cell.editableCell
                text: visible ? root.draft : ""
                maximumLength: 32767
                selectByMouse: true
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: Math.max(8, Math.min(128, Number(cell.cellFormat.size || 11) * 4 / 3)) * root.zoom
                background: Rectangle { color: root.theme.surfaceColor; border.color: root.theme.accent }
                onTextEdited: root.draft = text
                Keys.onReturnPressed: function(event) { root.commitAndMove(1, 0); event.accepted = true; }
                Keys.onEnterPressed: function(event) { root.commitAndMove(1, 0); event.accepted = true; }
                Keys.onEscapePressed: function(event) { root.cancelCellEditing(); event.accepted = true; }
                Keys.onTabPressed: function(event) { root.commitAndMove(0, 1); event.accepted = true; }
                Keys.onBacktabPressed: function(event) { root.commitAndMove(0, -1); event.accepted = true; }
            }
        }
    }

    Rectangle
    {
        anchors.fill: parent
        color: root.theme.backgroundColor
    }

    ColumnLayout
    {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.topMargin: root.chromeInset + 16
        anchors.bottomMargin: 20
        spacing: 12

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 12

            ActionButton
            {
                theme: root.theme
                text: "首页"
                iconName: "back"
                compact: true
                enabled: !root.busy
                onClicked: root.navigate("home")
            }

            Text
            {
                Layout.fillWidth: true
                text: root.documentName + (root.modified || root.editing ? " · 未保存" : "")
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize + 5
            }

            ActionButton
            {
                theme: root.theme
                text: "打开"
                compact: true
                enabled: !root.busy
                onClicked: root.navigate("open")
            }
            ActionButton
            {
                theme: root.theme
                text: "保存"
                iconName: "check"
                compact: true
                primary: true
                enabled: !root.busy
                onClicked: { if (root.finishCellEditing()) root.saveRequested(); }
            }
        }

        Rectangle
        {
            Layout.fillWidth: true
            implicitHeight: toolStack.implicitHeight + 20
            color: root.theme.surfaceColor
            radius: root.theme.radius
            border.color: root.theme.borderColor

            ColumnLayout
            {
                id: toolStack
                x: 12
                y: 10
                width: parent.width - 24
                spacing: 8

                RowLayout
                {
                    Layout.fillWidth: true
                    Repeater
                    {
                        model: [{key: "cell", label: "单元格"}, {key: "appearance", label: "样式"}, {key: "data", label: "数据与模板"}, {key: "sheet", label: "工作表"}, {key: "file", label: "文件"}]
                        delegate: ActionButton
                        {
                            required property var modelData
                            theme: root.theme
                            text: modelData.label
                            iconName: "chevron"
                            compact: true
                            primary: root.activeGroup === modelData.key
                            enabled: !root.busy
                            onClicked: root.activeGroup = root.activeGroup === modelData.key ? "" : modelData.key
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text
                    {
                        text: root.busy ? "正在处理…" : "双击编辑 · Shift + 点击 / 方向键扩展选区"
                        color: root.theme.textSecondary
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.fontSize - 1
                    }
                }

                Rectangle
                {
                    Layout.fillWidth: true
                    id: subFrame
                    visible: root.activeGroup.length > 0
                    implicitHeight: subTools.implicitHeight + 14
                    color: root.theme.transparentColor
                    radius: root.theme.radius * 0.6


                    RowLayout
                    {
                        id: subTools
                        x: 9
                        y: 7
                        width: parent.width - 18
                        Repeater
                        {
                            model: root.activeGroup === "cell" ? [{key: "value", label: "值与类型"}, {key: "clipboard", label: "复制与粘贴"}, {key: "history", label: "修改历史"}]
                                : root.activeGroup === "appearance" ? [{key: "font", label: "字体"}, {key: "table", label: "表格样式"}, {key: "style", label: "单元格样式"}, {key: "colors", label: "颜色"}, {key: "border", label: "边框"}, {key: "align", label: "对齐"}, {key: "number", label: "数字格式"}, {key: "merge", label: "合并"}, {key: "condition", label: "条件格式"}]
                                : root.activeGroup === "data" ? [{key: "sort", label: "内容排序"}, {key: "templates", label: "快捷模板"}, {key: "fill", label: "填充"}, {key: "aggregate", label: "汇总数值"}, {key: "filter", label: "筛选"}]
                                : (root.activeGroup === "sheet" ? [{key: "size", label: "行列尺寸"}, {key: "structure", label: "插入与删除行列"}, {key: "freeze", label: "冻结"}, {key: "switch", label: "管理工作表"}, {key: "find", label: "查找内容"}, {key: "jump", label: "定位单元格"}]
                                : [{key: "document", label: "新建与另存"}, {key: "support", label: "支持范围"}])
                            delegate: ActionButton
                            {
                                required property var modelData
                                theme: root.theme
                                text: modelData.label
                                compact: true
                                primary: root.activeSection === modelData.key
                                iconName: "arrow"
                                enabled: !root.busy
                                onClicked:
                                {
                                    root.activeSection = root.activeSection === modelData.key ? "" : modelData.key;
                                    if (modelData.key === "support") compatibilityDialog.open();
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                Item
                {
                    id: parameterFrame
                    Layout.fillWidth: true
                    visible: root.activeSection.length > 0 && root.activeSection !== "support"
                    implicitHeight: parameterStack.implicitHeight + 16
                    ColumnLayout
                    {
                        id: parameterStack
                        x: 10
                        y: 8
                        width: parent.width - 20
                        SpreadsheetStartTools
                        {
                            Layout.fillWidth: true; theme: root.theme; section: root.activeSection
                            rangeInfo: root.rangeInfo; layoutInfo: root.layoutInfo; enabled: !root.busy
                            onRequested: function(action, args) { if (root.finishCellEditing()) root.startToolRequested(action, args); }
                        }
                        SpreadsheetAppearance
                        {
                            Layout.fillWidth: true
                            theme: root.theme
                            systemFontFamilies: root.systemFontFamilies
                            section: root.activeSection
                            format: root.formatInfo
                            enabled: !root.busy
                            onToolRequested: function(action) { if (root.finishCellEditing()) root.startToolRequested(action, {}); }
                            onFormatRequested: function(patch) { if (root.finishCellEditing()) root.formatRequested(patch); }
                            onStyleRequested: function(preset, palette) { if (root.finishCellEditing()) root.styleRequested(preset, palette); }
                            onSizeRequested: function(columns, size) { if (root.finishCellEditing()) root.sizeRequested(columns, size); }
                        }
                        SpreadsheetTableTools
                        {
                            Layout.fillWidth: true
                            visible: root.activeSection === "table"
                            enabled: !root.busy
                            theme: root.theme
                            layoutInfo: root.layoutInfo
                            onRequested: function(action, args) { if (root.finishCellEditing()) root.startToolRequested(action, args); }
                        }
                        RowLayout
                        {
                            visible: root.activeSection === "sort"
                            CheckBox { id: sortHeader; text: "首行为表头"; checked: true }
                            ActionButton { theme: root.theme; text: "升序"; compact: true; onClicked: { if (root.finishCellEditing()) root.sortRequested(false, sortHeader.checked); } }
                            ActionButton { theme: root.theme; text: "降序"; compact: true; onClicked: { if (root.finishCellEditing()) root.sortRequested(true, sortHeader.checked); } }
                            Text { text: "按选区首列排序 · 内容和格式一起移动"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            visible: root.activeSection === "templates"
                            Repeater
                            {
                                model: [{key: "references", text: "文献来源登记"}, {key: "tasks", text: "任务清单"}, {key: "budget", text: "预算台账"}]
                                delegate: ActionButton
                                {
                                    required property var modelData
                                    theme: root.theme
                                    text: modelData.text
                                    compact: true
                                    onClicked: { if (root.finishCellEditing()) root.templateRequested(modelData.key); }
                                }
                            }
                            Text { text: "从当前单元格插入，请先选择空白区域"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            Layout.fillWidth: true
                            visible: root.activeSection === "value"
                            Text { text: "输入类型"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                            ComboBox
                            {
                                implicitWidth: 150
                                model: [{text: "自动识别", key: "auto"}, {text: "文本（保留编号）", key: "text"},
                                    {text: "数字", key: "number"}, {text: "布尔值", key: "boolean"}]
                                textRole: "text"
                                valueRole: "key"
                                currentIndex: Math.max(0, indexOfValue(root.draftKind))
                                enabled: !root.busy && root.cellInfo.canEdit
                                onActivated: { const kind = currentValue; if (root.startEditing()) root.draftKind = kind; }
                            }
                            ActionButton
                            {
                                theme: root.theme
                                text: "清空选区"
                                iconName: "close"
                                compact: true
                                enabled: !root.busy && root.cellInfo.canEdit
                                onClicked: { if (root.finishCellEditing()) root.clearRequested(); }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout
                        {
                            visible: root.activeSection === "history"
                            ActionButton { theme: root.theme; text: "撤销"; compact: true; iconName: "back"; enabled: root.canUndo && !root.busy; onClicked: { if (root.finishCellEditing()) root.undoRequested(); } }
                            ActionButton { theme: root.theme; text: "重做"; compact: true; iconName: "arrow"; enabled: root.canRedo && !root.busy; onClicked: { if (root.finishCellEditing()) root.redoRequested(); } }
                        }

                        RowLayout
                        {
                            visible: root.activeSection === "clipboard"
                            ActionButton { theme: root.theme; text: "复制选区"; compact: true; enabled: !root.busy; onClicked: { if (root.finishCellEditing()) root.copyRequested(); } }
                            ActionButton { theme: root.theme; text: "粘贴 / 填充选区"; compact: true; enabled: !root.busy && root.cellInfo.canEdit; onClicked: { if (root.finishCellEditing()) root.pasteRequested(); } }
                        }

                        RowLayout
                        {
                            Layout.fillWidth: true
                            visible: root.activeSection === "find"
                            TextField { id: searchField; Layout.preferredWidth: 240; placeholderText: "当前工作表 · 内容或公式"; maximumLength: 32767; onAccepted: { if (root.finishCellEditing()) root.findRequested(text, false); } }
                            ActionButton { theme: root.theme; text: "上一处"; compact: true; enabled: !root.busy && searchField.text.length > 0; onClicked: { if (root.finishCellEditing()) root.findRequested(searchField.text, true); } }
                            ActionButton { theme: root.theme; text: "下一处"; compact: true; enabled: !root.busy && searchField.text.length > 0; onClicked: { if (root.finishCellEditing()) root.findRequested(searchField.text, false); } }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout
                        {
                            visible: root.activeSection === "jump"
                            TextField { id: addressField; implicitWidth: 180; placeholderText: "C20 或 A1:C10"; maximumLength: 24; onAccepted: { if (root.finishCellEditing()) root.addressRequested(text); } }
                            ActionButton { theme: root.theme; text: "定位"; compact: true; enabled: !root.busy; onClicked: { if (root.finishCellEditing()) root.addressRequested(addressField.text); } }
                        }

                        ComboBox
                        {
                            objectName: "spreadsheetSheetChooser"
                            id: sheetChooser
                            visible: root.activeSection === "switch"
                            implicitWidth: 240
                            model: root.sheetNames
                            currentIndex: root.currentSheet
                            delegate: ItemDelegate
                            {
                                required property int index
                                required property string modelData
                                width: sheetChooser.width
                                enabled: !root.layoutInfo.hiddenSheetIndices || root.layoutInfo.hiddenSheetIndices.indexOf(index) < 0
                                text: modelData + (enabled ? "" : "（隐藏）")
                            }
                            enabled: !root.busy
                            onActivated:
                            {
                                const requestedIndex = currentIndex;
                                if (root.finishCellEditing()) root.sheetRequested(requestedIndex);
                                currentIndex = Qt.binding(function() { return root.currentSheet; });
                            }
                        }

                        RowLayout
                        {
                            visible: root.activeSection === "document"
                            ActionButton { theme: root.theme; text: "新建工作簿"; compact: true; iconName: "plus"; enabled: !root.busy; onClicked: root.navigate("new") }
                            ActionButton { theme: root.theme; text: "另存为…"; compact: true; iconName: "folder"; enabled: !root.busy; onClicked: { if (root.finishCellEditing()) root.saveAsRequested(); } }
                            ActionButton { theme: root.theme; text: "导出 PDF"; compact: true; onClicked: { if (root.finishCellEditing()) root.pdfRequested(); } }
                        }
                    }
                }
            }
            SpectrumStroke
            {
                x: toolStack.x
                y: toolStack.y + (parameterFrame.visible ? parameterFrame.y : subFrame.y)
                width: toolStack.width
                height: parameterFrame.visible ? parameterFrame.height : subFrame.height
                visible: subFrame.visible
                theme: root.theme
                radius: root.theme.radius * 0.6
                Behavior on y { NumberAnimation { duration: root.theme.motionEnabled ? root.theme.motionDuration : 0; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: root.theme.motionEnabled ? root.theme.motionDuration : 0 } }
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 10
            Text
            {
                Layout.preferredWidth: 105
                text: root.rangeInfo.address || "A1"
                color: root.theme.accent
                font.bold: true
                font.family: root.theme.fontFamily
            }
            TextField
            {
                id: valueEditor
                objectName: "spreadsheetValueEditor"
                Layout.fillWidth: true
                text: root.draft
                enabled: !root.busy
                readOnly: !root.cellInfo.canEdit
                maximumLength: 32767
                selectByMouse: true
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize
                placeholderText: root.cellInfo.canEdit ? "输入内容，按 Enter 确认" : "此单元格只读"
                onTextEdited: { const input = text; if (root.startEditing()) root.draft = input; }
                onAccepted: { if (root.finishCellEditing()) grid.forceActiveFocus(); }
                Keys.onEscapePressed: function(event) { root.cancelCellEditing(); event.accepted = true; }
                background: Rectangle { color: root.theme.surfaceColor; radius: root.theme.radius * 0.5; border.color: valueEditor.activeFocus ? root.theme.accent : root.theme.borderColor }
            }
            ActionButton
            {
                theme: root.theme
                text: "确认"
                iconName: "check"
                compact: true
                enabled: root.editing && !root.busy
                onClicked: { if (root.finishCellEditing()) grid.forceActiveFocus(); }
            }
        }

        Text
        {
            Layout.fillWidth: true
            visible: root.error.length > 0 || (!root.cellInfo.canEdit && (root.cellInfo.reason || "").length > 0)
            text: root.error.length > 0 ? root.error : (root.cellInfo.reason || "")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 1
        }

        Item
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 180

            HorizontalHeaderView
            {
                id: columnsHeader
                anchors.left: rowsHeader.right
                anchors.right: parent.right
                anchors.top: parent.top
                height: 30
                syncView: grid
                clip: true
                delegate: Rectangle
                {
                    id: columnHeaderCell
                    required property int column
                    required property var display
                    implicitWidth: 132
                    implicitHeight: 30
                    readonly property bool inRange: column >= root.rangeInfo.firstColumn && column <= root.rangeInfo.lastColumn
                    color: inRange ? root.theme.sheetsSelectionHeader : root.theme.accentSoft
                    border.color: root.theme.sheetsGridLine
                    Text { anchors.centerIn: parent; text: parent.display; color: parent.inRange ? root.theme.sheetsSelectionHeaderText : root.theme.textSecondary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize - 1 }
                    MouseArea
                    {
                        anchors.fill: parent
                        preventStealing: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: function(mouse)
                        {
                            if (!root.finishCellEditing()) { mouse.accepted = false; return; }
                            root.draggingBand = mouse.button === Qt.LeftButton;
                            root.bandAnchor = columnHeaderCell.column;
                            root.bandRows = false;
                            root.bandRequested(root.bandAnchor, root.bandAnchor, false);
                            if (mouse.button === Qt.RightButton) cellMenu.popup();
                        }
                        onPositionChanged: function(mouse)
                        {
                            if (!(pressedButtons & Qt.LeftButton)) return;
                            const p = mapToItem(columnsHeader.contentItem, mouse.x, mouse.y);
                            const c = columnsHeader.cellAtPosition(p, true);
                            if (c.x >= 0) root.bandRequested(root.bandAnchor, c.x, false);
                        }
                        onReleased: root.draggingBand = false
                        onCanceled: root.draggingBand = false
                    }
                }
            }

            VerticalHeaderView
            {
                id: rowsHeader
                anchors.left: parent.left
                anchors.top: columnsHeader.bottom
                anchors.bottom: parent.bottom
                width: 58
                syncView: grid
                clip: true
                delegate: Rectangle
                {
                    id: rowHeaderCell
                    required property int row
                    required property var display
                    implicitWidth: 58
                    implicitHeight: 34
                    readonly property bool inRange: row >= root.rangeInfo.firstRow && row <= root.rangeInfo.lastRow
                    color: inRange ? root.theme.sheetsSelectionHeader : root.theme.accentSoft
                    border.color: root.theme.sheetsGridLine
                    Text { anchors.centerIn: parent; text: parent.display; color: parent.inRange ? root.theme.sheetsSelectionHeaderText : root.theme.textSecondary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize - 1 }
                    MouseArea
                    {
                        anchors.fill: parent
                        preventStealing: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: function(mouse)
                        {
                            if (!root.finishCellEditing()) { mouse.accepted = false; return; }
                            root.draggingBand = mouse.button === Qt.LeftButton;
                            root.bandAnchor = rowHeaderCell.row;
                            root.bandRows = true;
                            root.bandRequested(root.bandAnchor, root.bandAnchor, true);
                            if (mouse.button === Qt.RightButton) cellMenu.popup();
                        }
                        onPositionChanged: function(mouse)
                        {
                            if (!(pressedButtons & Qt.LeftButton)) return;
                            const p = mapToItem(rowsHeader.contentItem, mouse.x, mouse.y);
                            const c = rowsHeader.cellAtPosition(p, true);
                            if (c.y >= 0) root.bandRequested(root.bandAnchor, c.y, true);
                        }
                        onReleased: root.draggingBand = false
                        onCanceled: root.draggingBand = false
                    }
                }
            }

            TableView
            {
                id: grid
                objectName: "spreadsheetGrid"
                anchors.left: rowsHeader.right
                anchors.right: parent.right
                anchors.top: columnsHeader.bottom
                anchors.bottom: parent.bottom
                model: root.tableModel
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                columnSpacing: 0
                rowSpacing: 0
                columnWidthProvider: function(column) { return root.columnWidthFor(column) * root.zoom; }
                rowHeightProvider: function(row) { return root.rowHeightFor(row) * root.zoom; }
                reuseItems: true
                enabled: !root.busy
                ScrollBar.horizontal: ScrollBar { }
                ScrollBar.vertical: ScrollBar { }

                Keys.onPressed: function(event)
                {
                    if (event.modifiers & Qt.ControlModifier && event.key === Qt.Key_C) root.copyRequested();
                    else if (event.modifiers & Qt.ControlModifier && event.key === Qt.Key_X) root.startToolRequested("cut", {});
                    else if (event.modifiers & Qt.ControlModifier && event.key === Qt.Key_V) root.pasteRequested();
                    else if (event.key === Qt.Key_Delete) root.clearRequested();
                    else if (event.key === Qt.Key_Left) root.moveCell(0, -1, Boolean(event.modifiers & Qt.ShiftModifier));
                    else if (event.key === Qt.Key_Right) root.moveCell(0, 1, Boolean(event.modifiers & Qt.ShiftModifier));
                    else if (event.key === Qt.Key_Tab) root.moveCell(0, event.modifiers & Qt.ShiftModifier ? -1 : 1);
                    else if (event.key === Qt.Key_Up) root.moveCell(-1, 0, Boolean(event.modifiers & Qt.ShiftModifier));
                    else if (event.key === Qt.Key_Down) root.moveCell(1, 0, Boolean(event.modifiers & Qt.ShiftModifier));
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_F2)
                    {
                        if (root.startEditing()) { valueEditor.forceActiveFocus(); valueEditor.selectAll(); }
                    }
                    else { return; }
                    event.accepted = true;
                }

                delegate: gridCell
            }
            Item
            {
                x: grid.x; y: columnsHeader.y; width: Math.min(grid.width, Number(root.layoutInfo.frozenWidth || 0) * root.zoom); height: columnsHeader.height
                clip: true; z: 3
                Row
                {
                    Repeater
                    {
                        model: root.layoutInfo.frozenColumnWidths || []
                        delegate: Rectangle
                        {
                            id: fixedColumn
                            required property int index
                            required property real modelData
                            width: modelData * root.zoom; height: columnsHeader.height
                            color: root.theme.accentSoft; border.color: root.theme.sheetsGridLine
                            Text { anchors.centerIn: parent; text: String.fromCharCode(65 + fixedColumn.index); color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                            TapHandler { onTapped: { if (root.finishCellEditing()) root.bandRequested(fixedColumn.index, fixedColumn.index, false); } }
                        }
                    }
                }
            }
            Item
            {
                x: rowsHeader.x; y: grid.y; width: rowsHeader.width; height: Math.min(grid.height, Number(root.layoutInfo.frozenHeight || 0) * root.zoom)
                clip: true; z: 3
                Column
                {
                    Repeater
                    {
                        model: root.layoutInfo.frozenRowHeights || []
                        delegate: Rectangle
                        {
                            id: fixedRow
                            required property int index
                            required property real modelData
                            width: rowsHeader.width; height: modelData * root.zoom
                            color: root.theme.accentSoft; border.color: root.theme.sheetsGridLine
                            Text { anchors.centerIn: parent; text: fixedRow.index + 1; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                            TapHandler { onTapped: { if (root.finishCellEditing()) root.bandRequested(fixedRow.index, fixedRow.index, true); } }
                        }
                    }
                }
            }
            SpreadsheetMergeLayer
            {
                anchors.fill: grid; theme: root.theme; merges: root.layoutInfo.merges || []; zoom: root.zoom
                scrollX: grid.contentX; scrollY: grid.contentY; selected: root.cellInfo; enabled: !root.busy
                onSelectedCell: function(row, column, extend) { root.chooseCell(row, column, extend); }
                onEditRequested: function(row, column) { if (root.chooseCell(row, column) && root.startEditing()) { valueEditor.forceActiveFocus(); valueEditor.selectAll(); } }
            }

            TableView
            {
                id: frozenTop; objectName: "frozenTop"; x: grid.x; y: grid.y; width: grid.width; height: Math.min(grid.height, Number(root.layoutInfo.frozenHeight || 0) * root.zoom)
                visible: width > 0 && height > 0; clip: true; interactive: false; enabled: !root.busy
                model: root.tableModel; delegate: gridCell; reuseItems: true
                contentX: grid.contentX; contentY: 0
                columnWidthProvider: function(column) { return root.columnWidthFor(column) * root.zoom; }
                rowHeightProvider: function(row) { return root.rowHeightFor(row) * root.zoom; }
            }
            SpreadsheetMergeLayer
            {
                anchors.fill: frozenTop; visible: frozenTop.visible; theme: root.theme; merges: root.layoutInfo.merges || []; zoom: root.zoom
                scrollX: grid.contentX; scrollY: 0; selected: root.cellInfo; enabled: !root.busy
                onSelectedCell: function(row, column, extend) { root.chooseCell(row, column, extend); }
                onEditRequested: function(row, column) { if (root.chooseCell(row, column) && root.startEditing()) { valueEditor.forceActiveFocus(); valueEditor.selectAll(); } }
            }

            TableView
            {
                id: frozenLeft; objectName: "frozenLeft"; x: grid.x; y: grid.y; width: Math.min(grid.width, Number(root.layoutInfo.frozenWidth || 0) * root.zoom); height: grid.height
                visible: width > 0 && height > 0; clip: true; interactive: false; enabled: !root.busy
                model: root.tableModel; delegate: gridCell; reuseItems: true
                contentX: 0; contentY: grid.contentY
                columnWidthProvider: function(column) { return root.columnWidthFor(column) * root.zoom; }
                rowHeightProvider: function(row) { return root.rowHeightFor(row) * root.zoom; }
            }
            SpreadsheetMergeLayer
            {
                anchors.fill: frozenLeft; visible: frozenLeft.visible; theme: root.theme; merges: root.layoutInfo.merges || []; zoom: root.zoom
                scrollX: 0; scrollY: grid.contentY; selected: root.cellInfo; enabled: !root.busy
                onSelectedCell: function(row, column, extend) { root.chooseCell(row, column, extend); }
                onEditRequested: function(row, column) { if (root.chooseCell(row, column) && root.startEditing()) { valueEditor.forceActiveFocus(); valueEditor.selectAll(); } }
            }

            TableView
            {
                id: frozenCorner; objectName: "frozenCorner"; x: grid.x; y: grid.y; width: Math.min(grid.width, Number(root.layoutInfo.frozenWidth || 0) * root.zoom); height: Math.min(grid.height, Number(root.layoutInfo.frozenHeight || 0) * root.zoom)
                visible: width > 0 && height > 0; clip: true; interactive: false; enabled: !root.busy
                model: root.tableModel; delegate: gridCell; reuseItems: true
                contentX: 0; contentY: 0
                columnWidthProvider: function(column) { return root.columnWidthFor(column) * root.zoom; }
                rowHeightProvider: function(row) { return root.rowHeightFor(row) * root.zoom; }
            }
            SpreadsheetMergeLayer
            {
                anchors.fill: frozenCorner; visible: frozenCorner.visible; theme: root.theme; merges: root.layoutInfo.merges || []; zoom: root.zoom
                scrollX: 0; scrollY: 0; selected: root.cellInfo; enabled: !root.busy
                onSelectedCell: function(row, column, extend) { root.chooseCell(row, column, extend); }
                onEditRequested: function(row, column) { if (root.chooseCell(row, column) && root.startEditing()) { valueEditor.forceActiveFocus(); valueEditor.selectAll(); } }
            }

        }

        RowLayout
        {
            Layout.fillWidth: true
            ScrollView
            {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                contentWidth: sheetTabs.implicitWidth
                contentHeight: 36
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                Row
                {
                    id: sheetTabs
                    spacing: 5
                    Repeater
                    {
                        model: root.sheetNames
                        ActionButton
                        {
                            id: sheetTab
                            required property int index
                            required property string modelData
                            theme: root.theme
                            text: modelData
                            visible: !root.layoutInfo.hiddenSheetIndices || root.layoutInfo.hiddenSheetIndices.indexOf(index) < 0
                            primary: index === root.currentSheet
                            compact: true
                            enabled: !root.busy
                            onClicked: { if (root.finishCellEditing()) root.sheetRequested(index); }
                            TapHandler
                            {
                                acceptedButtons: Qt.RightButton
                                onTapped:
                                {
                                    if (!root.finishCellEditing()) return;
                                    root.sheetRequested(sheetTab.index);
                                    sheetMenu.popup();
                                }
                            }
                        }
                    }
                }
            }
            ActionButton { theme: root.theme; text: "+ 工作表"; compact: true; enabled: !root.busy; onClicked: { if (root.finishCellEditing()) root.addSheetRequested(""); } }
            ActionButton { theme: root.theme; text: "打开表格"; compact: true; enabled: !root.busy; onClicked: { if (root.prepareNavigation("open")) root.openRequested(); } }
        }
        RowLayout
        {
            Layout.fillWidth: true
            Text
            {
                Layout.fillWidth: true
                text: "已选 " + root.rangeInfo.address + " · " + (root.rangeInfo.lastRow - root.rangeInfo.firstRow + 1) + " 行 × " + (root.rangeInfo.lastColumn - root.rangeInfo.firstColumn + 1) + " 列 · " + root.rangeInfo.count + " 格 · 非空 " + root.rangeInfo.nonempty
                    + (root.rangeInfo.numbers > 0 ? " · 求和 " + root.rangeInfo.sum + " · 平均 " + root.rangeInfo.average + "（不含公式）" : "")
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 1
            }
            ZoomControl { theme: root.theme; zoom: root.zoom; enabled: !root.busy; onZoomRequested: function(value) { root.zoomRequested(value); } }
            ActionButton { theme: root.theme; text: "支持范围"; iconName: "arrow"; compact: true; onClicked: compatibilityDialog.open() }
        }
    }

    Menu
    {
        id: cellMenu
        MenuItem { text: "复制选区"; onTriggered: root.copyRequested() }
        MenuItem { text: "剪切选区"; onTriggered: { if (root.finishCellEditing()) root.startToolRequested("cut", {}); } }
        MenuItem { text: "粘贴"; onTriggered: root.pasteRequested() }
        MenuSeparator { }
        MenuItem { text: "清空内容"; onTriggered: root.clearRequested() }
        MenuItem { text: "设置单元格样式…"; onTriggered: { root.activeGroup = "appearance"; root.activeSection = "font"; } }
        MenuItem { text: "调整行高 / 列宽…"; onTriggered: { root.activeGroup = "sheet"; root.activeSection = "size"; } }
        MenuSeparator { }
        MenuItem { text: "撤销"; enabled: root.canUndo; onTriggered: root.undoRequested() }
        MenuItem { text: "重做"; enabled: root.canRedo; onTriggered: root.redoRequested() }
    }
    Menu
    {
        id: sheetMenu
        MenuItem { text: "新建工作表"; onTriggered: root.addSheetRequested("") }
        MenuItem { text: "重命名…"; onTriggered: { sheetName.text = root.sheetNames[root.currentSheet]; sheetNameDialog.open(); sheetName.selectAll(); sheetName.forceActiveFocus(); } }
        MenuItem { text: "复制工作表"; onTriggered: root.startToolRequested("copySheet", {}) }
        MenuItem { text: "隐藏工作表"; onTriggered: root.startToolRequested("hideSheet", {}) }
        MenuItem { text: "移动、显示或删除…"; onTriggered: { root.activeGroup = "sheet"; root.activeSection = "switch"; } }
        MenuItem { text: "打开其他表格文件…"; onTriggered: root.openRequested() }
    }
    RoundedDialog
    {
        id: sheetNameDialog
        theme: root.theme
        parent: root
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        title: "重命名工作表"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: TextField { id: sheetName; maximumLength: 31; selectByMouse: true; onAccepted: sheetNameDialog.accept() }
        onAccepted: root.renameSheetRequested(sheetName.text)
    }

    RoundedDialog
    {
        id: draftDialog
        parent: root
        theme: root.theme
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(500, root.width - 60)
        title: "这次输入尚未提交"
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout
        {
            spacing: 18
            Text
            {
                Layout.fillWidth: true
                text: "当前输入不符合所选类型。可以继续修改，或放弃这个单元格的本次输入后离开。其他已修改内容仍会询问是否保存。"
                wrapMode: Text.Wrap
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
            }
            RowLayout
            {
                Layout.alignment: Qt.AlignRight
                ActionButton { theme: root.theme; text: "继续编辑"; primary: true; onClicked: root.resolveDraftNavigation(false) }
                ActionButton { theme: root.theme; text: "放弃本次输入并继续"; onClicked: root.resolveDraftNavigation(true) }
            }
        }
    }

    RoundedDialog
    {
        id: compatibilityDialog
        theme: root.theme
        parent: root
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(520, root.width - 60)
        title: "表格 · 当前支持范围"
        modal: true
        contentItem: ColumnLayout
        {
            spacing: 20
            Text { Layout.fillWidth: true; text: root.compatibilitySummary; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize }
            ActionButton { Layout.alignment: Qt.AlignRight; theme: root.theme; text: "知道了"; iconName: "check"; onClicked: compatibilityDialog.close() }
        }
    }
}
