pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var selection: ({})
    property string section: "cellFill"
    property string borderEdge: "all"
    property string chosenBorderColor: ""
    property var pendingStyle: ({})
    readonly property var selectedBorder: borderValue(borderEdge)
    signal editRequested(string action, var options)

    function borderValue(edge)
    {
        return edge === "all"
            ? ({color: selection.tableBorderColor || "", width: selection.tableBorderWidth || 0,
                localOverride: selection.tableLocalBorderOverride === true})
            : ((selection.tableBorderEdges || {})[edge] || {});
    }

    onSelectionChanged:
    {
        chosenBorderColor = borderValue(borderEdge).color || "";
        pendingStyle = Object.assign({}, selection.tableStyleOptions || {});
    }
    Component.onCompleted: pendingStyle = Object.assign({}, selection.tableStyleOptions || {})
    onBorderEdgeChanged: chosenBorderColor = borderValue(borderEdge).color || ""

    spacing: 8

    Label
    {
        visible: root.section === "tableStyle"
        text: root.selection.tableStyleAvailable === true
            ? "整表样式 · 选择区域后统一应用" : "这张表没有可编辑的关联样式"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
    }

    GridLayout
    {
        visible: root.section === "tableStyle" && root.selection.tableStyleAvailable === true
        columns: 3
        columnSpacing: 14
        rowSpacing: 4

        Repeater
        {
            model: [{key: "firstRow", label: "标题行"}, {key: "lastRow", label: "末行"},
                {key: "firstColumn", label: "首列"}, {key: "lastColumn", label: "末列"},
                {key: "bandRows", label: "隔行着色"}, {key: "bandColumns", label: "隔列着色"}]
            CheckBox
            {
                required property var modelData
                objectName: "presentationTableStyle_" + modelData.key
                text: modelData.label
                checked: root.pendingStyle[modelData.key] === true
                onClicked:
                {
                    const next = Object.assign({}, root.pendingStyle);
                    next[modelData.key] = checked;
                    root.pendingStyle = next;
                }
            }
        }
    }

    ActionButton
    {
        objectName: "presentationApplyTableStyle"
        visible: root.section === "tableStyle" && root.selection.tableStyleAvailable === true
        enabled: (root.selection.actions || []).includes("formatTableStyle")
        theme: root.theme
        text: "应用整表样式"
        iconName: ""
        compact: true
        onClicked: root.editRequested("formatTableStyle", {style: root.pendingStyle})
    }

    Label
    {
        visible: root.section === "cellFill"
        text: root.selection.tableLocalFillOverride === true
            ? "单元格底色 · 已覆盖表格样式" : "单元格底色 · 继承表格样式或默认底色"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
    }

    ActionButton
    {
        objectName: "presentationResetTableCellFill"
        visible: root.section === "cellFill" && root.selection.tableLocalFillOverride === true
        enabled: (root.selection.actions || []).includes("resetTableCellFill")
        theme: root.theme
        text: "恢复继承底色"
        iconName: ""
        compact: true
        onClicked: root.editRequested("resetTableCellFill", {})
    }

    PresentationPalette
    {
        visible: root.section === "cellFill"
        Layout.fillWidth: true
        theme: root.theme
        onColorSelected: function(value)
        {
            root.editRequested("formatTableCell", {fillColor: value,
                fillOpacity: root.selection.fillOpacity === undefined ? 1 : root.selection.fillOpacity});
        }
    }

    RowLayout
    {
        visible: root.section === "cellFill"
        Label { text: "透明度"; color: root.theme.textSecondary }
        SpinBox
        {
            from: 0
            to: 100
            value: Math.round((1 - (root.selection.fillOpacity === undefined ? 1 : root.selection.fillOpacity)) * 100)
            editable: true
            Accessible.name: "单元格底色透明度"
            onValueModified:
            {
                const color = root.selection.fillColor || root.theme.slidesPaper;
                root.editRequested("formatTableCell", {fillColor: color, fillOpacity: 1 - value / 100});
            }
        }
        Label { text: "%"; color: root.theme.textSecondary }
        Item { Layout.fillWidth: true }
    }

    Label
    {
        visible: root.section === "cellBorder"
        text: "单元格边框 · " + (root.selectedBorder.localOverride === true
            ? "已覆盖表格样式" : "继承表格样式或默认边框")
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
    }

    RowLayout
    {
        visible: root.section === "cellBorder"
        spacing: 6

        Repeater
        {
            model: [{edge: "all", label: "四边"}, {edge: "left", label: "左"},
                {edge: "top", label: "上"}, {edge: "right", label: "右"},
                {edge: "bottom", label: "下"}]
            ActionButton
            {
                required property var modelData
                theme: root.theme
                text: modelData.label
                iconName: ""
                compact: true
                primary: root.borderEdge === modelData.edge
                onClicked: root.borderEdge = modelData.edge
            }
        }
        Item { Layout.fillWidth: true }
    }

    ActionButton
    {
        objectName: "presentationResetTableBorder"
        visible: root.section === "cellBorder" && root.selectedBorder.localOverride === true
        enabled: (root.selection.actions || []).includes("resetTableBorder")
        theme: root.theme
        text: root.borderEdge === "all" ? "恢复四边继承" : "恢复当前边继承"
        iconName: ""
        compact: true
        onClicked: root.editRequested("resetTableBorder", {edge: root.borderEdge})
    }

    PresentationPalette
    {
        Layout.fillWidth: true
        visible: root.section === "cellBorder"
        theme: root.theme
        onColorSelected: function(value)
        {
            root.chosenBorderColor = value;
            root.editRequested("formatTableBorder", {color: value, width: borderWidth.value,
                edge: root.borderEdge});
        }
    }

    RowLayout
    {
        visible: root.section === "cellBorder"
        Label { text: "线宽"; color: root.theme.textSecondary }
        SpinBox
        {
            id: borderWidth
            objectName: "presentationTableBorderWidth"
            from: 1
            to: 8
            value: root.selectedBorder.width > 0 ? Math.round(root.selectedBorder.width) : 1
            editable: true
            enabled: root.chosenBorderColor.length > 0
            Accessible.name: "表格单元格边框线宽"
            onValueModified:
            {
                root.editRequested("formatTableBorder", {color: root.chosenBorderColor, width: value,
                    edge: root.borderEdge});
            }
        }
        Label { text: "pt"; color: root.theme.textSecondary }
        Item { Layout.fillWidth: true }
    }
}
