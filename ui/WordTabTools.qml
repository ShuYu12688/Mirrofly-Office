pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "components"

Flow
{
    id: root
    required property var theme
    required property var selection
    property bool editable: false
    readonly property var stops: selection.tabStops || []
    readonly property var alignments: ["left", "center", "right", "decimal", "bar"]
    readonly property var leaders: ["none", "dot", "hyphen", "underscore", "heavy", "middleDot"]
    signal formatRequested(string action, var value)
    enabled: editable
    spacing: theme.spacing
    Text
    {
        objectName: "wordTabPreviewWarning"
        width: root.width
        visible: root.selection.tabLayoutSupported === false
        text: root.selection.tabLayoutReason || ""
        color: root.theme.textSecondary
        wrapMode: Text.WordWrap
    }
    function setStop()
    {
        const point = Math.round(position.number * 20) / 20;
        const result = stops.filter(function(stop) { return stop.position !== point; });
        result.push({position: point, alignment: alignments[alignment.currentIndex],
            leader: alignment.currentIndex === 4 ? "none" : leaders[leader.currentIndex]});
        result.sort(function(a, b) { return a.position - b.position; });
        formatRequested("tabStops", result);
    }
    ComboBox
    {
        id: existing
        objectName: "wordExistingTab"
        model: root.stops.map(function(stop) { return stop.position + " pt · " + stop.alignment; })
        onActivated: function(index)
        {
            const stop = root.stops[index];
            position.number = stop.position;
            alignment.currentIndex = Math.max(0, root.alignments.indexOf(stop.alignment));
            leader.currentIndex = Math.max(0, root.leaders.indexOf(stop.leader));
        }
    }
    Text { text: "位置（pt）"; color: root.theme.textSecondary }
    WordNumberSpinBox
    {
        id: position
        objectName: "wordTabPosition"
        number: 72
        minimum: 0
        maximum: 504
        increment: 0.05
        onNumberModified: function(value) { position.number = value; }
        Accessible.name: "制表位置（磅）"
    }
    ComboBox
    {
        id: alignment
        objectName: "wordTabAlignment"
        model: ["左对齐", "居中", "右对齐", "小数点", "竖线"]
        Accessible.name: "制表对齐"
    }
    ComboBox
    {
        id: leader
        objectName: "wordTabLeader"
        model: ["无前导线", "点线", "短划线", "下划线", "粗线", "中点线"]
        enabled: alignment.currentIndex !== 4
        Accessible.name: "制表前导线"
    }
    ActionButton
    {
        objectName: "wordSetTab"
        theme: root.theme; text: "设置"; compact: true
        onClicked: root.setStop()
    }
    ActionButton
    {
        objectName: "wordRemoveTab"
        theme: root.theme; text: "删除所选"; compact: true
        enabled: existing.currentIndex >= 0
        onClicked: root.formatRequested("tabStops", root.stops.filter(function(stop, index) { return index !== existing.currentIndex; }))
    }
    ActionButton
    {
        objectName: "wordClearTabs"
        theme: root.theme; text: "清除全部"; compact: true
        onClicked: root.formatRequested("tabStops", [])
    }
}
