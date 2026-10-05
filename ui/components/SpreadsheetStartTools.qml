pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ColumnLayout
{
    id: root
    property var theme
    property string section
    property var rangeInfo
    property var layoutInfo
    property string borderInk: theme ? theme.sheetsStrongLine : ""
    signal requested(string action, var args)
    Text
    {
        visible: root.section === "clipboard" && !!root.layoutInfo.pendingCut
        text: "待移动 " + (root.layoutInfo.pendingCut || "") + " · 在本工作簿粘贴后移走源内容"
        color: root.theme.textSecondary
    }
    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        visible: root.section === "switch"
        TextField { id: worksheetName; placeholderText: "新工作表名称"; maximumLength: 31; selectByMouse: true }
        ActionButton { theme: root.theme; text: "新建工作表"; compact: true; onClicked: root.requested("addSheet", {name: worksheetName.text}) }
        ActionButton { theme: root.theme; text: "重命名"; compact: true; enabled: worksheetName.text.trim().length > 0; onClicked: root.requested("renameSheet", {name: worksheetName.text}) }
        ActionButton { theme: root.theme; text: "复制工作表"; compact: true; onClicked: root.requested("copySheet", {name: worksheetName.text}) }
        ComboBox { id: sheetPosition; model: root.layoutInfo.sheets || []; textRole: "name" }
        ActionButton { theme: root.theme; text: "移到此位置"; compact: true; onClicked: root.requested("moveSheet", {index: sheetPosition.currentIndex}) }
        ActionButton { theme: root.theme; text: "隐藏当前表"; compact: true; onClicked: root.requested("hideSheet", {}) }
        ComboBox { id: hiddenWorksheet; model: root.layoutInfo.hiddenSheets || []; textRole: "name"; visible: count > 0 }
        ActionButton { theme: root.theme; text: "显示所选隐藏表"; compact: true; visible: hiddenWorksheet.count > 0; onClicked: root.requested("showSheet", {index: hiddenWorksheet.model[hiddenWorksheet.currentIndex].index}) }
        ActionButton { theme: root.theme; text: "删除当前工作表"; compact: true; onClicked: root.requested("deleteSheet", {}) }
        Text { text: "复制包含内容与样式 · 删除可撤销 · 至少保留一张可见表"; color: root.theme.textSecondary }
    }
    Flow
    {
        Layout.fillWidth: true
        spacing: 6
        visible: root.section === "structure"
        Repeater
        {
            model: [{text: "选区上方插入行", key: "insertRows", after: false}, {text: "选区下方插入行", key: "insertRows", after: true}, {text: "选区左侧插入列", key: "insertColumns", after: false}, {text: "选区右侧插入列", key: "insertColumns", after: true}, {text: "删除所选整行", key: "deleteRows"}, {text: "删除所选整列", key: "deleteColumns"}]
            delegate: ActionButton
            {
                required property var modelData
                theme: root.theme
                text: modelData.text
                compact: true
                onClicked: root.requested(modelData.key, modelData.after === undefined ? ({}) : {after: modelData.after})
            }
        }
        Text { text: "数量随选区行列数 · 整行/整列内容会一起移动或删除 · 可撤销"; color: root.theme.textSecondary }
    }
    ColorDialog
    {
        id: borderColorDialog
        title: "边框颜色"
        selectedColor: root.borderInk
        onAccepted: root.borderInk = String(selectedColor)
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "border"
        ComboBox { id: borderKind; model: ["所有边框", "外侧边框", "内部边框", "内部横线", "内部竖线", "上框线", "下框线", "左框线", "右框线", "无边框"] }
        ComboBox { id: borderStyle; model: ["细实线", "中实线", "粗实线", "双线", "虚线", "点线"] }
        ActionButton { theme: root.theme; text: "边框颜色…"; compact: true; onClicked: borderColorDialog.open() }
        ActionButton { theme: root.theme; text: "设置边框"; compact: true; onClicked: root.requested("border", {kind: ["all", "outer", "inner", "horizontal", "vertical", "top", "bottom", "left", "right", "none"][borderKind.currentIndex], style: ["thin", "medium", "thick", "double", "dashed", "dotted"][borderStyle.currentIndex], color: root.borderInk}) }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6
        visible: ["clipboard", "fill", "merge", "size", "freeze", "value"].indexOf(root.section) >= 0
        Repeater
        {
            model: root.section === "clipboard" ? [{text: "剪切", key: "cut"}, {text: "仅粘贴值", key: "pasteValues"}, {text: "仅粘贴格式", key: "pasteCellFormat"}, {text: "拾取格式", key: "copyFormat"}, {text: "应用格式刷", key: "pasteFormat"}, {text: "清除格式", key: "clearFormat"}, {text: "清除全部", key: "clearAll"}]
                : root.section === "fill" ? [{text: "向下填充", key: "fillDown"}, {text: "向右填充", key: "fillRight"}, {text: "向下递增 1", key: "sequence"}]
                : root.section === "merge" ? [{text: "合并选区", key: "merge"}, {text: "取消合并", key: "unmerge"}]
                : root.section === "size" ? [{text: "按选区内容适配列宽", key: "autoWidth"}, {text: "隐藏所选行", key: "hideRows"}, {text: "隐藏所选列", key: "hideColumns"}, {text: "显示全部行列", key: "showAll"}]
                : root.section === "freeze" ? [{text: "冻结首行", key: "firstRow"}, {text: "冻结首列", key: "firstColumn"}, {text: "冻结当前格上方及左侧", key: "freezeHere"}, {text: "取消冻结", key: "unfreeze"}]
                : [{text: "选区转为数字", key: "toNumber"}, {text: "选区转为文本", key: "toText"}]
            delegate: ActionButton
            {
                required property var modelData
                theme: root.theme; text: modelData.text; iconName: ""; compact: true
                enabled: modelData.key !== "pasteFormat" || Boolean(root.layoutInfo.formatReady)
                onClicked:
                {
                    const key = modelData.key;
                    if (key === "firstRow" || key === "firstColumn") root.requested("freeze", {rows: key === "firstRow" ? 1 : 0, columns: key === "firstColumn" ? 1 : 0});
                    else if (key === "freezeHere") root.requested("freeze", {rows: root.rangeInfo.firstRow, columns: root.rangeInfo.firstColumn});
                    else if (key === "toNumber" || key === "toText") root.requested("convert", {kind: key === "toNumber" ? "number" : "text"});
                    else root.requested(key, {});
                }
            }
        }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "aggregate"
        Repeater
        {
            model: [{text: "求和", key: "sum"}, {text: "平均值", key: "average"}, {text: "数字计数", key: "count"}, {text: "最大值", key: "max"}, {text: "最小值", key: "min"}]
            delegate: ActionButton { required property var modelData; theme: root.theme; text: modelData.text; compact: true; onClicked: root.requested("aggregate", {kind: modelData.key}) }
        }
        Text { text: "逐列汇总到选区下方空白行 · 源数据修改后自动重算"; color: root.theme.textSecondary }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "filter"
        Text { text: "首行为表头 · 同一选区可逐列叠加条件 · 列号"; color: root.theme.textSecondary }
        SpinBox { id: filterColumn; from: (root.rangeInfo.firstColumn || 0) + 1; to: (root.rangeInfo.lastColumn || 0) + 1; value: from; editable: true }
        ComboBox { id: filterOperator; model: ["等于", "不等于", "包含", "开头是", "结尾是", "大于", "大于等于", "小于", "小于等于", "介于", "匹配多个值"] }
        TextField { id: filterValue; placeholderText: filterOperator.currentIndex === 10 ? "多个值用分号隔开" : "筛选值（等于时可留空）"; maximumLength: 1024 }
        TextField { id: filterValue2; visible: filterOperator.currentIndex === 9; placeholderText: "上限（含）"; maximumLength: 32 }
        ActionButton
        {
            theme: root.theme; text: "应用本列筛选"; compact: true
            onClicked:
            {
                const args = {column: filterColumn.value - 1, operator: ["equal", "notEqual", "contains", "beginsWith", "endsWith", "greaterThan", "greaterThanOrEqual", "lessThan", "lessThanOrEqual", "between", "equal"][filterOperator.currentIndex]};
                if (filterOperator.currentIndex === 10) args.values = filterValue.text.split(";");
                else args.value = filterValue.text;
                if (filterOperator.currentIndex === 9) args.value2 = filterValue2.text;
                root.requested("filter", args);
            }
        }
        ActionButton { theme: root.theme; text: "清除本列条件"; compact: true; onClicked: root.requested("clearFilterColumn", {column: filterColumn.value - 1}) }
        ActionButton { theme: root.theme; text: "清除全部筛选"; compact: true; onClicked: root.requested("clearFilter", {}) }
        Text { text: root.layoutInfo.filtered ? "范围 " + root.layoutInfo.filterRange + " · " + root.layoutInfo.filters.length + " 列条件" : "尚未筛选"; color: root.theme.textSecondary }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "condition"
        ComboBox { id: compare; model: ["大于", "小于", "等于"] }
        TextField { id: threshold; text: "0"; placeholderText: "数值"; maximumLength: 32; validator: DoubleValidator { locale: "C" } }
        ComboBox { id: conditionColor; model: ["浅蓝", "浅金", "浅珊瑚"] }
        ActionButton { theme: root.theme; text: "添加选区规则"; compact: true; enabled: threshold.acceptableInput; onClicked: root.requested("condition", {operator: ["greaterThan", "lessThan", "equal"][compare.currentIndex], value: Number(threshold.text), fill: [root.theme.spectrumBlue, root.theme.spectrumGold, root.theme.spectrumCoral][conditionColor.currentIndex]}) }
        ActionButton { theme: root.theme; text: "清除本表条件规则"; compact: true; onClicked: root.requested("clearConditions", {}) }
    }
    Flow
    {
        Layout.fillWidth: true; spacing: 6; visible: root.section === "find"
        TextField { id: query; placeholderText: "选区中要替换的文本"; maximumLength: 1024 }
        TextField { id: replacement; placeholderText: "替换为"; maximumLength: 1024 }
        ActionButton { theme: root.theme; text: "替换选区文本"; compact: true; enabled: query.text.length > 0; onClicked: root.requested("replace", {query: query.text, replacement: replacement.text}) }
    }
    Text
    {
        visible: root.section === "merge" || root.section === "freeze"
        text: root.section === "merge" ? "合并时其他格必须为空；内容保留在左上格。" : "最多冻结前 16 行、8 列；冻结线不能穿过合并格。"
        color: root.theme.textSecondary
    }
}
