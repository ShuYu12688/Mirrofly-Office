import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import Mirrorfly.Native 1.0
import "components"

ApplicationWindow
{
    id: root

    property var theme: appBridge.theme
    readonly property var systemFontFamilies: Qt.fontFamilies().filter(function(name)
    {
        return name.charAt(0) !== "@";
    })
    FeatureTheme { id: slidesTheme; base: root.theme; kind: "slides" }
    FeatureTheme { id: sheetsTheme; base: root.theme; kind: "sheets" }
    FeatureTheme { id: wordTheme; base: root.theme; kind: "word" }
    FeatureTheme { id: pdfTheme; base: root.theme; kind: "pdf" }
    FeatureTheme { id: mindmapTheme; base: root.theme; kind: "mindmap" }
    readonly property bool canvasActive: pdfEditor.active || mindmapEditor.active
    readonly property var canvasSession: pdfEditor.active ? pdfEditor : mindmapEditor
    property string pdfExportModule: "text"
    property var pdfExportOptions: ({})
    property var imageExportOptions: ({})
    property bool replacingPresentationImage: false
    readonly property bool assistantAvailable: startupLoading.finished && aiAgent.configured
        && !root.presentationFullscreen
    readonly property string assistantLocation: root.automationViewState.module
        + (root.automationViewState.module === "home" ? "" : " / " + root.title)
    onAssistantAvailableChanged: aiIsland.setAvailable(assistantAvailable)
    onAssistantLocationChanged: aiIsland.setLocation(assistantLocation)
    Component.onCompleted:
    {
        aiIsland.setAvailable(assistantAvailable);
        aiIsland.setLocation(assistantLocation);
    }
    property bool wordFramePending: false
    property bool slideFramePending: false
    onFrameSwapped:
    {
        if (slideFramePending)
        {
            slideFramePending = false;
            if (featureIntro.kind === "slides") featureIntro.finishLoading();
        }
        if (wordFramePending)
        {
            wordFramePending = false;
            if (featureIntro.kind === "word") featureIntro.finishLoading();
        }
    }
    function beginPdfExport(module)
    {
        spreadsheet.commitTextInput();
        if (pdfExporter.busy || !root.settleActiveInput()) return;
        pdfExportModule = module;
        pdfExportDialog.open();
    }
    function automationReady()
    {
        return editorInteractionAllowed && !spreadsheetPage.editing && !canvasPage.editing
            && !presentationPage.canvasEditing && !presentationPage.transforming
            && !wordPage.automationSelection.composing && !editorPage.automationSelection.composing
            && (!textEditor.active || !editorPage.applyingContent);
    }
    readonly property var automationViewState: ({ready: automationReady(), module: pdfEditor.active ? "pdf" : mindmapEditor.active ? "mindmap"
            : wordEditor.active ? "word" : spreadsheet.active ? "sheets" : presentation.active ? "slides"
            : textEditor.active ? (textEditor.markdown ? "markdown" : "text") : "home",
            selection: wordEditor.active ? wordPage.automationSelection : textEditor.active ? editorPage.automationSelection
                : spreadsheet.active ? spreadsheet.rangeInfo : presentation.active ? presentation.selection : root.canvasSession.viewData.selectedId || "",
            pendingInput: spreadsheetPage.editing || canvasPage.editing || presentationPage.canvasEditing || presentationPage.transforming,
            blockers: [unsavedDialog.opened ? "unsaved_confirmation" : "",
                fileDialog.visible || saveDialog.visible || canvasSaveDialog.visible || wordSaveDialog.visible
                    || presentationSaveDialog.visible || spreadsheetSaveDialog.visible ? "file_dialog" : "",
                wordPage.modalActive || editorPage.modalActive || canvasPage.modalActive
                    || presentationPage.modalActive || spreadsheetPage.modalActive ? "editor_dialog" : ""].filter(function(value) { return value.length > 0; }),
            presentation: {mode: root.presentationFullscreen
                ? (root.dualScreenPresenter ? "presenter" :
                    presentationPage.presenterMode ? "rehearsal" : "slideshow") : "editor",
                currentSlide: presentation.currentSlide, nextSlide: presentationPage.nextPage},
            home: {route: homePage.route, section: homePage.expandedSection, query: root.searchQuery, category: root.selectedCategory}})
    function automationState()
    {
        return automationViewState;
    }
    function automationHomeView(route: string, query: string, category: string): bool
    {
        if (automationViewState.module !== "home"
            || ["home", "create", "recent", "local", "ai"].indexOf(route) < 0
            || ["all", "recent", "starred", "writer", "sheets", "slides", "pdf", "mindmap", "other"].indexOf(category) < 0)
            return false;
        root.searchQuery = query;
        root.selectedCategory = category;
        homePage.navigate(route);
        return true;
    }
    function automationSlideMedia(shape: int, action: string, value: real): bool
    {
        return presentationPage.mediaCommand(shape, action, value);
    }
    function automationSlideAnimation(action: string, value: real): bool
    {
        return presentationPage.animationCommand(action, value);
    }
    function automationSlidePlayback()
    {
        return {media: presentationPage.automationMedia, animation: presentationPage.automationAnimation,
            transition: presentationPage.automationTransition,
            rehearsal: {active: root.presentationFullscreen && presentationPage.presenterMode
                    && !root.dualScreenPresenter,
                elapsedSeconds: presentationPage.presenterElapsedSeconds,
                currentSlide: presentation.currentSlide, nextSlide: presentationPage.nextPage},
            presenter: {active: root.dualScreenPresenter,
                audienceScreen: root.dualScreenPresenter && root.audienceScreen
                    ? root.audienceScreen.name : ""}};
    }
    function automationSlidePresenter(mode: string): bool
    {
        if (mode === "start") return root.startDualPresenter();
        if (mode === "stop" && root.dualScreenPresenter)
        {
            root.togglePresentationFullscreen();
            return true;
        }
        return false;
    }
    function automationSlideRehearsal(mode: string): bool
    {
        if (!presentation.active || presentation.locked || !presentationPage.hasVisibleSlides)
            return false;
        if (mode === "start")
        {
            if (root.presentationFullscreen) return false;
            presentationPage.presenterMode = true;
            root.togglePresentationFullscreen();
            return true;
        }
        if (mode === "stop" && root.presentationFullscreen && presentationPage.presenterMode
            && !root.dualScreenPresenter)
        {
            root.togglePresentationFullscreen();
            return true;
        }
        return false;
    }
    function automationDidExecute(module, action, args, result)
    {
        if (action === "resolveUnsaved" && module === root.unsavedOwner) unsavedDialog.close();
        if (action === "selectSaveFile" || action === "cancelSaveDialog")
        {
            if (module === "word") wordSaveDialog.close();
            else if (module === "text") saveDialog.close();
            else if (module === "sheets") spreadsheetSaveDialog.close();
            else if (module === "slides") presentationSaveDialog.close();
            else if (module === root.canvasSaveOwner) canvasSaveDialog.close();
        }
        if (module === "sheets" && spreadsheet.active) spreadsheetPage.revealCell();
        else if (module === "word" && wordEditor.active)
        {
            if (action === "insertText") wordPage.selectMatch({start: args[0], end: args[0] + args[2].length});
            else if (action === "replace") wordPage.selectMatch({start: args[0], end: args[0] + args[3].length});
            else if (action === "format") wordPage.selectMatch({start: args[0], end: args[1]});
            wordPage.refreshSelection();
            wordPage.syncTypingFormat();
        }
        return true;
    }
    FeatureTheme { id: writerTheme; base: root.theme; kind: "writer" }
    FeatureTheme { id: markdownTheme; base: root.theme; kind: "markdown" }
    readonly property var textTheme: textEditor.markdown ? markdownTheme.values : writerTheme.values

    function settleCanvasInput()
    {
        presentationPage.finishTextEditing();
    }

    property bool allowWindowClose: false
    property bool exitPending: false
    property bool fullscreenWasMaximized: false
    property bool presentationFullscreen: false
    property bool dualScreenPresenter: false
    PresentationScreens { id: presentationScreens }
    readonly property var audienceScreen:
    {
        for (const candidate of presentationScreens.screens)
            if (candidate !== root.screen)
                return candidate;
        return null;
    }
    onAudienceScreenChanged:
    {
        if (root.dualScreenPresenter && !root.audienceScreen)
        {
            root.dualScreenPresenter = false;
            audienceWindow.close();
        }
    }
    property int lastNonFullscreenVisibility: Window.Windowed
    property string unsavedOwner: "text"
    function resolveUnsaved(decision)
    {
        if (decision === "cancel") root.exitPending = false;
        if (unsavedOwner === "pdf") pdfEditor.resolveUnsaved(decision);
        else if (unsavedOwner === "mindmap") mindmapEditor.resolveUnsaved(decision);
        else if (unsavedOwner === "word") wordEditor.resolveUnsaved(decision);
        else if (unsavedOwner === "sheets") spreadsheet.resolveUnsaved(decision);
        else if (unsavedOwner === "slides") presentation.resolveUnsaved(decision);
        else textEditor.resolveUnsaved(decision);
    }

    function settleActiveInput(action = "")
    {
        root.settleCanvasInput();
        if (root.canvasActive && !canvasPage.prepareNavigation(action)) return false;
        return !spreadsheet.active || (action.length > 0
            ? spreadsheetPage.prepareNavigation(action) : spreadsheetPage.finishCellEditing());
    }
    readonly property bool editorInteractionAllowed: appBridge.ready && !textEditor.locked && !presentation.locked && !spreadsheet.locked && !wordEditor.locked && !pdfEditor.locked && !mindmapEditor.locked
        && !pdfExportDialog.opened && !pdfExportFileDialog.visible && !imageExportDialog.opened
        && !imageExportFolderDialog.visible && !unsavedDialog.opened && !canvasSaveDialog.visible && !featureIntro.running && !canvasPage.modalActive
        && !wordPage.modalActive && !wordSaveDialog.visible && !editorPage.modalActive && !presentationPage.modalActive && !spreadsheetPage.modalActive && !fileDialog.visible
        && !saveDialog.visible && !presentationSaveDialog.visible && !spreadsheetSaveDialog.visible && !imageDialog.visible
    property string selectedCategory: "all"
    property string searchQuery: ""
    function togglePresentationFullscreen()
    {
        if (!root.presentationFullscreen && (!presentation.active || !presentationPage.hasVisibleSlides))
        {
            return;
        }
        root.settleCanvasInput();
        if (root.presentationFullscreen)
        {
            root.dualScreenPresenter = false;
            audienceWindow.close();
            root.presentationFullscreen = false;
            if (root.fullscreenWasMaximized)
            {
                root.showMaximized();
            }
            else
            {
                root.showNormal();
            }
        }
        else
        {
            root.fullscreenWasMaximized = root.visibility === Window.Maximized;
            root.presentationFullscreen = true;
            root.showFullScreen();
        }
    }
    function startDualPresenter(): bool
    {
        if (root.presentationFullscreen || !root.audienceScreen ||
            !presentation.active || presentation.locked || !presentationPage.hasVisibleSlides)
            return false;
        root.settleCanvasInput();
        audienceWindow.screen = root.audienceScreen;
        root.dualScreenPresenter = true;
        presentationPage.presenterMode = true;
        root.togglePresentationFullscreen();
        audienceWindow.showFullScreen();
        Qt.callLater(root.requestActivate);
        return true;
    }
    property var filteredFiles:
    {
        const sourceFiles = appBridge.recentFiles;
        return appBridge.filterFiles(searchQuery, selectedCategory);
    }

    width: 1260
    height: 820
    minimumWidth: 1040
    minimumHeight: 720
    visible: true
    onVisibilityChanged:
    {
        if (visibility === Window.Windowed || visibility === Window.Maximized)
        {
            lastNonFullscreenVisibility = visibility;
        }
        else if (visibility === Window.FullScreen && !presentationFullscreen)
        {
            Qt.callLater(function()
            {
                if (root.lastNonFullscreenVisibility === Window.Maximized) root.showMaximized();
                else root.showNormal();
            });
        }
    }
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint
        | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint
    title: root.canvasActive ? root.canvasSession.documentName + (root.canvasSession.modified ? " *" : "") + " — Mirrorfly Office" : wordEditor.active ? wordEditor.documentName + (wordEditor.modified ? " *" : "") + " — Mirrorfly Office" : textEditor.active
        ? textEditor.documentName + (textEditor.modified ? " *" : "") + " — Mirrorfly Office"
        : (presentation.active ? presentation.documentName + (presentation.modified ? " *" : "")
            + " — Mirrorfly Office" : (spreadsheet.active ? spreadsheet.documentName
                + (spreadsheet.modified ? " *" : "") + " — Mirrorfly Office" : "Mirrorfly Office"))
    color: theme.transparentColor
    opacity: theme.windowOpacity
    font.family: theme.fontFamily
    font.pixelSize: theme.fontSize
    palette.window: theme.surfaceColor
    palette.windowText: theme.textPrimary
    palette.base: theme.surfaceColor
    palette.text: theme.textPrimary
    palette.button: theme.hoverColor
    palette.buttonText: theme.textPrimary
    palette.highlight: theme.accent
    palette.highlightedText: theme.onAccent

    onClosing: function(close)
    {
        if (!root.allowWindowClose)
        {
            if (canvasSaveDialog.visible || wordPage.modalActive || wordSaveDialog.visible || editorPage.modalActive || presentationPage.modalActive || spreadsheetPage.modalActive || fileDialog.visible
                || saveDialog.visible || presentationSaveDialog.visible || spreadsheetSaveDialog.visible || imageDialog.visible
                || presentation.locked || spreadsheet.locked || wordEditor.locked || pdfEditor.locked || mindmapEditor.locked)
            {
                close.accepted = false;
                root.exitPending = false;
                return;
            }
            if (!root.settleActiveInput("close"))
            {
                close.accepted = false;
                root.exitPending = false;
                return;
            }
            if (appBridge.residentEnabled && !root.exitPending)
            {
                close.accepted = false;
                if (root.presentationFullscreen) root.togglePresentationFullscreen();
                appBridge.hideMainWindow();
                return;
            }
            close.accepted = root.canvasActive ? root.canvasSession.requestWindowClose() : wordEditor.active ? wordEditor.requestWindowClose() : spreadsheet.active ? spreadsheet.requestWindowClose()
                : (presentation.active ? presentation.requestWindowClose() : textEditor.requestWindowClose());
        }
        if (close.accepted && appBridge.residentEnabled) appBridge.finishQuit();
        else root.exitPending = false;
    }

    WindowFrame
    {
        id: windowFrame
        isolatedLoading: featureIntro.running
        objectName: "windowFrame"

        anchors.fill: parent
        theme: root.theme
        targetWindow: root
        chromeVisible: !featureIntro.running && (!startupLoading.visible || startupLoading.phase === "controls")
        outlineVisible: !featureIntro.running && (!startupLoading.visible || startupLoading.phase === "controls")
        chromeOpacity: startupLoading.visible ? startupLoading.controlsOpacity : 1
        outlineOpacity: startupLoading.visible ? startupLoading.controlsOpacity : 1

        HomePage
        {
            id: homePage

            chromeInset: windowFrame.titlebarHeight
            anchors.fill: parent
            theme: root.theme
            controlsOpacity: startupLoading.visible ? startupLoading.controlsOpacity : 1
            brandVisible: !startupLoading.visible
            agent: aiAgent
            islandStatus: aiIsland.status
            files: root.filteredFiles
            recentCount: appBridge.recentFiles.length
            category: root.selectedCategory
            query: root.searchQuery
            notice: root.canvasSession.message.length > 0 ? root.canvasSession.message : wordEditor.message.length > 0 ? wordEditor.message : spreadsheet.error.length > 0 ? spreadsheet.error : (presentation.error.length > 0 ? presentation.error
                : (textEditor.message.length > 0 ? textEditor.message : appBridge.notice))
            visible: !textEditor.active && !presentation.active && !spreadsheet.active && !wordEditor.active && !root.canvasActive
            enabled: appBridge.ready && !textEditor.locked && !presentation.locked && !spreadsheet.locked && !wordEditor.locked && !pdfEditor.locked && !mindmapEditor.locked
            onOpenRequested: appBridge.chooseFile()
            onOpenIslandRequested: aiIsland.open()
            onCreateRequested: function(kind)
            {
                appBridge.requestCreate(kind);
            }
            onQueryEdited: function(query)
            {
                root.searchQuery = query;
            }
            onCategorySelected: function(category)
            {
                root.selectedCategory = category;
            }
            onFileRequested: function(path)
            {
                appBridge.inspectFile(path);
            }
            onStarRequested: function(path)
            {
                appBridge.toggleStar(path);
            }
            onReplayRequested: appBridge.replayLoading()
            onDismissNotice:
            {
                pdfEditor.clearMessage(); mindmapEditor.clearMessage();
                wordEditor.clearMessage();
                spreadsheet.clearError();
                appBridge.clearNotice();
                textEditor.clearMessage();
                presentation.clearError();
                presentation.clearMessage();
            }
        }

        TextEditorPage
        {
            id: editorPage

            chromeInset: windowFrame.titlebarHeight
            zoom: textEditor.zoom
            onZoomRequested: function(value) { textEditor.setZoom(value); }
            anchors.fill: parent
            theme: root.textTheme
            visible: textEditor.active
            opacity: featureIntro.running ? 0 : 1
            content: textEditor.content
            revision: textEditor.revision
            documentName: textEditor.documentName
            documentPath: textEditor.documentPath
            modified: textEditor.modified
            markdown: textEditor.markdown
            overlayRadius: windowFrame.cornerRadius
            busy: textEditor.locked || presentation.locked || spreadsheet.locked || wordEditor.locked || pdfEditor.locked || mindmapEditor.locked || !appBridge.ready || featureIntro.running
            formatLabel: textEditor.formatLabel
            message: wordEditor.message.length > 0 ? wordEditor.message : spreadsheet.error.length > 0 ? spreadsheet.error : (presentation.error.length > 0 ? presentation.error : textEditor.message)
            onDocumentLoadRequested: function(document, source, markdown)
            {
                editorPage.finishDocumentLoad(editorTools.loadDocument(document, source, markdown, root.textTheme, textEditor.documentPath));
            }
            onDocumentStateRequested: function(document, cursor)
            {
                editorPage.applyDocumentState(editorTools.inspectDocument(document, cursor));
            }
            onHomeRequested: textEditor.requestHome()
            onNewRequested: appBridge.requestCreate(textEditor.markdown ? "markdown" : "writer")
            onOpenRequested: appBridge.chooseFile()
            onSaveRequested: textEditor.save()
            onSaveAsRequested: textEditor.saveAs()
            onPdfRequested: root.beginPdfExport("text")
            onDismissMessage:
            {
                wordEditor.clearMessage();
                textEditor.clearMessage();
                presentation.clearError();
                presentation.clearMessage();
            }
            onMarkdownActionRequested: function(action, document, start, end, options)
            {
                if (!textEditor.locked)
                {
                    editorPage.applyMarkdownEdit(editorTools.applyEdit(document, start, end, action, options));
                }
            }
        }

        PresentationPage
        {
            id: presentationPage
            chineseFontFamilies: presentation.chineseFontFamilies
            systemFontFamilies: root.systemFontFamilies
            templatePreviews: presentation.templatePreviews(presentationPage.templateOptions)

            chromeInset: windowFrame.titlebarHeight
            anchors.fill: parent
            theme: slidesTheme.values
            visible: presentation.active
            enabled: appBridge.ready && !textEditor.locked && !spreadsheet.locked && !wordEditor.locked && !pdfEditor.locked && !mindmapEditor.locked && !featureIntro.running
            document: presentation.document
            searchFunction: function(query, caseSensitive, offset)
            {
                return presentation.findText(query, caseSensitive, offset);
            }
            paragraphInfoFunction: function(index) { return presentation.paragraphInfo(index); }
            guideSettings: presentation.guideSettings
            slideTransition: presentation.slideTransition
            busy: presentation.locked || textEditor.locked || spreadsheet.locked || wordEditor.locked || pdfEditor.locked || mindmapEditor.locked
            imageExportBusy: imageExporter.busy
            syncing: presentation.syncing
            pendingEdits: presentation.pendingEdits
            error: wordEditor.message.length > 0 ? wordEditor.message : spreadsheet.error.length > 0 ? spreadsheet.error : (presentation.error.length > 0 ? presentation.error : textEditor.message)
            documentName: presentation.documentName
            documentPath: presentation.documentPath
            slideCount: presentation.slideCount
            hiddenSlides: presentation.hiddenSlides
            slideSections: presentation.slideSections
            sectionsEditable: presentation.sectionsEditable
            currentSlide: presentation.currentSlide
            speakerNotes: presentation.speakerNotes(presentation.currentSlide)
            zoom: presentation.zoom
            fontSummary: presentation.fontSummary
            editable: presentation.editable
            themeEditable: presentation.themeEditable
            themeState: presentation.themeState
            modified: presentation.modified
            canUndo: presentation.canUndo
            canRedo: presentation.canRedo
            selectedShape: presentation.selectedShape
            selection: presentation.selection
            slideWidth: presentation.slideWidth
            slideHeight: presentation.slideHeight
            message: presentation.message
            fullscreen: root.presentationFullscreen
            dualScreenPresenter: root.dualScreenPresenter
            secondaryScreenAvailable: root.audienceScreen !== null
            externalPlayback: audienceWindow.playbackView
            onHomeRequested: { root.settleCanvasInput(); presentation.showHome(); }
            onOpenRequested: { root.settleCanvasInput(); appBridge.chooseFile(); }
            onNewRequested: { root.settleCanvasInput(); appBridge.requestCreate("slides"); }
            onSaveRequested: { root.settleCanvasInput(); presentation.save(); }
            onSaveAsRequested: { root.settleCanvasInput(); presentation.saveAs(); }
            onPdfRequested: root.beginPdfExport("slides")
            onImageExportRequested: imageExportDialog.open()
            onUndoRequested: presentation.undo()
            onRedoRequested: presentation.redo()
            onEditableCopyRequested: presentation.createEditableCopy()
            onShapeRequested: function(index)
            {
                root.settleCanvasInput();
                presentation.selectShape(index);
            }
            onEditRequested: function(action, options)
            {
                if (!presentation.applyEdit(action, options))
                {
                    presentationPage.restoreSelection();
                }
            }
            onImageRequested:
            {
                root.replacingPresentationImage = false;
                imageDialog.open();
            }
            onGuideSettingsRequested: function(patch) { presentation.setGuideSettings(patch); }
            onImageReplaceRequested:
            {
                root.replacingPresentationImage = true;
                imageDialog.open();
            }
            onFirstFrameReady:
            {
                presentation.finishLoadingFrame();
                if (featureIntro.kind === "slides" && !presentation.busy) featureIntro.finishLoading();
            }
            onSlideRequested: function(index)
            {
                root.settleCanvasInput();
                presentation.setSlide(index);
            }
            onZoomRequested: function(value)
            {
                presentation.setZoom(value);
            }
            onFullscreenRequested: root.togglePresentationFullscreen()
            onDualPresenterRequested: root.startDualPresenter()
            onDismissError:
            {
                wordEditor.clearMessage();
                presentation.clearError();
                presentation.clearMessage();
                textEditor.clearMessage();
            }
        }

        SpreadsheetPage
        {
            id: spreadsheetPage
            systemFontFamilies: root.systemFontFamilies
            anchors.fill: parent
            chromeInset: windowFrame.titlebarHeight
            zoom: spreadsheet.zoom
            onZoomRequested: function(value) { spreadsheet.setZoom(value); }
            theme: sheetsTheme.values
            visible: spreadsheet.active
            opacity: featureIntro.running ? 0 : 1
            enabled: appBridge.ready && !featureIntro.running
            tableModel: spreadsheet.model
            layoutInfo: spreadsheet.layoutInfo
            onStartToolRequested: function(action, args) { spreadsheet.startTool(action, args); }
            formatInfo: spreadsheet.formatInfo
            layoutRevision: spreadsheet.layoutRevision
            function columnWidthFor(column) { return spreadsheet.columnWidth(column); }
            function rowHeightFor(row) { return spreadsheet.rowHeight(row); }
            onFormatRequested: function(patch) { spreadsheet.formatSelection(patch); }
            onStyleRequested: function(preset, palette) { spreadsheet.styleSelection(preset, palette); }
            onSizeRequested: function(columns, size) { spreadsheet.resizeSelection(columns, size); }
            onSortRequested: function(descending, header) { spreadsheet.sortSelection(descending, header); }
            onTemplateRequested: function(name) { spreadsheet.insertTemplate(name); }
            sheetNames: spreadsheet.sheetNames
            currentSheet: spreadsheet.currentSheet
            cellInfo: spreadsheet.cellInfo
            rangeInfo: spreadsheet.rangeInfo
            documentName: spreadsheet.documentName
            documentPath: spreadsheet.documentPath
            modified: spreadsheet.modified
            busy: spreadsheet.locked || textEditor.locked || presentation.locked || wordEditor.locked || pdfEditor.locked || mindmapEditor.locked
            canUndo: spreadsheet.canUndo
            canRedo: spreadsheet.canRedo
            error: wordEditor.message.length > 0 ? wordEditor.message : spreadsheet.error.length > 0 ? spreadsheet.error
                : (presentation.error.length > 0 ? presentation.error : textEditor.message)
            compatibilitySummary: spreadsheet.compatibilitySummary
            onNavigationReady: function(action)
            {
                if (action === "markdown") appBridge.requestCreate("markdown");
                else if (action === "close") root.close();
            }
            onHomeRequested: spreadsheet.requestHome()
            onNewRequested: appBridge.requestCreate("sheets")
            onOpenRequested: appBridge.chooseFile()
            onSaveRequested: spreadsheet.save()
            onSaveAsRequested: spreadsheet.saveAs()
            onPdfRequested: root.beginPdfExport("sheets")
            onUndoRequested: spreadsheet.undo()
            onRedoRequested: spreadsheet.redo()
            onSheetRequested: function(index) { spreadsheet.selectSheet(index); }
            onAddSheetRequested: function(name) { spreadsheet.addSheet(name); }
            onRenameSheetRequested: function(name) { spreadsheet.renameSheet(name); }
            onBandRequested: function(first, last, rows) { spreadsheet.selectBand(first, last, rows); }
            onCellRequested: function(row, column, extend) { spreadsheet.selectCell(row, column, extend); }
            onFindRequested: function(query, backwards) { if (spreadsheet.findCell(query, backwards)) spreadsheetPage.revealCell(); }
            onAddressRequested: function(address) { if (spreadsheet.selectAddress(address)) spreadsheetPage.revealCell(); }
            onCopyRequested: spreadsheet.copyCell()
            onPasteRequested: spreadsheet.pasteCell()
            onClearRequested: spreadsheet.clearSelection()
            onInputCommitRequested: spreadsheet.commitTextInput()
            onCellEditRequested: function(row, column, value, kind)
            {
                spreadsheetPage.acceptCellEdit(spreadsheet.setCellValue(row, column, value, kind));
            }
            onDismissError: { wordEditor.clearMessage(); spreadsheet.clearError(); presentation.clearError(); textEditor.clearMessage(); }
        }

        WordPage
        {
            id: wordPage
            systemFontFamilies: root.systemFontFamilies
            anchors.fill: parent
            chromeInset: windowFrame.titlebarHeight
            zoom: wordEditor.zoom
            onZoomRequested: function(value) { wordEditor.setZoom(value); }
            theme: wordTheme.values
            visible: wordEditor.active
            // Build retained text nodes under the opaque loading overlay before revealing the page.
            enabled: appBridge.ready && !featureIntro.running
            busy: wordEditor.locked || spreadsheet.locked || presentation.locked || textEditor.locked
            revision: wordEditor.revision
            documentName: wordEditor.documentName
            modified: wordEditor.modified
            readOnly: wordEditor.readOnly
            formatReady: wordEditor.formatReady
            onCopySelectionRequested: function(start, end, cut) { wordEditor.copySelection(start, end, cut); }
            onCopyFormatRequested: function(position) { wordEditor.copyFormat(position); }
            onPasteFormatRequested: function(start, end) { wordEditor.pasteFormat(start, end); }
            onReplaceAllRequested: function(query, replacement) { wordEditor.replaceAll(query, replacement); }
            statistics: wordEditor.statistics
            chineseFonts: wordEditor.chineseFonts
            message: wordEditor.message.length > 0 ? wordEditor.message
                : (spreadsheet.error.length > 0 ? spreadsheet.error : (presentation.error.length > 0 ? presentation.error : textEditor.message))
            onLoadRequested: function(document) { wordEditor.loadEditor(document); }
            onInspectRequested: function(position) { wordPage.selection = wordEditor.inspect(position); }
            onFormatRequested: function(start, end, action, value) { wordEditor.format(start, end, action, value); }
            onFindRequested: function(query, from, backward) { wordPage.selectMatch(wordEditor.find(query, from, backward)); }
            onReplaceRequested: function(start, end, expected, replacement) { wordEditor.replace(start, end, expected, replacement); }
            onPasteRequested: function(start, end) { wordEditor.paste(start, end); }
            onPastePlainRequested: function(start, end) { wordEditor.pastePlain(start, end); }
            onParagraphRequested: function(start, end) { wordPage.finishParagraph(wordEditor.insertParagraph(start, end)); }
            onTemplateRequested: function(position, kind) { wordEditor.insertTemplate(position, kind); }
            onHomeRequested: wordEditor.requestHome()
            onNewRequested: appBridge.requestCreate("word")
            onOpenRequested: appBridge.chooseFile()
            onSaveRequested: wordEditor.save()
            onSaveAsRequested: wordEditor.saveAs()
            onPdfRequested: root.beginPdfExport("word")
            onEditableCopyRequested: wordEditor.requestEditableCopy()
            onUndoRequested: wordEditor.undo()
            onRedoRequested: wordEditor.redo()
        }

        CanvasPage
        {
            id: canvasPage
            anchors.fill: parent
            chromeInset: windowFrame.titlebarHeight
            zoom: root.canvasSession.zoom
            onZoomRequested: function(value) { root.canvasSession.setZoom(value); }
            visible: root.canvasActive
            opacity: featureIntro.running ? 0 : 1
            enabled: appBridge.ready && !featureIntro.running
            kind: root.canvasSession.kind
            theme: pdfEditor.active ? pdfTheme.values : mindmapTheme.values
            documentName: root.canvasSession.documentName
            modified: root.canvasSession.modified
            busy: textEditor.locked || presentation.locked || spreadsheet.locked || wordEditor.locked || pdfEditor.locked || mindmapEditor.locked
            canUndo: root.canvasSession.canUndo
            canRedo: root.canvasSession.canRedo
            message: root.canvasSession.message
            view: root.canvasSession.viewData
            preview: root.canvasSession.image
            commandHandler: function(action, args) { return root.canvasSession.execute(action, args); }
            onHomeRequested: root.canvasSession.requestHome()
            onOpenRequested: appBridge.chooseFile()
            onNewRequested: root.canvasSession.requestNew()
            onSaveRequested: root.canvasSession.save()
            onSaveAsRequested: root.canvasSession.saveAs()
            onPdfRequested: root.beginPdfExport(root.canvasSession.kind)
            onUndoRequested: root.canvasSession.undo()
            onRedoRequested: root.canvasSession.redo()
            onPageRequested: function(index) { pdfEditor.selectPage(index); }
            onNodeRequested: function(id) { mindmapEditor.selectNode(id); }
            connectionMode: mindmapEditor.connectionMode
            connectFrom: mindmapEditor.connectionFrom
            function connectionHandler(mode, from) { return mindmapEditor.beginConnection(mode, from); }
            function connectHandler(target) { return mindmapEditor.connectNode(target); }
            onRenderSizeRequested: function(width, height) { pdfEditor.setRenderSize(width, height); }
            onCopyOutlineRequested: root.canvasSession.copyOutline()
        }

        LoadingPage
        {
            id: startupLoading
            objectName: "startupLoading"
            anchors.fill: parent
            theme: root.theme
            ready: appBridge.ready
            destinationMark: homePage.brandMark
        }
    }

    FeatureIntro
    {
        id: featureIntro
        objectName: "featureIntro"

        anchors.fill: parent
        z: 100
        theme: root.theme
        loadingProgress: featureIntro.kind === "word" ? wordEditor.loadingProgress : presentation.loadingProgress
        loadingStatus: featureIntro.kind === "word" ? wordEditor.loadingStage : presentation.loadingStage
        contentReady: (!textEditor.active || !editorPage.applyingContent) && (!pdfEditor.active || pdfEditor.previewReady)
        onFinished:
        {
            if (wordEditor.active) wordPage.focusEditor();
            else if (textEditor.active)
            {
                editorPage.focusEditor();
            }
            else if (spreadsheet.active)
            {
                spreadsheetPage.focusGrid();
            }
            else if (presentation.active)
            {
                presentationPage.focusCanvas();
            }
        }
    }


    NativeDialogs.FileDialog
    {
        id: fileDialog

        title: "打开本地文件"
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters:
        [
            "支持的文件 (*.txt *.text *.md *.markdown *.docx *.xlsx *.pptx *.pdf *.mfg)",
            "PDF 文档 (*.pdf)",
            "Mirrorfly 思维导图 (*.mfg)",
            "文本与 Markdown (*.txt *.text *.md *.markdown)",
            "PowerPoint 演示文稿 (*.pptx)",
            "Excel 工作簿 (*.xlsx)",
            "Word 文档 (*.docx)",
            "办公文档 (*.docx *.doc *.odt *.rtf *.txt *.text *.md *.markdown *.xlsx *.xls *.ods *.csv *.pptx *.ppt *.odp *.pdf)",
            "所有文件 (*)"
        ]
        onAccepted: appBridge.selectFile(selectedFile)
    }

    PresentationAudienceWindow
    {
        id: audienceWindow
        theme: slidesTheme.values
        document: presentation.document
        currentSlide: presentation.currentSlide
        slideWidth: presentation.slideWidth
        playing: root.dualScreenPresenter && root.presentationFullscreen
        busy: presentation.locked
        onAdvanceRequested: presentationPage.advancePresentation()
        onPreviousRequested:
        {
            if (presentationPage.previousPage >= 0)
                presentation.setSlide(presentationPage.previousPage);
        }
        onSlideClicked: function(x, y)
        {
            const playback = audienceWindow.playbackView;
            if (playback.transitionState.active) playback.seekTransition(1);
            else if (presentationPage.followClickAction(x, y)) return;
            else if (playback.triggerAnimationAt(x, y)) return;
            else if (playback.transitionState.advanceOnClick !== false)
                presentationPage.advancePresentation();
        }
        onExitRequested:
        {
            if (root.dualScreenPresenter) root.togglePresentationFullscreen();
        }
        onVisibleChanged:
        {
            if (visible) Qt.callLater(presentationPage.scheduleAutoAdvance);
        }
    }

    PresentationImageExportDialog
    {
        id: imageExportDialog
        theme: root.theme
        onDestinationRequested: function(options)
        {
            root.imageExportOptions = options;
            imageExportFolderDialog.open();
        }
    }
    NativeDialogs.FolderDialog
    {
        id: imageExportFolderDialog
        title: "选择幻灯片图片的保存位置"
        onAccepted: imageExporter.start(selectedFolder, root.imageExportOptions)
    }
    PdfExportDialog
    {
        id: pdfExportDialog
        theme: root.theme
        module: root.pdfExportModule
        onDestinationRequested: function(options) { root.pdfExportOptions = options; pdfExportFileDialog.open(); }
    }
    NativeDialogs.FileDialog
    {
        id: pdfExportFileDialog
        title: "保存 PDF 副本"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF 文件 (*.pdf)"]
        onAccepted: pdfExporter.start(root.pdfExportModule, selectedFile, root.pdfExportOptions)
    }
    Rectangle
    {
        parent: root.contentItem
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 22
        width: Math.min(parent.width - 40, 700)
        height: exportStatus.implicitHeight + 20
        radius: root.theme.radius
        color: root.theme.surfaceColor
        border.color: root.theme.accent
        visible: pdfExporter.busy || pdfExporter.message.length > 0
        z: 90
        RowLayout
        {
            id: exportStatus
            anchors.centerIn: parent
            width: parent.width - 24
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: pdfExporter.message + (pdfExporter.busy && pdfExporter.completed > 0 ? " · " + pdfExporter.completed + " 页" : ""); color: root.theme.textPrimary }
            ActionButton { theme: root.theme; text: pdfExporter.busy ? "取消" : "关闭"; compact: true; onClicked: { if (pdfExporter.busy) pdfExporter.cancel(); else pdfExporter.clearMessage(); } }
        }
    }
    Rectangle
    {
        parent: root.contentItem
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: pdfExporter.busy || pdfExporter.message.length > 0 ? 88 : 22
        width: Math.min(parent.width - 40, 700)
        height: imageExportStatus.implicitHeight + 20
        radius: root.theme.radius
        color: root.theme.surfaceColor
        border.color: root.theme.accent
        visible: imageExporter.busy || imageExporter.message.length > 0
        z: 91

        RowLayout
        {
            id: imageExportStatus
            anchors.centerIn: parent
            width: parent.width - 24
            Label
            {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: imageExporter.message + (imageExporter.busy && imageExporter.total > 0
                    ? " · " + imageExporter.completed + " / " + imageExporter.total + " 页" : "")
                color: root.theme.textPrimary
            }
            ActionButton
            {
                theme: root.theme
                text: imageExporter.busy ? "取消" : "关闭"
                compact: true
                onClicked:
                {
                    if (imageExporter.busy) imageExporter.cancel();
                    else imageExporter.clearMessage();
                }
            }
        }
    }
    NativeDialogs.FileDialog
    {
        id: presentationSaveDialog

        title: "保存演示文稿"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "pptx"
        nameFilters: ["PowerPoint 演示文稿 (*.pptx)"]
        onAccepted: presentation.selectSaveFile(selectedFile)
        onRejected: presentation.cancelSaveDialog()
    }

    NativeDialogs.FileDialog
    {
        id: spreadsheetSaveDialog
        title: "保存工作簿"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "xlsx"
        nameFilters: ["Excel 工作簿 (*.xlsx)"]
        onAccepted: spreadsheet.selectSaveFile(selectedFile)
        onRejected: spreadsheet.cancelSaveDialog()
    }

    property string canvasSaveOwner: "mindmap"
    NativeDialogs.FileDialog
    {
        id: canvasSaveDialog
        title: root.canvasSaveOwner === "pdf" ? "保存 PDF 副本" : "保存思维导图"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: root.canvasSaveOwner === "pdf" ? "pdf" : "mfg"
        nameFilters: root.canvasSaveOwner === "pdf" ? ["PDF 文档 (*.pdf)"] : ["Mirrorfly 思维导图 (*.mfg)"]
        onAccepted: (root.canvasSaveOwner === "pdf" ? pdfEditor : mindmapEditor).selectSaveFile(selectedFile)
        onRejected: (root.canvasSaveOwner === "pdf" ? pdfEditor : mindmapEditor).cancelSaveDialog()
    }

    NativeDialogs.FileDialog
    {
        id: wordSaveDialog
        title: "保存 Word 文档"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "docx"
        nameFilters: ["Word 文档 (*.docx)"]
        onAccepted: wordEditor.selectSaveFile(selectedFile)
        onRejected: wordEditor.cancelSaveDialog()
    }

    NativeDialogs.FileDialog
    {
        id: imageDialog

        title: root.replacingPresentationImage ? "替换图片" : "插入图片"
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"]
        onAccepted:
        {
            if (root.replacingPresentationImage)
            {
                presentation.replaceImage(selectedFile);
            }
            else
            {
                presentation.addImage(selectedFile);
            }
        }
    }

    NativeDialogs.FileDialog
    {
        id: saveDialog

        title: "保存文档"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: textEditor.markdown ? "md" : "txt"
        nameFilters: ["文本与 Markdown (*.txt *.text *.md *.markdown)"]
        onAccepted: textEditor.selectSaveFile(selectedFile)
        onRejected: textEditor.cancelSaveDialog()
    }

    RoundedDialog
    {
        id: unsavedDialog
        theme: root.theme
        overlayRadius: windowFrame.cornerRadius

        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(500, root.width - 80)
        title: "保存修改？"
        modal: true
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout
        {
            spacing: 24

            Text
            {
                Layout.fillWidth: true
                text: "「" + (root.unsavedOwner === "pdf" ? pdfEditor.documentName : root.unsavedOwner === "mindmap" ? mindmapEditor.documentName : root.unsavedOwner === "word" ? wordEditor.documentName : root.unsavedOwner === "sheets" ? spreadsheet.documentName
                    : (root.unsavedOwner === "slides" ? presentation.documentName : textEditor.documentName))
                    + "」有未保存的修改。"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize
                wrapMode: Text.Wrap
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 10

                ActionButton
                {
                    theme: root.theme
                    text: "保存后继续"
                    iconName: "check"
                    primary: true
                    onClicked:
                    {
                        unsavedDialog.close();
                        root.resolveUnsaved("save");
                    }
                }

                ActionButton
                {
                    theme: root.theme
                    text: "不保存"
                    iconName: "arrow"
                    onClicked:
                    {
                        unsavedDialog.close();
                        root.resolveUnsaved("discard");
                    }
                }

                Item
                {
                    Layout.fillWidth: true
                }

                ActionButton
                {
                    theme: root.theme
                    text: "取消"
                    iconName: "close"
                    onClicked:
                    {
                        unsavedDialog.close();
                        root.resolveUnsaved("cancel");
                    }
                }
            }
        }
    }

    WordSessionConnections
    {
        host: root
        wordBridge: wordEditor
        page: wordPage
        intro: featureIntro
        savePrompt: wordSaveDialog
        unsavedPrompt: unsavedDialog
    }

    TextSessionConnections
    {
        host: root
        textBridge: textEditor
        tools: editorTools
        page: editorPage
        intro: featureIntro
        savePrompt: saveDialog
        unsavedPrompt: unsavedDialog
    }


    Connections
    {
        target: appBridge

        function onQuitRequested()
        {
            root.exitPending = true;
            appBridge.restoreMainWindow();
            aiAgent.cancel();
            Qt.callLater(function() { root.close(); });
        }

        function onFileDialogRequested()
        {
            fileDialog.selectedNameFilter.index = 0;
            fileDialog.open();
        }

        function onPresentationDialogRequested()
        {
            fileDialog.selectedNameFilter.index = 1;
            fileDialog.open();
        }
    }

    PresentationSessionConnections
    {
        host: root
        presentationBridge: presentation
        intro: featureIntro
        savePrompt: presentationSaveDialog
        unsavedPrompt: unsavedDialog
    }

    SpreadsheetSessionConnections
    {
        host: root
        spreadsheetBridge: spreadsheet
        intro: featureIntro
        savePrompt: spreadsheetSaveDialog
        unsavedPrompt: unsavedDialog
    }

    CanvasSessionConnections
    {
        host: root
        pdfBridge: pdfEditor
        mindmapBridge: mindmapEditor
        application: appBridge
        intro: featureIntro
        savePrompt: canvasSaveDialog
        unsavedPrompt: unsavedDialog
    }


    Connections
    {
        target: aiIsland
        function onPromptRequested(prompt)
        {
            if (!root.visible) appBridge.restoreMainWindow();
            if (root.settleActiveInput()) aiAgent.start(prompt);
        }
    }

    Shortcut
    {
        sequence: "Ctrl+Shift+A"
        enabled: root.assistantAvailable
        onActivated: aiIsland.toggle()
    }

    Shortcut
    {
        sequence: "Ctrl+O"
        enabled: root.editorInteractionAllowed
        onActivated: { if (!root.settleActiveInput("open")) return; appBridge.chooseFile(); }
    }

    Shortcut
    {
        sequence: "Ctrl+K"
        enabled: root.editorInteractionAllowed && !textEditor.active && !presentation.active && !spreadsheet.active && !wordEditor.active && !root.canvasActive
        onActivated: homePage.focusSearch()
    }

    Shortcut
    {
        sequence: "Ctrl+N"
        enabled: root.editorInteractionAllowed
        onActivated: { if (!root.settleActiveInput("new")) return; appBridge.requestCreate(root.canvasActive ? root.canvasSession.kind : wordEditor.active ? "word" : spreadsheet.active ? "sheets" : (presentation.active ? "slides" : (textEditor.markdown ? "markdown" : "writer"))); }
    }

    Shortcut
    {
        sequence: "Ctrl+Shift+N"
        enabled: root.editorInteractionAllowed
        onActivated: { if (!root.settleActiveInput("markdown")) return; appBridge.requestCreate("markdown"); }
    }

    Shortcut
    {
        sequence: "Ctrl+S"
        enabled: root.editorInteractionAllowed && (root.canvasActive || (wordEditor.active && !wordEditor.readOnly) || spreadsheet.active || textEditor.active || (presentation.active && presentation.editable))
        onActivated: { if (!root.settleActiveInput()) return; root.canvasActive ? root.canvasSession.save() : wordEditor.active ? wordEditor.save() : spreadsheet.active ? spreadsheet.save() : (presentation.active ? presentation.save() : textEditor.save()); }
    }

    Shortcut
    {
        sequence: "Ctrl+Shift+S"
        enabled: root.editorInteractionAllowed && (root.canvasActive || (wordEditor.active && !wordEditor.readOnly) || spreadsheet.active || textEditor.active || (presentation.active && presentation.editable))
        onActivated: { if (!root.settleActiveInput()) return; root.canvasActive ? root.canvasSession.saveAs() : wordEditor.active ? wordEditor.saveAs() : spreadsheet.active ? spreadsheet.saveAs() : (presentation.active ? presentation.saveAs() : textEditor.saveAs()); }
    }

    Shortcut
    {
        sequence: "Ctrl+W"
        enabled: root.editorInteractionAllowed && (root.canvasActive || wordEditor.active || spreadsheet.active || textEditor.active || presentation.active)
        onActivated: { if (!root.settleActiveInput("home")) return; root.canvasActive ? root.canvasSession.requestHome() : wordEditor.active ? wordEditor.requestHome() : spreadsheet.active ? spreadsheet.requestHome() : (presentation.active ? presentation.showHome() : textEditor.requestHome()); }
    }

    Shortcut
    {
        sequence: "Escape"
        enabled: unsavedDialog.opened
        onActivated:
        {
            unsavedDialog.close();
            root.resolveUnsaved("cancel");
        }
    }

    Shortcut
    {
        sequence: "Ctrl+Z"
        enabled: root.editorInteractionAllowed && root.canvasActive && !canvasPage.editing && root.canvasSession.canUndo
        onActivated: root.canvasSession.undo()
    }
    Shortcut
    {
        sequence: "Ctrl+Y"
        enabled: root.editorInteractionAllowed && root.canvasActive && !canvasPage.editing && root.canvasSession.canRedo
        onActivated: root.canvasSession.redo()
    }
    Shortcut
    {
        sequence: "F11"
        enabled: root.editorInteractionAllowed && presentation.active
        onActivated: root.togglePresentationFullscreen()
    }

    Shortcut
    {
        sequence: "Ctrl+Z"
        enabled: root.editorInteractionAllowed && presentation.active && presentation.editable && presentation.canUndo
        onActivated: presentation.undo()
    }

    Shortcut
    {
        sequence: "Ctrl+Z"
        enabled: root.editorInteractionAllowed && spreadsheet.active && !spreadsheetPage.editing && spreadsheet.canUndo
        onActivated: spreadsheet.undo()
    }

    Shortcut
    {
        sequence: "Ctrl+Y"
        enabled: root.editorInteractionAllowed && spreadsheet.active && !spreadsheetPage.editing && spreadsheet.canRedo
        onActivated: spreadsheet.redo()
    }

    Shortcut
    {
        sequence: "Ctrl+Y"
        enabled: root.editorInteractionAllowed && presentation.active && presentation.editable && presentation.canRedo
        onActivated: presentation.redo()
    }

    Shortcut
    {
        sequence: "Escape"
        enabled: root.editorInteractionAllowed && !spreadsheetPage.editing
        onActivated:
        {
            if (root.presentationFullscreen)
            {
                root.togglePresentationFullscreen();
                return;
            }
            if (!root.settleActiveInput()) return;
            appBridge.clearNotice();
            textEditor.clearMessage();
            presentation.clearError();
            presentation.clearMessage();
        }
    }
}
