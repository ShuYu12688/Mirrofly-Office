pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

GridLayout
{
    id: root
    required property var theme
    signal createRequested(string kind)
    readonly property bool wide: width >= theme.workspaceWideBreakpoint
    readonly property bool narrow: width < theme.workspaceNarrowBreakpoint
    columns: wide ? 4 : (narrow ? 1 : 2)
    columnSpacing: theme.workspaceGap
    rowSpacing: theme.workspaceGap
    uniformCellWidths: true
    Repeater
    {
        model:
        [
            { "kind": "word", "title": "Word", "format": "DOCX · 文档", "detail": "写作、排版与文档模板", "row": 0, "column": 0, "rows": 2, "columns": 1 },
            { "kind": "slides", "title": "演示文稿", "format": "PPTX · 演示", "detail": "表达观点，制作与放映演示", "row": 0, "column": 1, "rows": 1, "columns": 2 },
            { "kind": "sheets", "title": "电子表格", "format": "XLSX · 数据", "detail": "整理数据，计算与发现规律", "row": 0, "column": 3, "rows": 1, "columns": 1 },
            { "kind": "markdown", "title": "Markdown", "format": "MD · 笔记", "detail": "标题、代码与轻量写作", "row": 1, "column": 1, "rows": 1, "columns": 1 },
            { "kind": "pdf", "title": "PDF", "format": "PDF · 阅读", "detail": "打开文档，标记与整理页面", "row": 1, "column": 2, "rows": 1, "columns": 1 },
            { "kind": "mindmap", "title": "思维导图", "format": "MFG · 思路", "detail": "连接想法，梳理清晰的结构", "row": 1, "column": 3, "rows": 1, "columns": 1 },
            { "kind": "writer", "title": "纯文本", "format": "TXT · 随手记录", "detail": "简洁的文字空间，不受格式打扰", "row": 2, "column": 0, "rows": 1, "columns": 4 }
        ]
        CreateCard
        {
            required property var modelData
            required property int index
            objectName: "createTile_" + modelData.kind
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.row: root.wide ? modelData.row : (root.narrow ? index : Math.floor(index / 2))
            Layout.column: root.wide ? modelData.column : (root.narrow ? 0 : index % 2)
            Layout.rowSpan: root.wide ? modelData.rows : 1
            Layout.columnSpan: root.wide ? modelData.columns : (index === 6 && !root.narrow ? 2 : 1)
            Layout.preferredHeight: root.wide
                ? (index === 0 ? root.theme.workspaceTileTallHeight : (index === 6 ? 100 : root.theme.workspaceTileHeight))
                : root.theme.workspaceTileHeight
            theme: root.theme
            kind: modelData.kind
            text: modelData.title
            subtitle: modelData.detail
            formatLabel: modelData.format
            horizontal: index === 6 && !root.narrow
            onClicked: root.createRequested(modelData.kind)
        }
    }
}
