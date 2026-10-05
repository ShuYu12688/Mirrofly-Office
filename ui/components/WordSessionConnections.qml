import QtQuick

Item
{
    id: session
    visible: false

    required property var host
    required property var wordBridge
    required property var page
    required property var intro
    required property var savePrompt
    required property var unsavedPrompt

    Connections
    {
        target: session.wordBridge

        function onDocumentActivated()
        {
            if (!(session.intro.running && session.intro.loadingMode && session.intro.kind === "word"))
                session.intro.play("word");
        }

        function onLoadStarted()
        {
            session.host.wordFramePending = false;
            session.intro.beginLoading("word");
        }

        function onLoadReady()
        {
            if (session.intro.kind !== "word") return;
            session.host.wordFramePending = session.intro.exposed;
            if (session.host.wordFramePending) session.host.update();
            else session.intro.finishLoading();
        }

        function onOpenCompleted(success)
        {
            if (!success && session.intro.kind === "word" && session.intro.loadingMode)
            {
                session.host.wordFramePending = false;
                session.intro.cancelLoading();
            }
        }

        function onCopyCompleted(success)
        {
            if (!success && session.intro.kind === "word")
            {
                session.host.wordFramePending = false;
                session.intro.cancelLoading();
            }
        }

        function onEditorChanged() { session.page.refreshSelection(); }

        function onSaveDialogRequested()
        {
            session.savePrompt.selectedFile = session.wordBridge.saveUrl;
            session.savePrompt.open();
        }

        function onConfirmUnsavedRequested()
        {
            session.host.unsavedOwner = "word";
            session.unsavedPrompt.open();
        }

        function onWindowCloseAllowed()
        {
            session.host.allowWindowClose = true;
            session.host.close();
        }
    }
}
