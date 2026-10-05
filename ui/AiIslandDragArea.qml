pragma ComponentBehavior: Bound

import QtQuick

MouseArea
{
    id: root
    required property real threshold
    property bool nativeMove: false
    signal dragPressed()
    signal dragStarted()
    signal dragEnded()
    signal dragMoved(real deltaX, real deltaY)
    signal tapped()
    property point pressPosition: Qt.point(0, 0)
    property bool moved: false

    preventStealing: true
    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
    onPressed: function(mouse)
    {
        root.pressPosition = root.mapToGlobal(mouse.x, mouse.y);
        root.moved = false;
        root.dragPressed();
    }
    onPositionChanged: function(mouse)
    {
        if (!root.pressed)
            return;
        const position = root.mapToGlobal(mouse.x, mouse.y);
        const dx = position.x - root.pressPosition.x;
        const dy = position.y - root.pressPosition.y;
        if (!root.moved && Math.hypot(dx, dy) < root.threshold)
            return;
        if (!root.moved)
        {
            root.moved = true;
            root.dragStarted();
        }
        if (!root.nativeMove) root.dragMoved(dx, dy);
    }
    onReleased: root.dragEnded()
    onCanceled: root.dragEnded()
    onClicked:
    {
        if (!root.moved)
            root.tapped();
    }
}
