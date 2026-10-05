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
    signal formatRequested(string action, var value)
    enabled: editable
    spacing: 6
    Repeater
    {
        model: [{action: "underlineStyle", label: "下划线"}, {action: "strikeStyle", label: "删除线"}]
        RowLayout
        {
            id: lineTool
            required property var modelData
            Text { text: lineTool.modelData.label; color: root.theme.textSecondary }
            ComboBox
            {
                objectName: "word" + lineTool.modelData.action
                model: ["无", "单线", "双线"]
                currentIndex: Math.max(0, ["none", "single", "double"].indexOf(
                    root.selection[lineTool.modelData.action] || "none"))
                onActivated: function(index)
                {
                    root.formatRequested(lineTool.modelData.action, ["none", "single", "double"][index]);
                }
            }
        }
    }
}
