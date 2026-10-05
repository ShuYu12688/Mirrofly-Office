pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Effects

Item
{
    id: root

    default property alias content: contentHost.data
    property var theme
    property Window targetWindow
    property bool isolatedLoading: false
    property bool chromeVisible: true
    property bool outlineVisible: true
    property real chromeOpacity: 1
    property real outlineOpacity: 1
    readonly property int titlebarHeight: targetWindow && targetWindow.visibility === Window.FullScreen
        ? 0 : theme.windowTitlebarHeight
    readonly property bool maximized: targetWindow
        && (targetWindow.visibility === Window.Maximized || targetWindow.visibility === Window.FullScreen)
    readonly property real cornerRadius: maximized ? 0 : theme.windowRadius
    readonly property real borderInset: maximized ? 0 : 1
    readonly property var contentInsets:
    ({
        "left": 0,
        "top": 0,
        "right": 0,
        "bottom": 0
    })
    readonly property bool roundedClip: cornerRadius > 0 && visible
        && targetWindow && targetWindow.visibility !== Window.Minimized
    readonly property int resizeMargin: theme.windowResizeMargin
    readonly property var resizeEdges:
    [
        Qt.LeftEdge,
        Qt.RightEdge,
        Qt.TopEdge,
        Qt.BottomEdge,
        Qt.TopEdge | Qt.LeftEdge,
        Qt.TopEdge | Qt.RightEdge,
        Qt.BottomEdge | Qt.LeftEdge,
        Qt.BottomEdge | Qt.RightEdge
    ]
    readonly property var resizeCursors:
    [
        Qt.SizeHorCursor,
        Qt.SizeHorCursor,
        Qt.SizeVerCursor,
        Qt.SizeVerCursor,
        Qt.SizeFDiagCursor,
        Qt.SizeBDiagCursor,
        Qt.SizeBDiagCursor,
        Qt.SizeFDiagCursor
    ]

    function toggleMaximized()
    {
        if (!targetWindow)
        {
            return;
        }

        if (maximized)
        {
            targetWindow.showNormal();
        }
        else
        {
            targetWindow.showMaximized();
        }
    }

    component CaptionButton: AbstractButton
    {
        id: captionButton

        property var theme
        property string operation
        property bool restored: false
        readonly property color glyphColor: operation === "close" && (hovered || down)
            ? theme.onAccent : theme.textSecondary

        implicitWidth: theme.windowControlWidth
        implicitHeight: theme.windowTitlebarHeight - 8
        hoverEnabled: true
        activeFocusOnTab: true
        padding: 0
        Accessible.name: operation === "close" ? "关闭窗口"
            : (operation === "minimize" ? "最小化" : (restored ? "还原窗口" : "最大化"))

        onGlyphColorChanged: glyph.requestPaint()
        onOperationChanged: glyph.requestPaint()
        onRestoredChanged: glyph.requestPaint()

        HoverHandler
        {
            cursorShape: Qt.PointingHandCursor
        }

        background: Rectangle
        {
            radius: captionButton.theme.radius * 0.45
            color: captionButton.hovered || captionButton.down
                ? (captionButton.operation === "close" ? captionButton.theme.accent : captionButton.theme.hoverColor)
                : captionButton.theme.transparentColor
            border.width: captionButton.visualFocus ? 2 : 0
            border.color: captionButton.theme.accent

            Behavior on color
            {
                ColorAnimation
                {
                    duration: captionButton.theme.motionEnabled ? captionButton.theme.hoverDuration : 0
                }
            }
        }

        contentItem: Item
        {
            Canvas
            {
                id: glyph

                anchors.centerIn: parent
                width: 16
                height: 16
                onPaint:
                {
                    const context = getContext("2d");
                    context.clearRect(0, 0, width, height);
                    context.strokeStyle = captionButton.glyphColor;
                    context.lineWidth = 1.2;
                    context.lineCap = "square";
                    context.lineJoin = "miter";
                    context.beginPath();

                    if (captionButton.operation === "minimize")
                    {
                        context.moveTo(3, 8);
                        context.lineTo(13, 8);
                    }
                    else if (captionButton.operation === "close")
                    {
                        context.moveTo(4, 4);
                        context.lineTo(12, 12);
                        context.moveTo(12, 4);
                        context.lineTo(4, 12);
                    }
                    else if (captionButton.restored)
                    {
                        context.moveTo(5, 5);
                        context.lineTo(5, 2.5);
                        context.lineTo(13, 2.5);
                        context.lineTo(13, 10.5);
                        context.lineTo(10.5, 10.5);
                        context.rect(2.5, 5.5, 8, 8);
                    }
                    else
                    {
                        context.rect(3.5, 3.5, 9, 9);
                    }

                    context.stroke();
                }
            }
        }
    }

    Rectangle
    {
        id: clipMask
        objectName: "windowFrameMask"

        width: root.width
        height: root.height
        radius: root.cornerRadius
        color: root.isolatedLoading ? Qt.rgba(0, 0, 0, 0) : Qt.rgba(1, 1, 1, 1)
        visible: false
        antialiasing: true
        layer.enabled: root.roundedClip || root.isolatedLoading
        layer.smooth: true
    }

    Item
    {
        id: frameSurface
        objectName: "windowFrameSurface"

        anchors.fill: parent
        layer.enabled: root.roundedClip || root.isolatedLoading
        layer.smooth: true
        layer.effect: MultiEffect
        {
            maskEnabled: true
            maskSource: clipMask
            autoPaddingEnabled: false
            blurEnabled: false
            shadowEnabled: false
        }

        Rectangle
        {
            anchors.fill: parent
            color: root.theme.backgroundColor
        }

        Item
        {
            id: titlebar
            objectName: "windowFrameTitlebar"

            z: 10

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.titlebarHeight
            visible: root.chromeVisible && root.titlebarHeight > 0
            opacity: root.chromeOpacity

            Item
            {
                anchors.left: parent.left
                anchors.right: captionControls.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom

                DragHandler
                {
                    target: null
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged:
                    {
                        if (active && root.targetWindow)
                        {
                            root.targetWindow.startSystemMove();
                        }
                    }
                }

                TapHandler
                {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.DragThreshold
                    onDoubleTapped: root.toggleMaximized()
                }

            }

            Row
            {
                id: captionControls

                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                enabled: root.targetWindow !== null

                CaptionButton
                {
                    theme: root.theme
                    operation: "minimize"
                    onClicked: root.targetWindow.showMinimized()
                }

                CaptionButton
                {
                    theme: root.theme
                    operation: "maximize"
                    restored: root.maximized
                    onClicked: root.toggleMaximized()
                }

                CaptionButton
                {
                    theme: root.theme
                    operation: "close"
                    onClicked: root.targetWindow.close()
                }
            }

        }

        Item
        {
            id: contentHost

            anchors.fill: parent
            anchors.leftMargin: root.contentInsets.left
            anchors.topMargin: root.contentInsets.top
            anchors.rightMargin: root.contentInsets.right
            anchors.bottomMargin: root.contentInsets.bottom
            clip: true
        }
    }

    Rectangle
    {
        objectName: "windowFrameOutline"
        anchors.fill: parent
        radius: root.cornerRadius
        color: root.theme.transparentColor
        border.width: root.outlineVisible ? root.borderInset : 0
        border.color: root.theme.borderColor
        opacity: root.outlineOpacity
        antialiasing: true
    }

    Repeater
    {
        model: 8

        MouseArea
        {
            required property int index

            x: index === 1 || index === 5 || index === 7
                ? root.width - root.resizeMargin : (index === 2 || index === 3 ? root.resizeMargin : 0)
            y: index === 3 || index === 6 || index === 7
                ? root.height - root.resizeMargin : (index === 0 || index === 1 ? root.resizeMargin : 0)
            width: index === 2 || index === 3
                ? Math.max(0, root.width - root.resizeMargin * 2) : root.resizeMargin
            height: index === 0 || index === 1
                ? Math.max(0, root.height - root.resizeMargin * 2) : root.resizeMargin
            enabled: !root.isolatedLoading && root.targetWindow && !root.maximized
                && root.targetWindow.visibility !== Window.Minimized
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: root.resizeCursors[index]
            onPressed: function(mouse)
            {
                mouse.accepted = root.targetWindow.startSystemResize(root.resizeEdges[index]);
            }
        }
    }
}
