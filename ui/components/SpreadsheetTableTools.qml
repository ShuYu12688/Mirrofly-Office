pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root
    property var theme
    property var layoutInfo
    readonly property var current: layoutInfo.currentTable || ({})
    signal requested(string action, var args)

    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        TextField { id: tableName; text: root.current.name || ""; placeholderText: "表格名（留空自动命名）"; maximumLength: 64 }
        ComboBox { id: palette; model: ["浅绿", "蓝色", "紫色"] }
        CheckBox { id: rowStripes; text: "隔行底色"; checked: root.current.rowStripes === undefined ? true : root.current.rowStripes }
        CheckBox { id: columnStripes; text: "隔列底色"; checked: Boolean(root.current.columnStripes) }
        CheckBox { id: firstColumn; text: "强调首列"; checked: Boolean(root.current.firstColumn) }
        CheckBox { id: lastColumn; text: "强调末列"; checked: Boolean(root.current.lastColumn) }
    }
    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        enabled: Boolean(root.layoutInfo.tablesSupported)
        ActionButton
        {
            theme: root.theme
            text: root.current.name ? "更新表格样式" : "将选区设为表格"
            compact: true
            onClicked:
            {
                const headers = [root.theme.sheetsHeaderFill, root.theme.sheetsTableBlueFill, root.theme.sheetsTablePurpleFill];
                const bands = [root.theme.sheetsBandFill, root.theme.sheetsTableBlueBand, root.theme.sheetsTablePurpleBand];
                root.requested("tableStyle", {name: tableName.text, rowStripes: rowStripes.checked, columnStripes: columnStripes.checked, firstColumn: firstColumn.checked, lastColumn: lastColumn.checked,
                    palette: {header: headers[palette.currentIndex], alternate: bands[palette.currentIndex], body: root.theme.sheetsTableBodyFill, headerText: root.theme.sheetsHeaderText, bodyText: root.theme.sheetsTableBodyText}});
            }
        }
        ActionButton { theme: root.theme; text: "范围改为当前选区"; compact: true; enabled: tableName.text.length > 0; onClicked: root.requested("tableRange", {name: tableName.text}) }
        ActionButton { theme: root.theme; text: "移除表格及表格样式"; compact: true; enabled: tableName.text.length > 0; onClicked: root.requested("removeTable", {name: tableName.text}) }
    }
    Text
    {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: root.theme.textSecondary
        text: root.layoutInfo.tablesSupported === false ? "导入表格的复杂结构或样式暂未支持，已保留原内容。"
            : root.current.name ? root.current.name + " · " + root.current.range + " · 手工单元格格式优先；改变范围会清除该表格的筛选条件。"
            : "首行需为互不重复的文字表头，至少两行；名称使用字母、数字和下划线，不能以数字开头。"
    }
}
