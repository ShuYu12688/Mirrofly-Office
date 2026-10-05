import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout
{
    id: root
    property var theme
    property real zoom: 1
    signal zoomRequested(real value)
    spacing: 6

    function step(direction)
    {
        zoomRequested(Math.max(0.25, Math.min(4, Math.round((zoom + direction * 0.1) * 100) / 100)));
    }

    ActionButton { theme: root.theme; text: "−"; iconName: ""; compact: true; enabled: root.zoom > 0.25; Accessible.name: "缩小视图"; onClicked: root.step(-1) }
    SpinBox
    {
        objectName: "viewZoomPercent"
        from: 25; to: 400; stepSize: 10
        value: Math.round(root.zoom * 100)
        editable: true
        implicitWidth: 115
        Accessible.name: "视图缩放百分比"
        onValueModified: root.zoomRequested(value / 100)
    }
    Label { text: "%"; color: root.theme.textSecondary }
    ActionButton { theme: root.theme; text: "+"; iconName: ""; compact: true; enabled: root.zoom < 4; Accessible.name: "放大视图"; onClicked: root.step(1) }
    ActionButton { theme: root.theme; text: "复位"; iconName: ""; compact: true; onClicked: root.zoomRequested(1) }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; enabled: root.visible && root.enabled; onActivated: root.step(1) }
    Shortcut { sequence: "Ctrl+-"; enabled: root.visible && root.enabled; onActivated: root.step(-1) }
    Shortcut { sequence: "Ctrl+0"; enabled: root.visible && root.enabled; onActivated: root.zoomRequested(1) }
}
