pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flow
{
    id: root
    required property var theme
    required property var selection
    property bool editable: false
    readonly property var markers: ["disc", "circle", "square", "decimal", "lowerLetter", "upperLetter",
        "lowerRoman", "upperRoman"]
    readonly property var listStyle: selection.listStyle || ({})
    signal formatRequested(string action, var value)
    enabled: editable
    spacing: 6
    RowLayout
    {
        Text { text: "选区独立列表"; color: root.theme.textSecondary }
        ComboBox
        {
            objectName: "wordListMarker"
            model: ["实心圆", "空心圆", "方块", "1, 2, 3", "a, b, c", "A, B, C", "i, ii, iii", "I, II, III"]
            currentIndex: root.selection.list > 0 ? root.markers.indexOf(root.listStyle.marker) : -1
            onActivated: function(index) { root.formatRequested("listMarker", root.markers[index]); }
        }
    }
    RowLayout
    {
        Text { text: "起始编号"; color: root.theme.textSecondary }
        SpinBox
        {
            objectName: "wordListStart"
            from: root.listStyle.marker === "decimal" ? 0 : 1
            to: root.listStyle.marker === "lowerRoman" || root.listStyle.marker === "upperRoman" ? 4999 : 1000000
            editable: true
            enabled: root.selection.list === 2
            value: root.listStyle.start === undefined ? 1 : root.listStyle.start
            onValueModified: root.formatRequested("listStart", value)
        }
    }
}
