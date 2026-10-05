pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var searchFunction
    property bool syncing: false
    property var searchResult: ({})
    property int offset: 0
    property string status: "输入要查找的文字，然后点击“查找”。"
    signal editRequested(string action, var options)
    signal navigateRequested(int slideIndex, int shapeIndex)

    spacing: 8

    function refresh(nextOffset = 0)
    {
        if (!searchFunction || query.text.length === 0)
        {
            searchResult = ({});
            status = "输入要查找的文字，然后点击“查找”。";
            return;
        }
        const result = searchFunction(query.text, exact.checked, nextOffset);
        if (!result || !result.ok)
        {
            searchResult = ({});
            status = "查找失败，请检查文字或结果范围。";
            return;
        }
        searchResult = result;
        offset = nextOffset;
        status = "找到 " + result.total + " 处；第 " +
            (result.total === 0 ? 0 : nextOffset + 1) + "—" +
            Math.min(result.total, nextOffset + (result.nodes || []).length) + " 处。";
    }

    function optionsFor(scope, node)
    {
        const result = {query: query.text, replacement: replacement.text,
            caseSensitive: exact.checked, scope: scope,
            expectedGeneration: searchResult.generation};
        if (scope === "match")
        {
            result.slideIndex = node.slideIndex;
            result.shapeIndex = node.shapeIndex;
            result.shapeId = node.shapeId;
            result.paragraphIndex = node.paragraphIndex;
            result.startByte = node.startByte;
        }
        return result;
    }

    onSyncingChanged: if (!syncing && searchResult.ok) refresh(0)

    Text
    {
        Layout.fillWidth: true
        text: "按整段文字查找，支持跨文字格式片段。字段等只读内容会显示，但不能替换。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    RowLayout
    {
        Layout.fillWidth: true
        spacing: 8

        TextField
        {
            id: query
            objectName: "presentationFindQuery"
            Layout.preferredWidth: 220
            maximumLength: 256
            placeholderText: "查找文字"
            Accessible.name: "演示文稿查找文字"
            onTextChanged: root.searchResult = ({})
            onAccepted: root.refresh(0)
        }
        CheckBox
        {
            id: exact
            objectName: "presentationFindCaseSensitive"
            text: "区分大小写"
            onCheckedChanged: root.searchResult = ({})
        }
        ActionButton
        {
            theme: root.theme
            text: "查找"
            compact: true
            enabled: query.text.length > 0 && !root.syncing
            onClicked: root.refresh(0)
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout
    {
        Layout.fillWidth: true
        spacing: 8

        TextField
        {
            id: replacement
            objectName: "presentationFindReplacement"
            Layout.preferredWidth: 220
            maximumLength: 4096
            placeholderText: "替换为（留空表示删除）"
            Accessible.name: "演示文稿替换文字"
        }
        ActionButton
        {
            theme: root.theme
            text: "全部替换"
            compact: true
            enabled: root.searchResult.ok === true && root.searchResult.total > 0 && !root.syncing
            onClicked: root.editRequested("replaceTextMatches", root.optionsFor("all", null))
        }
        Item { Layout.fillWidth: true }
    }

    Text
    {
        objectName: "presentationFindStatus"
        Layout.fillWidth: true
        text: root.syncing ? "正在同步替换结果…" : root.status
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    ScrollView
    {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(200, Math.max(0, (root.searchResult.nodes || []).length * 35))
        visible: (root.searchResult.nodes || []).length > 0
        clip: true

        ColumnLayout
        {
            width: parent.width
            spacing: 4
            Repeater
            {
                model: root.searchResult.nodes || []
                RowLayout
                {
                    id: matchRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6

                    ActionButton
                    {
                        theme: root.theme
                        text: "第 " + (matchRow.modelData.slideIndex + 1) + " 页 · " +
                            matchRow.modelData.excerpt
                        compact: true
                        onClicked: root.navigateRequested(matchRow.modelData.slideIndex,
                            matchRow.modelData.shapeIndex)
                    }
                    ActionButton
                    {
                        theme: root.theme
                        text: "替换此处"
                        compact: true
                        enabled: matchRow.modelData.replaceable && !root.syncing
                        onClicked: root.editRequested("replaceTextMatches",
                            root.optionsFor("match", matchRow.modelData))
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    RowLayout
    {
        visible: root.searchResult.ok === true && root.searchResult.total > 64
        ActionButton
        {
            theme: root.theme
            text: "上一页结果"
            compact: true
            enabled: root.offset > 0
            onClicked: root.refresh(Math.max(0, root.offset - 64))
        }
        ActionButton
        {
            theme: root.theme
            text: "下一页结果"
            compact: true
            enabled: root.searchResult.nextOffset >= 0
            onClicked: root.refresh(root.searchResult.nextOffset)
        }
        Item { Layout.fillWidth: true }
    }
}
