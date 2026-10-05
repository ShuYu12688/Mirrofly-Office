import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    signal editRequested(string action, var options)

    spacing: 8

    Text
    {
        Layout.fillWidth: true
        text: "在当前页面插入可编辑表格。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
    }

    RowLayout
    {
        spacing: 8
        Label { text: "行"; color: root.theme.textSecondary }
        SpinBox
        {
            id: rows
            from: 1
            to: 30
            value: 3
            editable: true
            Accessible.name: "表格行数"
        }
        Label { text: "列"; color: root.theme.textSecondary }
        SpinBox
        {
            id: columns
            from: 1
            to: 20
            value: 4
            editable: true
            Accessible.name: "表格列数"
        }
        ActionButton
        {
            theme: root.theme
            text: "插入表格"
            iconName: "plus"
            compact: true
            onClicked: root.editRequested("insertTable", {rows: rows.value, columns: columns.value})
        }
        Item { Layout.fillWidth: true }
    }
}
