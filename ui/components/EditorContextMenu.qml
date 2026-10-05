import QtQuick
import QtQuick.Controls

Menu
{
    id: root
    property bool editable: true
    property bool hasSelection: false
    property bool canUndo: false
    property bool canRedo: false
    signal copyRequested()
    signal cutRequested()
    signal pasteRequested()
    signal selectAllRequested()
    signal undoRequested()
    signal redoRequested()
    MenuItem { text: "复制"; enabled: root.hasSelection; onTriggered: root.copyRequested() }
    MenuItem { text: "剪切"; enabled: root.editable && root.hasSelection; onTriggered: root.cutRequested() }
    MenuItem { text: "粘贴"; enabled: root.editable; onTriggered: root.pasteRequested() }
    MenuItem { text: "全选"; onTriggered: root.selectAllRequested() }
    MenuSeparator { }
    MenuItem { text: "撤销"; enabled: root.editable && root.canUndo; onTriggered: root.undoRequested() }
    MenuItem { text: "重做"; enabled: root.editable && root.canRedo; onTriggered: root.redoRequested() }
}
