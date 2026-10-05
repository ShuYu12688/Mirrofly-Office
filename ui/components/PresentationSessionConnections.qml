import QtQuick

Item
{
    id: session
    visible: false

    required property var host
    required property var presentationBridge
    required property var intro
    required property var savePrompt
    required property var unsavedPrompt

    Connections
    {
        target: session.presentationBridge

        function onDocumentActivated()
        {
            if (!(session.intro.running && session.intro.loadingMode && session.intro.kind === "slides"))
                session.intro.play("slides");
        }

        function onLoadStarted()
        {
            session.host.slideFramePending = false;
            session.intro.beginLoading("slides");
        }

        function onCopyCompleted(success)
        {
            if (session.intro.kind !== "slides") return;
            if (!success) session.intro.cancelLoading();
            else
            {
                session.host.slideFramePending = session.intro.exposed;
                if (session.host.slideFramePending) session.host.update();
                else session.intro.finishLoading();
            }
        }

        function onOpenCompleted(success)
        {
            if (!success && session.intro.loadingMode) session.intro.cancelLoading();
        }

        function onSaveDialogRequested()
        {
            session.savePrompt.selectedFile = session.presentationBridge.saveUrl;
            session.savePrompt.open();
        }

        function onConfirmUnsavedRequested()
        {
            session.host.unsavedOwner = "slides";
            session.unsavedPrompt.open();
        }

        function onWindowCloseAllowed()
        {
            session.host.allowWindowClose = true;
            session.host.close();
        }

        function onStateChanged()
        {
            if (!session.presentationBridge.active && session.host.presentationFullscreen)
            {
                session.host.togglePresentationFullscreen();
            }
        }
    }
}
