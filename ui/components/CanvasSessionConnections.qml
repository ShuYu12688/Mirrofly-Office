import QtQuick

Item
{
    id: session
    visible: false

    required property var host
    required property var pdfBridge
    required property var mindmapBridge
    required property var application
    required property var intro
    required property var savePrompt
    required property var unsavedPrompt

    function openSave(owner, bridge)
    {
        session.host.canvasSaveOwner = owner;
        session.savePrompt.selectedFile = bridge.saveUrl;
        session.savePrompt.open();
    }

    function confirmUnsaved(owner)
    {
        session.host.unsavedOwner = owner;
        session.unsavedPrompt.open();
    }

    function closeWindow()
    {
        session.host.allowWindowClose = true;
        session.host.close();
    }

    Connections
    {
        target: session.pdfBridge
        function onDocumentActivated() { session.intro.play("pdf"); }
        function onOpenDialogRequested() { session.application.chooseFile(); }
        function onSaveDialogRequested() { session.openSave("pdf", session.pdfBridge); }
        function onConfirmUnsavedRequested() { session.confirmUnsaved("pdf"); }
        function onWindowCloseAllowed() { session.closeWindow(); }
    }

    Connections
    {
        target: session.mindmapBridge
        function onDocumentActivated() { session.intro.play("mindmap"); }
        function onOpenDialogRequested() { session.application.chooseFile(); }
        function onSaveDialogRequested() { session.openSave("mindmap", session.mindmapBridge); }
        function onConfirmUnsavedRequested() { session.confirmUnsaved("mindmap"); }
        function onWindowCloseAllowed() { session.closeWindow(); }
    }
}
