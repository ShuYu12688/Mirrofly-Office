import QtQuick

Item
{
    id: session
    visible: false

    required property var host
    required property var textBridge
    required property var tools
    required property var page
    required property var intro
    required property var savePrompt
    required property var unsavedPrompt

    Connections
    {
        target: session.tools

        function onDocumentEdited(document)
        {
            const source = session.tools.sourceText(document);
            if (session.tools.canSave)
            {
                session.textBridge.setEditingError("");
                session.textBridge.updateText(source);
            }
            else
            {
                session.textBridge.setEditingError(session.tools.inspectDocument(document, 0).error);
            }
            session.page.refreshDocumentState();
        }
    }

    Connections
    {
        target: session.textBridge

        function onDocumentActivated()
        {
            session.intro.play(session.textBridge.markdown ? "markdown" : "writer");
        }

        function onSaveDialogRequested()
        {
            session.savePrompt.selectedFile = session.textBridge.saveUrl;
            session.savePrompt.open();
        }

        function onConfirmUnsavedRequested()
        {
            session.host.unsavedOwner = "text";
            session.unsavedPrompt.open();
        }

        function onWindowCloseAllowed()
        {
            session.host.allowWindowClose = true;
            session.host.close();
        }
    }
}
