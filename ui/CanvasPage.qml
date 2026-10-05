pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import Mirrorfly.Native
import "components"

FocusScope
{
    id: root
    objectName: "CanvasPage"
    property real zoom: 1
    signal zoomRequested(real value)
    property var theme
    property real chromeInset: 0
    property string kind: "mindmap"
    property string documentName: ""
    property string message: ""
    property bool modified: false
    property bool busy: false
    property bool canUndo: false
    property bool canRedo: false
    property var view: ({})
    property var preview
    property var commandHandler: function(action, args) { return false; }
    property string group: ""
    property string section: ""
    property string nodeDraft: view.selectedText || ""
    property string annotationMode: ""
    property url stampImage
    property point annotationStart: Qt.point(-1, -1)
    property point annotationEnd: Qt.point(-1, -1)
    property string connectionMode: "off"
    readonly property bool connecting: connectionMode !== "off"
    function connectionHandler(mode, from) { return false; }
    function connectHandler(target) { return false; }
    property string connectFrom: ""
    property string selectedEdge: ""
    property string dragId: ""
    property real dragX: 0
    property real dragY: 0
    property real edgeX: 0
    property real edgeY: 0
    property bool pointerActive: false
    property string colorTarget: "border"
    readonly property bool pdf: kind === "pdf"
    readonly property bool editing: !pdf && nodeDraft !== (view.selectedText || "")
    readonly property bool modalActive: pointerActive || graphMenu.opened || nodeColor.visible || pdfMenu.opened || stampImageDialog.visible
    signal homeRequested()
    signal openRequested()
    signal newRequested()
    signal saveRequested()
    signal pdfRequested()
    signal saveAsRequested()
    signal undoRequested()
    signal redoRequested()
    signal pageRequested(int index)
    signal nodeRequested(string id)
    signal renderSizeRequested(int width, int height)
    signal copyOutlineRequested()
    onGroupChanged: section = ""
    onViewChanged: { nodeDraft = view.selectedText || ""; }
    onKindChanged: { selectedEdge = ""; dragId = ""; mapViewport.contentX = 0; mapViewport.contentY = 0; }

    function nodeAt(x, y)
    {
        const nodes = view.nodes || [];
        for (let i = nodes.length - 1; i >= 0; --i)
        {
            const n = nodes[i];
            if (x >= n.x && x <= n.x + n.width && y >= n.y && y <= n.y + n.height) return n;
        }
        return null;
    }
    function edgeAt(x, y)
    {
        return mapRenderer.edgeAt(x, y);
    }

    function createNode()
    {
        apply("createNode", {x: Math.min(48000, mapViewport.contentX / root.zoom + 80), y: Math.min(48000, mapViewport.contentY / root.zoom + 80 + (view.nodeCount % 5) * 110)});
    }

    function prepareNavigation(action = "")
    {
        if (busy) return false;
        if (!editing) return true;
        const success = commandHandler("rename", {text: nodeDraft});
        if (!success) nodeEditor.forceActiveFocus();
        return success;
    }
    function apply(action, args = ({}))
    {
        if (!prepareNavigation()) return false;
        return commandHandler(action, args);
    }
    function selectNodeAt(x, y, edit)
    {
        const nodes = view.nodes || [];
        for (let i = 0; i < nodes.length; ++i)
        {
            const n = nodes[i];
            if (x >= n.x && x <= n.x + n.width && y >= n.y && y <= n.y + n.height)
            {
                if (!prepareNavigation()) return;
                nodeRequested(n.id);
                if (edit) { nodeEditor.forceActiveFocus(); nodeEditor.selectAll(); }
                else mapViewport.forceActiveFocus();
                return;
            }
        }
    }

    Rectangle { anchors.fill: parent; color: root.theme.backgroundColor }
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
            ActionButton { theme: root.theme; text: "首页"; iconName: "back"; compact: true; enabled: !root.busy; onClicked: { if (root.prepareNavigation()) root.homeRequested(); } }
            Text { Layout.fillWidth: true; text: root.documentName + (root.modified || root.editing ? " · 未保存" : ""); textFormat: Text.PlainText; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize + 5; elide: Text.ElideMiddle }
            ActionButton { theme: root.theme; text: "打开"; compact: true; enabled: !root.busy; onClicked: { if (root.prepareNavigation()) root.openRequested(); } }
            ActionButton { theme: root.theme; text: root.pdf ? "保存副本" : "保存"; primary: true; compact: true; enabled: !root.busy; onClicked: { if (root.prepareNavigation()) root.saveRequested(); } }
        }
        Rectangle
        {
            Layout.fillWidth: true
            implicitHeight: tools.implicitHeight + 20
            color: root.theme.surfaceColor
            radius: root.theme.radius
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
                        model: root.pdf ? [{key: "page", text: "页面"}, {key: "annotation", text: "批注"}, {key: "file", text: "文件"}]
                            : [{key: "node", text: "创建"}, {key: "links", text: "连接"}, {key: "look", text: "节点样式"}, {key: "file", text: "文件"}]
                        delegate: ActionButton
                        {
                            required property var modelData
                            theme: root.theme; text: modelData.text; compact: true; iconName: "chevron"
                            primary: root.group === modelData.key
                            enabled: !root.busy
                            onClicked: root.group = root.group === modelData.key ? "" : modelData.key
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton { theme: root.theme; text: "撤销"; compact: true; enabled: root.canUndo && !root.busy; onClicked: { if (root.prepareNavigation()) root.undoRequested(); } }
                    ActionButton { theme: root.theme; text: "重做"; compact: true; enabled: root.canRedo && !root.busy; onClicked: { if (root.prepareNavigation()) root.redoRequested(); } }
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
                            model: root.group === "page" ? [{key: "navigate", text: "翻页"}, {key: "arrange", text: "页面整理"}, {key: "text", text: "提取文字"}]
                                : root.group === "annotation" ? [{key: "add", text: "便笺与高亮"}, {key: "label", text: "文字与水印"}, {key: "image", text: "图片标注"}, {key: "notes", text: "标注列表"}]
                                : root.group === "node" ? [{key: "insert", text: "添加主题"}, {key: "remove", text: "删除主题"}]
                                : root.group === "links" ? [{key: "connect", text: "连接节点"}, {key: "disconnect", text: "管理连线"}]
                                : root.group === "look" ? [{key: "shape", text: "边框与形状"}, {key: "nodeSize", text: "节点尺寸"}]
                                : root.group === "structure" ? [{key: "order", text: "顺序与折叠"}, {key: "parent", text: "移动分支"}, {key: "free", text: "升级自由画布"}]
                                : [{key: "save", text: "新建与另存"}, {key: "support", text: "支持范围"}]
                            delegate: ActionButton
                            {
                                required property var modelData
                                theme: root.theme; text: modelData.text; compact: true; iconName: "arrow"
                                primary: root.section === modelData.key
                                onClicked: root.section = root.section === modelData.key ? "" : modelData.key
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
                Item
                {
                    id: parameterFrame
                    Layout.fillWidth: true
                    visible: root.section.length > 0
                    implicitHeight: parameters.implicitHeight + 16
                    ColumnLayout
                    {
                        id: parameters
                        x: 10; y: 8; width: parent.width - 20
                        enabled: !root.busy
                        RowLayout
                        {
                            visible: root.section === "navigate"
                            ActionButton { theme: root.theme; text: "上一页"; compact: true; enabled: root.view.page > 0; onClicked: root.pageRequested(root.view.page - 1) }
                            SpinBox { id: pageIndex; from: 1; to: Math.max(1, root.view.pageCount || 1); value: (root.view.page || 0) + 1; editable: true; onValueModified: root.pageRequested(value - 1) }
                            ActionButton { theme: root.theme; text: "下一页"; compact: true; enabled: root.view.page + 1 < root.view.pageCount; onClicked: root.pageRequested(root.view.page + 1) }
                        }
                        RowLayout
                        {
                            visible: root.section === "arrange"
                            enabled: root.view.editable || false
                            ActionButton { theme: root.theme; text: "顺时针旋转"; compact: true; onClicked: root.apply("rotate", {turns: 1}) }
                            ActionButton { theme: root.theme; text: "向前移动"; compact: true; enabled: root.view.page > 0; onClicked: root.apply("movePage", {index: root.view.page - 1}) }
                            ActionButton { theme: root.theme; text: "向后移动"; compact: true; enabled: root.view.page + 1 < root.view.pageCount; onClicked: root.apply("movePage", {index: root.view.page + 1}) }
                            ActionButton { theme: root.theme; text: "删除当前页"; compact: true; enabled: root.view.pageCount > 1; onClicked: root.apply("deletePage") }
                        }
                        RowLayout
                        {
                            visible: root.section === "add"
                            enabled: root.view.editable || false
                            TextField { id: noteText; implicitWidth: 300; placeholderText: "批注内容（可选）"; maximumLength: 2000 }
                            ActionButton { theme: root.theme; text: "文字批注"; compact: true; primary: root.annotationMode === "note"; onClicked: root.annotationMode = root.annotationMode === "note" ? "" : "note" }
                            ActionButton { theme: root.theme; text: "区域高亮"; compact: true; primary: root.annotationMode === "highlight"; onClicked: root.annotationMode = root.annotationMode === "highlight" ? "" : "highlight" }
                            Text { text: root.annotationMode.length ? "在页面点击或拖动框选，可撤销" : "选择工具后再点页面"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            visible: root.section === "label"
                            enabled: root.view.editable || false
                            TextField { id: labelText; implicitWidth: 220; placeholderText: "可见文字或水印内容"; maximumLength: 2000 }
                            SpinBox { id: labelSize; from: 6; to: 96; value: 18; editable: true }
                            ActionButton { theme: root.theme; text: "放置文字"; compact: true; primary: root.annotationMode === "label"; onClicked: root.annotationMode = "label" }
                            ActionButton { theme: root.theme; text: "本页水印"; compact: true; onClicked: root.apply("watermark", {x: 0.1, y: 0.1, width: 0.8, height: 0.8, text: labelText.text, size: labelSize.value, opacity: 0.22}) }
                        }
                        RowLayout
                        {
                            visible: root.section === "image"
                            enabled: root.view.editable || false
                            ActionButton { theme: root.theme; text: "选择图片…"; compact: true; onClicked: stampImageDialog.open() }
                            Text { text: root.annotationMode === "image" ? "在页面拖动框选图片位置" : "PNG / JPEG · 保持比例"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            visible: root.section === "notes"
                            ComboBox { id: noteList; implicitWidth: 480; model: root.view.annotations || []; textRole: "text"; displayText: count ? (currentText || "无文字批注") : "当前页没有批注" }
                            ActionButton { theme: root.theme; text: "删除批注"; compact: true; enabled: (root.view.editable || false) && noteList.count > 0; onClicked: root.apply("deleteAnnotation", {id: root.view.annotations[noteList.currentIndex].id}) }
                        }
                        RowLayout
                        {
                            visible: root.section === "text"
                            ActionButton { theme: root.theme; text: "复制本页文字"; compact: true; onClicked: root.copyOutlineRequested() }
                            Text { text: root.view.textTruncated ? "本页文字较长，最多复制 64 KiB" : "读取现有文字层 · 扫描图片暂不识别"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            visible: root.section === "insert"
                            ActionButton { theme: root.theme; text: "独立节点 · Enter"; compact: true; onClicked: root.createNode() }
                            ActionButton { theme: root.theme; text: "连接新节点 · Tab"; compact: true; onClicked: root.apply("addChild") }
                            Text { text: "双击主题，编辑下方输入栏"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            visible: root.section === "remove"
                            ActionButton { theme: root.theme; text: "删除当前节点"; compact: true; onClicked: root.apply("deleteNode") }
                            Text { text: "移除相关连线，其他节点保留 · 可撤销"; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            visible: root.section === "connect"
                            ActionButton { theme: root.theme; text: "自动排版"; compact: true; enabled: !root.busy; onClicked: root.apply("autoLayout") }
                            ActionButton { theme: root.theme; text: root.connecting ? "结束连接 · Esc" : "开始连接"; primary: root.connecting; compact: true; onClicked: { root.connectionHandler(root.connecting ? "off" : "continuous", ""); } }
                            Text { Layout.fillWidth: true; text: "依次点击起点、终点，或从起点拖到终点；允许循环关系"; wrapMode: Text.Wrap; color: root.theme.textSecondary; font.family: root.theme.fontFamily }
                        }
                        RowLayout
                        {
                            visible: root.section === "disconnect"
                            ComboBox { id: edgeList; Layout.preferredWidth: 320; model: root.view.edges || []; textRole: "display"; onActivated: root.selectedEdge = root.view.edges[currentIndex].id }
                            ActionButton { theme: root.theme; text: "断开选中连线"; enabled: root.selectedEdge.length > 0; compact: true; onClicked: { if (root.apply("disconnect", {id: root.selectedEdge})) root.selectedEdge = ""; } }
                        }
                        RowLayout
                        {
                            visible: root.section === "shape"
                            ComboBox { implicitWidth: 135; model: ["圆角矩形", "矩形", "菱形", "椭圆"]; currentIndex: Math.max(0, ["rounded", "rectangle", "diamond", "ellipse"].indexOf((root.view.selectedStyle || {}).shape || "rounded")); onActivated: root.apply("styleNode", {shape: ["rounded", "rectangle", "diamond", "ellipse"][currentIndex]}) }
                            ActionButton { theme: root.theme; text: "边框颜色…"; compact: true; onClicked: { root.colorTarget = "border"; nodeColor.open(); } }
                            ActionButton { theme: root.theme; text: "填充颜色…"; compact: true; onClicked: { root.colorTarget = "fill"; nodeColor.open(); } }
                            Label { text: "线宽"; color: root.theme.textSecondary }
                            SpinBox { from: 1; to: 6; value: (root.view.selectedStyle || {}).stroke || 2; onValueModified: root.apply("styleNode", {stroke: value}) }
                            ActionButton { theme: root.theme; text: "恢复配色"; compact: true; onClicked: root.apply("styleNode", {border: "", fill: "", stroke: 2}) }
                        }
                        RowLayout
                        {
                            visible: root.section === "nodeSize"
                            Label { text: "宽"; color: root.theme.textSecondary }
                            SpinBox { from: 64; to: 1000; stepSize: 10; editable: true; value: (root.view.selectedStyle || {}).width || 220; onValueModified: root.apply("styleNode", {width: value}) }
                            Label { text: "高"; color: root.theme.textSecondary }
                            SpinBox { from: 48; to: 800; stepSize: 10; editable: true; value: (root.view.selectedStyle || {}).height || 84; onValueModified: root.apply("styleNode", {height: value}) }
                        }
                        RowLayout
                        {
                            visible: root.section === "save"
                            ActionButton { theme: root.theme; text: root.pdf ? "打开 PDF" : "新建思维导图"; compact: true; onClicked: { if (root.prepareNavigation()) root.newRequested(); } }
                            ActionButton { theme: root.theme; text: "另存为…"; compact: true; onClicked: { if (root.prepareNavigation()) root.saveAsRequested(); } }
                            ActionButton { theme: root.theme; text: root.pdf ? "导出与压缩" : "导出 PDF"; compact: true; onClicked: { if (root.prepareNavigation()) root.pdfRequested(); } }
                        }
                        Text
                        {
                            Layout.fillWidth: true
                            visible: root.section === "support"
                            text: root.pdf ? "支持翻页、旋转、页面排序与删除、文字批注和区域高亮；保存为 PDF 副本，保留原文。支持可见文字、图片和水印，保存后合并为页面内容；暂不改写原有正文，不提供 OCR、表单或签名编辑。"
                                : "自由画布支持独立节点、拖动、定向连线、循环与混合结构、边框和形状，保存为 .mfg。最多 1000 节点、4000 连线。"
                            wrapMode: Text.Wrap; color: root.theme.textSecondary; font.family: root.theme.fontFamily
                        }
                    }
                }
            }
            SpectrumStroke
            {
                theme: root.theme
                x: tools.x; y: tools.y + (parameterFrame.visible ? parameterFrame.y : subFrame.y)
                width: tools.width; height: parameterFrame.visible ? parameterFrame.height : subFrame.height
                radius: root.theme.radius * 0.6; visible: subFrame.visible
                Behavior on y { NumberAnimation { duration: root.theme.motionEnabled ? root.theme.motionDuration : 0; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: root.theme.motionEnabled ? root.theme.motionDuration : 0 } }
            }
        }
        RowLayout
        {
            Layout.fillWidth: true
            visible: !root.pdf
            TextField
            {
                id: nodeEditor
                Layout.fillWidth: true
                text: root.nodeDraft
                maximumLength: 2048
                enabled: !root.busy && (root.view.editable || false)
                placeholderText: "主题内容 · Enter 确认"
                onTextEdited: root.nodeDraft = text
                onAccepted: { if (root.prepareNavigation()) mapViewport.forceActiveFocus(); }
                Keys.onEscapePressed: { root.nodeDraft = root.view.selectedText || ""; mapViewport.forceActiveFocus(); }
            }
            ActionButton { theme: root.theme; text: "确认修改"; compact: true; enabled: root.editing && !root.busy; onClicked: root.prepareNavigation() }
        }
        Text
        {
            Layout.fillWidth: true
            visible: root.message.length > 0 || Boolean(root.view.reason)
            text: root.message || root.view.reason || ""
            textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.textSecondary; font.family: root.theme.fontFamily
        }
        ZoomControl { Layout.alignment: Qt.AlignRight; theme: root.theme; zoom: root.zoom; enabled: !root.busy; onZoomRequested: function(value) { root.zoomRequested(value); } }
        Item
        {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            clip: true
            readonly property real pageRatio: (root.view.width || 1) / (root.view.height || 1)
            readonly property real pageWidth: Math.min(width, height * pageRatio) * root.zoom
            readonly property real pageHeight: pageWidth / pageRatio
            readonly property real pageX: Math.max(0, (width - pageWidth) / 2) - mapViewport.contentX
            readonly property real pageY: Math.max(0, (height - pageHeight) / 2) - mapViewport.contentY
            function requestRender() { if (root.pdf) root.renderSizeRequested(Math.round(width * 1.5 * root.zoom), Math.round(height * 1.5 * root.zoom)); }
            Connections { target: root; function onZoomChanged() { stage.requestRender(); mapViewport.returnToBounds(); } }
            onWidthChanged: requestRender()
            onHeightChanged: requestRender()
            CanvasRenderer
            {
                id: mapRenderer
                anchors.fill: parent
                image: root.preview
                scene: root.pdf ? ({}) : root.view
                theme: root.theme
                zoom: root.zoom
                offset: Qt.point(mapViewport.contentX, mapViewport.contentY)
                interaction: ({dragId: root.dragId, x: root.dragX, y: root.dragY, sourceId: root.connectFrom, edgeX: root.edgeX, edgeY: root.edgeY, edgeId: root.selectedEdge})
            }
            MouseArea
            {
                anchors.fill: parent
                visible: root.pdf
                enabled: !root.busy
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: root.annotationMode.length > 0 ? Qt.CrossCursor : Qt.ArrowCursor
                function point(mouse)
                {
                    return Qt.point((mouse.x - stage.pageX) / stage.pageWidth,
                        (mouse.y - stage.pageY) / stage.pageHeight);
                }
                onPressed: function(mouse)
                {
                    if (mouse.button === Qt.RightButton) { pdfMenu.popup(); return; }
                    const p = point(mouse);
                    if (!root.annotationMode.length || p.x < 0 || p.y < 0 || p.x >= 1 || p.y >= 1) return;
                    root.annotationStart = p;
                    root.annotationEnd = p;
                    root.pointerActive = true;
                }
                onPositionChanged: function(mouse)
                {
                    if (root.pointerActive) { const p = point(mouse); root.annotationEnd = Qt.point(Math.max(0, Math.min(1, p.x)), Math.max(0, Math.min(1, p.y))); }
                }
                onReleased: function(mouse)
                {
                    if (!root.pointerActive || mouse.button !== Qt.LeftButton) return;
                    root.pointerActive = false;
                    const a = root.annotationStart, b = root.annotationEnd;
                    let w = Math.abs(a.x - b.x), h = Math.abs(a.y - b.y);
                    const x = Math.min(a.x, b.x), y = Math.min(a.y, b.y);
                    if (w < 0.01 || h < 0.01) { w = root.annotationMode === "note" ? 0.045 : 0.3; h = root.annotationMode === "label" || root.annotationMode === "image" ? 0.15 : 0.035; }
                    const args = {x: x, y: y, width: Math.min(w, 1 - x), height: Math.min(h, 1 - y), text: noteText.text};
                    if (root.annotationMode === "label") { args.text = labelText.text; args.size = labelSize.value; }
                    if (root.annotationMode === "image") args.file = String(root.stampImage);
                    if (root.apply(root.annotationMode, args)) root.annotationMode = "";
                }
                onCanceled: root.pointerActive = false
                onWheel: function(wheel)
                {
                    if (wheel.modifiers & Qt.ControlModifier)
                    {
                        root.zoomRequested(Math.max(0.25, Math.min(4, root.zoom + (wheel.angleDelta.y > 0 ? 0.1 : -0.1))));
                        wheel.accepted = true; return;
                    }
                    if (root.zoom > 1)
                    {
                        if (wheel.modifiers & Qt.ShiftModifier) mapViewport.contentX = Math.max(0, Math.min(mapViewport.contentWidth - mapViewport.width, mapViewport.contentX - wheel.angleDelta.y));
                        else mapViewport.contentY = Math.max(0, Math.min(mapViewport.contentHeight - mapViewport.height, mapViewport.contentY - wheel.angleDelta.y));
                        wheel.accepted = true; return;
                    }
                    if (wheel.angleDelta.y < 0 && root.view.page + 1 < root.view.pageCount) root.pageRequested(root.view.page + 1);
                    else if (wheel.angleDelta.y > 0 && root.view.page > 0) root.pageRequested(root.view.page - 1);
                    wheel.accepted = true;
                }
            }
            Rectangle
            {
                visible: root.pdf && root.pointerActive
                x: stage.pageX + Math.min(root.annotationStart.x, root.annotationEnd.x) * stage.pageWidth
                y: stage.pageY + Math.min(root.annotationStart.y, root.annotationEnd.y) * stage.pageHeight
                width: Math.abs(root.annotationStart.x - root.annotationEnd.x) * stage.pageWidth
                height: Math.abs(root.annotationStart.y - root.annotationEnd.y) * stage.pageHeight
                color: "transparent"
                border.color: root.theme.accent
                border.width: 2
            }
            Flickable
            {
                id: mapViewport
                objectName: "canvasViewport"
                z: 2
                anchors.fill: parent
                enabled: !root.busy
                interactive: false
                contentWidth: Math.max(width, root.pdf ? stage.pageWidth : (root.view.width || 0) * root.zoom)
                contentHeight: Math.max(height, root.pdf ? stage.pageHeight : (root.view.height || 0) * root.zoom)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar { }
                ScrollBar.vertical: ScrollBar { }
                Keys.onPressed: function(event)
                {
                    if (event.key === Qt.Key_Tab) root.apply("addChild");
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) root.createNode();
                    else if (event.key === Qt.Key_Delete)
                    {
                        if (root.selectedEdge) { root.apply("disconnect", {id: root.selectedEdge}); root.selectedEdge = ""; }
                        else root.apply("deleteNode");
                    }
                    else if (event.key === Qt.Key_Escape) { root.connectionHandler("off", ""); root.dragId = ""; root.pointerActive = false; }
                    else if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0)
                    {
                        const step = event.modifiers & Qt.ShiftModifier ? 10 : 2;
                        const node = root.view.selectedStyle;
                        root.apply("moveNode", {x: Math.max(0, Math.min(48000, node.x + (event.key === Qt.Key_Left ? -step : event.key === Qt.Key_Right ? step : 0))),
                            y: Math.max(0, Math.min(48000, node.y + (event.key === Qt.Key_Up ? -step : event.key === Qt.Key_Down ? step : 0)))});
                    }
                    else return;
                    event.accepted = true;
                }
            }
            MouseArea
            {
                id: graphPointer
                anchors.fill: parent
                visible: !root.pdf
                enabled: !root.busy
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                preventStealing: true
                hoverEnabled: true
                cursorShape: root.connecting ? Qt.CrossCursor : root.dragId ? Qt.ClosedHandCursor : Qt.ArrowCursor
                property point pressedAt: Qt.point(0, 0)
                property point nodeOrigin: Qt.point(0, 0)
                property point panOrigin: Qt.point(0, 0)
                property bool panning: false
                property bool moved: false
                onPressed: function(mouse)
                {
                    if (!root.prepareNavigation()) { mouse.accepted = false; return; }
                    const x = (mouse.x + mapViewport.contentX) / root.zoom, y = (mouse.y + mapViewport.contentY) / root.zoom;
                    const node = root.nodeAt(x, y);
                    root.selectedEdge = node ? "" : root.edgeAt(x, y);
                    if (node) root.nodeRequested(node.id);
                    if (mouse.button === Qt.RightButton) { graphMenu.popup(); return; }
                    root.pointerActive = true;
                    pressedAt = Qt.point(mouse.x, mouse.y);
                    panOrigin = Qt.point(mapViewport.contentX, mapViewport.contentY);
                    moved = false;
                    panning = mouse.button === Qt.MiddleButton || (!node && !root.connecting && !root.selectedEdge);
                    mapViewport.forceActiveFocus();
                    if (node && root.connecting)
                    {
                        if (!root.connectFrom) root.connectionHandler(root.connectionMode, node.id);
                        root.edgeX = x; root.edgeY = y;
                    }
                    else if (node && !panning)
                    {
                        root.dragId = node.id; nodeOrigin = Qt.point(node.x, node.y);
                        root.dragX = node.x; root.dragY = node.y;
                    }
                }
                onPositionChanged: function(mouse)
                {
                    root.edgeX = (mouse.x + mapViewport.contentX) / root.zoom; root.edgeY = (mouse.y + mapViewport.contentY) / root.zoom;
                    if (!root.pointerActive) return;
                    const dx = mouse.x - pressedAt.x, dy = mouse.y - pressedAt.y;
                    if (Math.hypot(dx, dy) > 3) moved = true;
                    if (root.dragId)
                    {
                        root.dragX = Math.max(0, Math.min(48000, nodeOrigin.x + dx / root.zoom));
                        root.dragY = Math.max(0, Math.min(48000, nodeOrigin.y + dy / root.zoom));
                    }
                    else if (panning)
                    {
                        mapViewport.contentX = Math.max(0, Math.min(mapViewport.contentWidth - mapViewport.width, panOrigin.x - dx));
                        mapViewport.contentY = Math.max(0, Math.min(mapViewport.contentHeight - mapViewport.height, panOrigin.y - dy));
                    }
                }
                onReleased: function(mouse)
                {
                    if (!root.pointerActive) return;
                    root.pointerActive = false;
                    const id = root.dragId;
                    root.dragId = "";
                    if (id && moved) root.apply("moveNode", {id: id, x: root.dragX, y: root.dragY});
                    else if (root.connecting && root.connectFrom)
                    {
                        const target = root.nodeAt((mouse.x + mapViewport.contentX) / root.zoom, (mouse.y + mapViewport.contentY) / root.zoom);
                        if (target && (target.id !== root.connectFrom || moved))
                        {
                            root.connectHandler(target.id);
                        }
                    }
                    panning = false;
                }
                onCanceled: { root.dragId = ""; root.pointerActive = false; panning = false; }
                onDoubleClicked: function(mouse)
                {
                    if (!root.connecting) root.selectNodeAt((mouse.x + mapViewport.contentX) / root.zoom, (mouse.y + mapViewport.contentY) / root.zoom, true);
                }
                onWheel: function(wheel)
                {
                    if (wheel.modifiers & Qt.ControlModifier)
                    {
                        root.zoomRequested(Math.max(0.25, Math.min(4, root.zoom + (wheel.angleDelta.y > 0 ? 0.1 : -0.1))));
                        wheel.accepted = true; return;
                    }
                    if (wheel.modifiers & Qt.ShiftModifier)
                        mapViewport.contentX = Math.max(0, Math.min(mapViewport.contentWidth - mapViewport.width, mapViewport.contentX - wheel.angleDelta.y));
                    else mapViewport.contentY = Math.max(0, Math.min(mapViewport.contentHeight - mapViewport.height, mapViewport.contentY - wheel.angleDelta.y));
                    wheel.accepted = true;
                }
            }
        }
        Text
        {
            text: root.busy ? "正在处理…" : root.pdf ? "第 " + ((root.view.page || 0) + 1) + " / " + (root.view.pageCount || 0) + " 页 · Ctrl + 滚轮缩放"
                : root.connecting ? (root.connectFrom ? "请选择或拖到终点 · Esc 结束连接" : "连接模式：先选择起点 · 支持循环关系")
                : (root.view.nodeCount || 0) + " 个节点 · 拖动节点移动 / 空白处平移 · Enter 创建 · Tab 连接新节点 · 方向键微调"
            color: root.theme.textSecondary; font.family: root.theme.fontFamily; font.pixelSize: root.theme.fontSize - 1
        }
    }
    Menu
    {
        id: graphMenu
        MenuItem { text: "新建节点"; onTriggered: root.createNode() }
        MenuItem { text: "编辑文字"; onTriggered: { nodeEditor.forceActiveFocus(); nodeEditor.selectAll(); } }
        MenuItem { text: "从此节点连接…"; enabled: root.view.editable || false; onTriggered: { root.connectionHandler("once", root.view.selectedId); } }
        MenuItem { text: "节点样式…"; enabled: root.view.editable || false; onTriggered: { root.group = "look"; root.section = "shape"; } }
        MenuSeparator { }
        MenuItem { text: root.selectedEdge ? "断开连线" : "删除节点"; onTriggered: { if (root.selectedEdge) { root.apply("disconnect", {id: root.selectedEdge}); root.selectedEdge = ""; } else root.apply("deleteNode"); } }
        MenuItem { text: "复制文字大纲"; onTriggered: root.copyOutlineRequested() }
        MenuItem { text: "撤销"; enabled: root.canUndo; onTriggered: root.undoRequested() }
    }
    NativeDialogs.ColorDialog
    {
        id: nodeColor
        title: root.colorTarget === "border" ? "节点边框颜色" : "节点填充颜色"
        onAccepted: { const patch = {}; patch[root.colorTarget] = selectedColor.toString(); root.apply("styleNode", patch); }
    }
    NativeDialogs.FileDialog
    {
        id: stampImageDialog
        title: "选择 PDF 标注图片"
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: ["图片 (*.png *.jpg *.jpeg)"]
        onAccepted: { root.stampImage = selectedFile; root.annotationMode = "image"; }
    }
    Menu
    {
        id: pdfMenu
        MenuItem { text: "复制本页文字"; onTriggered: root.copyOutlineRequested() }
        MenuItem { text: "添加便笺"; enabled: root.view.editable || false; onTriggered: { root.group = "annotation"; root.section = "add"; root.annotationMode = "note"; } }
        MenuItem { text: "框选高亮"; enabled: root.view.editable || false; onTriggered: { root.group = "annotation"; root.section = "add"; root.annotationMode = "highlight"; } }
        MenuItem { text: "顺时针旋转"; enabled: root.view.editable || false; onTriggered: root.apply("rotate", {turns: 1}) }
        MenuItem { text: "删除当前页"; enabled: (root.view.editable || false) && root.view.pageCount > 1; onTriggered: root.apply("deletePage") }
        MenuSeparator {}
        MenuItem { text: "导出与压缩…"; onTriggered: root.pdfRequested() }
        MenuItem { text: "撤销"; enabled: root.canUndo; onTriggered: root.undoRequested() }
    }

}
