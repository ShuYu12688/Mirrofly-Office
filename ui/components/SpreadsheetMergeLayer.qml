pragma ComponentBehavior: Bound
import QtQuick
import Mirrorfly.Native

Item
{
    id: root
    property var theme
    property var merges: []
    property real zoom: 1
    property real scrollX: 0
    property real scrollY: 0
    property var selected: ({row: -1, column: -1})
    signal selectedCell(int row, int column, bool extend)
    signal editRequested(int row, int column)
    clip: true
    Repeater
    {
        model: root.merges
        delegate: Rectangle
        {
            id: merged
            required property var modelData
            x: modelData.x * root.zoom - root.scrollX; y: modelData.y * root.zoom - root.scrollY
            width: modelData.width * root.zoom; height: modelData.height * root.zoom
            visible: width > 0 && height > 0 && x < root.width && y < root.height && x + width > 0 && y + height > 0
            readonly property bool active: root.selected.row >= modelData.row && root.selected.row <= modelData.lastRow && root.selected.column >= modelData.column && root.selected.column <= modelData.lastColumn
            color: active ? root.theme.sheetsSelectionFill : modelData.format.fill !== "none" ? modelData.format.fill : root.theme.surfaceColor
            border.width: active ? 2 : 1
            border.color: active ? root.theme.accent : root.theme.sheetsGridLine
            SpreadsheetBorderRenderer { anchors.fill: parent; format: merged.modelData.format; zoom: root.zoom; visible: !merged.active && merged.modelData.format.border === "1" }
            Text
            {
                id: mergedText
                anchors.fill: parent; anchors.margins: 7 * root.zoom
                text: merged.modelData.text; textFormat: Text.PlainText; elide: Text.ElideRight; clip: true
                color: merged.modelData.format.text || root.theme.textPrimary
                font.family: merged.modelData.format.font || root.theme.fontFamily
                font.pointSize: Number(merged.modelData.format.size || 11) * root.zoom
                font.bold: merged.modelData.format.bold === "1"; font.italic: merged.modelData.format.italic === "1"
                font.underline: merged.modelData.format.underline === "1"; font.strikeout: merged.modelData.format.strike === "1"
                horizontalAlignment: merged.modelData.format.align === "center" ? Text.AlignHCenter : merged.modelData.format.align === "right" ? Text.AlignRight : Text.AlignLeft
                verticalAlignment: merged.modelData.format.valign === "top" ? Text.AlignTop : merged.modelData.format.valign === "center" ? Text.AlignVCenter : Text.AlignBottom
                wrapMode: merged.modelData.format.wrap === "1" ? Text.Wrap : Text.NoWrap
                visible: Number(merged.modelData.format.textRotation || 0) === 0 && merged.modelData.format.shrinkToFit !== "1" && ["justify", "distributed"].indexOf(merged.modelData.format.align) < 0
            }
            SpreadsheetTextRenderer
            {
                anchors.fill: parent; anchors.margins: 7 * root.zoom
                anchors.leftMargin: 7 * root.zoom + Number(merged.modelData.format.indent || 0) * 10 * root.zoom
                text: merged.modelData.text; font: mergedText.font; color: mergedText.color
                format: merged.modelData.format; visible: !mergedText.visible
            }
            MouseArea
            {
                anchors.fill: parent
                onClicked: function(mouse) { root.selectedCell(merged.modelData.row, merged.modelData.column, Boolean(mouse.modifiers & Qt.ShiftModifier)); }
                onDoubleClicked: root.editRequested(merged.modelData.row, merged.modelData.column)
            }
        }
    }
}
