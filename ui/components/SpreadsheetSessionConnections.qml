import QtQuick

Item
{
    id: session
    visible: false

    required property var host
    required property var spreadsheetBridge
    required property var intro
    required property var savePrompt
    required property var unsavedPrompt

    Connections
    {
        target: session.spreadsheetBridge

        function onDocumentActivated() { session.intro.play("sheets"); }

        function onSaveDialogRequested()
        {
            session.savePrompt.selectedFile = session.spreadsheetBridge.saveUrl;
            session.savePrompt.open();
        }

        function onConfirmUnsavedRequested()
        {
            session.host.unsavedOwner = "sheets";
            session.unsavedPrompt.open();
        }

        function onWindowCloseAllowed()
        {
            session.host.allowWindowClose = true;
            session.host.close();
        }
    }
}
