import QtQuick
import QtQuick.Controls

Item
{
    id: root

    property var theme
    property var editorData: ({})
    property bool editing: false
    property bool synchronizing: false
    property bool submitting: false
    property string editingId: ""
    property string originalText: ""
    property bool dirty: false
    signal textEdited(string text)
    signal undoRequested()
    signal redoRequested()
    signal inputCommitRequested()

    visible: editing && Boolean(editorData.valid)
    width: editorData.width || 0
    height: editorData.height || 0
    transform: Matrix4x4
    {
        matrix: Qt.matrix4x4(root.editorData.a || 0, root.editorData.c || 0, 0, root.editorData.tx || 0,
            root.editorData.b || 0, root.editorData.d || 0, 0, root.editorData.ty || 0,
            0, 0, 1, 0, 0, 0, 0, 1)
    }

    function restoreText()
    {
        const source = String(editorData.text || "");
        if (input.text !== source)
        {
            synchronizing = true;
            const cursor = input.cursorPosition;
            input.text = source;
            input.cursorPosition = Math.min(cursor, source.length);
            synchronizing = false;
        }
        originalText = source;
        dirty = false;
    }

    function begin()
    {
        if (!enabled || !editorData.valid)
        {
            return;
        }
        restoreText();
        editingId = editorData.id;
        editing = true;
        input.forceActiveFocus();
        input.cursorPosition = input.length;
    }

    function beginAt(surface, x, y)
    {
        begin();
        if (editing)
        {
            const point = input.mapFromItem(surface, x, y);
            input.cursorPosition = input.positionAt(point.x, point.y);
        }
    }

    function finish()
    {
        if (editing)
        {
            inputCommitRequested();
            if (dirty)
            {
                submitting = true;
                textEdited(input.text);
                submitting = false;
            }
            editing = false;
            input.focus = false;
        }
    }

    onEditorDataChanged:
    {
        if (editing && (!editorData.valid || editingId !== editorData.id))
        {
            editing = false;
        }
        if (!editing && !submitting)
        {
            restoreText();
        }
    }
    onEnabledChanged: if (!enabled) finish()

    TextArea
    {
        id: input

        objectName: "presentationTextInput"
        anchors.fill: parent
        clip: true
        textFormat: TextEdit.PlainText
        selectByMouse: true
        persistentSelection: true
        wrapMode: root.editorData.wrap === false ? TextEdit.NoWrap : TextEdit.Wrap
        color: root.editorData.color || root.theme.textPrimary
        selectionColor: root.theme.accent
        selectedTextColor: root.theme.onAccent
        font: root.editorData.font || Qt.font({family: root.theme.fontFamily, pixelSize: 18})
        leftPadding: root.editorData.left || 0
        rightPadding: root.editorData.right || 0
        topPadding: root.editorData.top || 0
        bottomPadding: root.editorData.bottom || 0
        horizontalAlignment: root.editorData.alignment === "center" ? TextEdit.AlignHCenter
            : (root.editorData.alignment === "right" ? TextEdit.AlignRight
                : (root.editorData.alignment === "justify" ? TextEdit.AlignJustify : TextEdit.AlignLeft))
        verticalAlignment: root.editorData.vertical === "center" ? TextEdit.AlignVCenter
            : (root.editorData.vertical === "bottom" ? TextEdit.AlignBottom : TextEdit.AlignTop)
        Accessible.name: "幻灯片文字编辑"

        background: Rectangle
        {
            color: root.theme.transparentColor
            border.color: root.theme.accent
            border.width: 0.8
        }

        onTextChanged:
        {
            if (root.editing && !root.synchronizing)
            {
                root.dirty = text !== root.originalText;
            }
        }
        onActiveFocusChanged: if (!activeFocus && root.editing) root.finish()
        Keys.onPressed: function(event)
        {
            if (event.key === Qt.Key_Escape)
            {
                root.finish();
                event.accepted = true;
            }
            else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z)
            {
                input.undo();
                event.accepted = true;
            }
            else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Y)
            {
                input.redo();
                event.accepted = true;
            }
        }
    }
}
