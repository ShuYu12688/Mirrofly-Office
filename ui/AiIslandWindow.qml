pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

Window
{
    id: root
    required property var client
    required property var theme
    required property var screens
    readonly property bool expanded: client.expanded
    readonly property bool compact: client.compact === true
    property bool nativeDragging: false
    property bool dragActive: false
    property point pendingDragPosition: Qt.point(0, 0)
    readonly property var anchorScreen: screens.length > 0 ? screens[0]
        : ({ "x": 0, "y": 0, "width": 800, "height": 600, "availableHeight": 600 })
    property point islandPosition: Qt.point(anchorScreen.x + anchorScreen.width / 2,
        anchorScreen.y + theme.islandTopMargin)
    property point dragOrigin: Qt.point(0, 0)
    readonly property var positionScreen: screenAt(islandPosition)
    readonly property point displayPosition: boundedPosition(islandPosition)

    function screenAt(position)
    {
        let nearest = root.anchorScreen;
        let distance = Infinity;
        for (const screen of root.screens)
        {
            const dx = Math.max(screen.x - position.x, 0, position.x - screen.x - screen.width);
            const dy = Math.max(screen.y - position.y, 0, position.y - screen.y - screen.height);
            if (dx * dx + dy * dy < distance)
            {
                nearest = screen;
                distance = dx * dx + dy * dy;
            }
        }
        return nearest;
    }

    function beginDrag()
    {
        root.dragOrigin = Qt.point(root.x + root.width / 2, root.y);
    }

    function boundedPosition(target)
    {
        const screen = root.screenAt(target);
        const left = screen.availableX !== undefined ? screen.availableX : screen.x;
        const top = screen.availableY !== undefined ? screen.availableY : screen.y;
        const availableWidth = screen.availableWidth > 0 ? screen.availableWidth : screen.width;
        const availableHeight = screen.availableHeight > 0 ? screen.availableHeight : screen.height;
        return Qt.point(
            Math.max(left + root.width / 2,
                Math.min(target.x, left + availableWidth - root.width / 2)),
            Math.max(top, Math.min(target.y, top + availableHeight - root.height)));
    }

    function moveDrag(deltaX, deltaY)
    {
        root.pendingDragPosition = root.boundedPosition(
            Qt.point(root.dragOrigin.x + deltaX, root.dragOrigin.y + deltaY));
        if (!dragFrame.running) dragFrame.start();
    }

    function startDrag()
    {
        root.dragActive = true;
        // The native handoff releases the QML mouse grab synchronously.
        root.nativeDragging = typeof root.client.startWindowDrag === "function";
        if (root.nativeDragging) root.nativeDragging = root.client.startWindowDrag();
    }

    function finishDrag()
    {
        if (!root.nativeDragging && dragFrame.running)
        {
            dragFrame.stop();
            root.islandPosition = root.pendingDragPosition;
        }
        if (!root.nativeDragging)
        {
            root.dragActive = false;
            if (root.expanded && !root.compact) focusTimer.restart();
        }
    }

    function activatePrompt()
    {
        if (!root.expanded || root.compact || root.dragActive)
            return;
        root.raise();
        root.requestActivate();
        islandPanel.focusPrompt();
    }

    onExpandedChanged:
    {
        if (expanded)
            focusTimer.restart();
        else
            focusTimer.stop();
    }
    onCompactChanged:
    {
        if (compact) focusTimer.stop();
        else if (expanded) focusTimer.restart();
    }
    onActiveChanged:
    {
        if (active && expanded && !compact)
        {
            root.raise();
            islandPanel.focusPrompt();
        }
    }

    width: expanded ? (compact ? theme.islandCompactWidth : theme.islandPanelWidth) : theme.islandCapsuleWidth
    height: root.expanded ? Math.max(theme.islandCapsuleHeight,
        Math.min(compact ? theme.islandCompactHeight : theme.islandPanelHeight,
            (root.positionScreen.availableHeight > 0 ? root.positionScreen.availableHeight
                : root.positionScreen.height) - 48)) : theme.islandCapsuleHeight
    x: root.displayPosition.x - width / 2
    y: root.displayPosition.y
    visible: client.ready
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint
    title: "Mirrorfly AI"

    Behavior on width
    {
        enabled: !root.dragActive
        NumberAnimation { duration: root.theme.motionEnabled ? 240 : 0; easing.type: Easing.OutCubic }
    }
    Behavior on height
    {
        enabled: !root.dragActive
        NumberAnimation { duration: root.theme.motionEnabled ? 240 : 0; easing.type: Easing.OutCubic }
    }

    Timer
    {
        id: dragFrame
        interval: 16
        onTriggered: root.islandPosition = root.pendingDragPosition
    }

    Connections
    {
        target: typeof root.client.startWindowDrag === "function" ? root.client : null
        function onDragFinished(center_x, top)
        {
            root.islandPosition = Qt.point(center_x, top);
            root.nativeDragging = false;
            root.dragActive = false;
            if (root.expanded && !root.compact) focusTimer.restart();
        }
    }

    Timer
    {
        id: focusTimer
        interval: 40
        repeat: false
        onTriggered: root.activatePrompt()
    }

    Rectangle
    {
        id: capsule
        anchors.fill: parent
        visible: !root.expanded
        radius: height / 2
        color: root.theme.islandSurface
        border.color: root.theme.islandBlue
        border.width: 1

        Image
        {
            anchors.left: parent.left
            anchors.leftMargin: 13
            anchors.verticalCenter: parent.verticalCenter
            width: 34
            height: 34
            source: "../assets/mirrorfly-mark-light.svg"
            fillMode: Image.PreserveAspectFit
        }
        Text
        {
            anchors.left: parent.left
            anchors.leftMargin: 58
            anchors.right: parent.right
            anchors.rightMargin: 43
            anchors.verticalCenter: parent.verticalCenter
            text: root.client.busy ? "正在处理你的想法…" : "问问 Mirrorfly AI"
            color: root.theme.islandText
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize - 1
            elide: Text.ElideRight
        }
        Text
        {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: "↗"
            color: root.theme.islandBlue
            font.pixelSize: 21
        }
        AiIslandDragArea
        {
            objectName: "aiIslandCapsuleDrag"
            threshold: root.theme.islandDragThreshold
            nativeMove: root.nativeDragging
            anchors.fill: parent
            onDragPressed: root.beginDrag()
            onDragStarted: root.startDrag()
            onDragEnded: root.finishDrag()
            onDragMoved: function(deltaX, deltaY) { root.moveDrag(deltaX, deltaY); }
            onTapped: root.client.open()
        }
    }

    AiIslandPanel
    {
        id: islandPanel
        anchors.fill: parent
        visible: root.expanded
        theme: root.theme
        agent: root.client
        compactMode: root.compact
        nativeDragging: root.nativeDragging
        onCloseRequested: root.client.collapse()
        onEditRequested: root.client.open()
        onDragPressed: root.beginDrag()
        onDragStarted: root.startDrag()
        onDragEnded: root.finishDrag()
        onDragMoved: function(deltaX, deltaY) { root.moveDrag(deltaX, deltaY); }
    }

    Instantiator
    {
        model: root.screens
        delegate: ScreenGlowWindow
        {
            required property var modelData
            screenGeometry: modelData
            theme: root.theme
            glowing: root.client.ready && root.expanded
        }
    }
}
