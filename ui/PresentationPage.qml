pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Mirrorfly.Native 1.0
import "components"

FocusScope
{
    id: root
    objectName: "PresentationPage"

    property var templatePreviews: ({})
    property var chineseFontFamilies: []
    property var systemFontFamilies: []
    readonly property var templateOptions: templateCatalog.options
    PresentationTemplateCatalog { id: templateCatalog; theme: root.theme }
    property var theme
    property real chromeInset: 0
    property var document
    property var searchFunction
    property var paragraphInfoFunction
    property var guideSettings: ({})
    property var slideTransition: ({})
    property bool busy: false
    property bool imageExportBusy: false
    property bool syncing: false
    property int pendingEdits: 0
    property string error: ""
    property string message: ""
    property string documentName: ""
    property string documentPath: ""
    property int slideCount: 0
    property var hiddenSlides: []
    property var slideSections: []
    property bool sectionsEditable: false
    function sectionStartingAt(index)
    {
        for (const section of slideSections)
            if (section.firstSlide === index)
                return section;
        return null;
    }
    property int currentSlide: 0
    property real zoom: 0
    property string fontSummary: ""
    property bool fullscreen: false
    property bool presenterMode: false
    property bool dualScreenPresenter: false
    property bool secondaryScreenAvailable: false
    property var externalPlayback: null
    property string speakerNotes: ""
    readonly property int presenterElapsedSeconds: Math.floor(presenterPanel.elapsedSeconds)
    property int lastViewedSlide: -1
    property int observedSlide: -1
    property bool editable: true
    property bool themeEditable: false
    property var themeState: ({})
    property bool modified: false
    property bool canUndo: false
    property bool canRedo: false
    property int selectedShape: -1
    property var selection: ({})
    property string formatBrushSourceId: ""
    property real slideWidth: 0
    property real slideHeight: 0
    readonly property var playbackView: dualScreenPresenter && externalPlayback ? externalPlayback : fullSlide
    readonly property var automationMedia: playbackView.mediaStates
    readonly property var automationAnimation: playbackView.animationState
    readonly property var automationTransition: playbackView.transitionState
    function mediaCommand(shape, action, value) { return playbackView.mediaCommand(shape, action, value || 0); }
    function animationCommand(action, value)
    {
        if (action === "advance") return playbackView.advanceAnimation();
        if (action === "trigger") return playbackView.triggerAnimation(Math.trunc(value));
        if (action === "restart") { playbackView.restartAnimation(); return true; }
        if (action === "seek") return playbackView.seekAnimation(value);
        return false;
    }
    function advancePresentation()
    {
        if (busy) return false;
        if (fullscreen && playbackView.transitionState.active)
        {
            playbackView.seekTransition(1);
            return true;
        }
        if (fullscreen && playbackView.advanceAnimation()) return true;
        if (nextPage >= 0)
        {
            if (!fullscreen) finishTextEditing();
            slideRequested(nextPage);
            return true;
        }
        return false;
    }

    function followClickAction(x, y)
    {
        const navigation = playbackView.clickNavigationAt(x, y, lastViewedSlide);
        if (navigation.kind === "endShow")
        {
            fullscreenRequested();
            return true;
        }
        if (navigation.kind === "slide")
        {
            slideRequested(navigation.target);
            return true;
        }
        return false;
    }

    function scheduleAutoAdvance()
    {
        slideAdvanceTimer.stop();
        const state = playbackView.transitionState;
        if (!visible || !fullscreen || busy || nextPage < 0 || state.advanceAfterMs < 0) return;
        slideAdvanceTimer.interval = Math.max(1, state.advanceAfterMs, state.durationMs || 0);
        slideAdvanceTimer.start();
    }
    readonly property bool transforming: transformOverlay.dragging
    readonly property bool canvasEditing: canvasEditor.editing
    property bool canvasPanning: false
    property point panStart: Qt.point(0, 0)
    property real panContentX: 0
    property real panContentY: 0
    property var renderedTransformPreview: ({})

    readonly property bool modalActive: editableCopyDialog.opened || compatibilityDialog.opened || slideMenu.opened || objectMenu.opened
    readonly property int previousPage: adjacentPage(-1)
    readonly property int nextPage: adjacentPage(1)
    readonly property bool hasVisibleSlides: slideCount > 0 && hiddenSlides.filter(function(hidden) { return hidden; }).length < slideCount

    signal homeRequested()
    signal openRequested()
    signal slideRequested(int index)
    signal zoomRequested(real value)
    signal fullscreenRequested()
    signal dismissError()
    signal imageExportRequested()
    signal dualPresenterRequested()

    function adjacentPage(direction)
    {
        let index = currentSlide + direction;
        while (index >= 0 && index < slideCount)
        {
            if (!fullscreen || !hiddenSlides[index]) return index;
            index += direction;
        }
        return -1;
    }

    function openSlideMenu(index, source, x, y)
    {
        if (busy || index < 0 || index >= slideCount) return;
        finishTextEditing();
        slideRequested(index);
        slideMenu.targetSlide = index;
        const position = source.mapToItem(root, x, y);
        slideMenu.popup(root, position.x, position.y);
    }
    signal newRequested()
    signal saveRequested()
    signal pdfRequested()
    signal saveAsRequested()
    signal undoRequested()
    signal redoRequested()
    signal editableCopyRequested()
    signal shapeRequested(int index)
    signal editRequested(string action, var options)
    signal guideSettingsRequested(var patch)
    signal imageRequested()
    signal imageReplaceRequested()
    signal firstFrameReady()

    function restoreSelection()
    {
        presentationTools.restoreSelection();
        canvasEditor.restoreText();
    }

    function focusCanvas()
    {
        if (visible && enabled && !busy)
        {
            canvasFocus.forceActiveFocus();
        }
    }

    function finishTextEditing()
    {
        canvasEditor.finish();
        transformOverlay.cancel();
        endCanvasPan();
    }

    function clampCanvasX(value)
    {
        return Math.max(0, Math.min(value, Math.max(0, slideViewport.contentWidth - slideViewport.width)));
    }

    function clampCanvasY(value)
    {
        return Math.max(0, Math.min(value, Math.max(0, slideViewport.contentHeight - slideViewport.height)));
    }

    function beginCanvasPan(surface, x, y)
    {
        if (busy || fullscreen || (slideViewport.contentWidth <= slideViewport.width
            && slideViewport.contentHeight <= slideViewport.height)) return false;
        const point = surface.mapToItem(slideViewport, x, y);
        panStart = Qt.point(point.x, point.y);
        panContentX = slideViewport.contentX;
        panContentY = slideViewport.contentY;
        canvasPanning = true;
        transformOverlay.cancel();
        return true;
    }

    function updateCanvasPan(surface, x, y)
    {
        if (!canvasPanning) return;
        const point = surface.mapToItem(slideViewport, x, y);
        slideViewport.contentX = clampCanvasX(panContentX - (point.x - panStart.x));
        slideViewport.contentY = clampCanvasY(panContentY - (point.y - panStart.y));
    }

    function endCanvasPan()
    {
        canvasPanning = false;
    }

    function requestCanvasZoom(value, viewportX, viewportY)
    {
        if (busy || fullscreen) return;
        const oldWidth = Math.max(1, fullSlide.displayWidth);
        const oldHeight = Math.max(1, fullSlide.displayHeight);
        const anchorX = (slideViewport.contentX + viewportX - fullSlide.x) / oldWidth;
        const anchorY = (slideViewport.contentY + viewportY - fullSlide.y) / oldHeight;
        zoomRequested(value);
        Qt.callLater(function()
        {
            if (value === 0)
            {
                slideViewport.contentX = 0;
                slideViewport.contentY = 0;
                return;
            }
            slideViewport.contentX = clampCanvasX(fullSlide.x + anchorX * fullSlide.displayWidth - viewportX);
            slideViewport.contentY = clampCanvasY(fullSlide.y + anchorY * fullSlide.displayHeight - viewportY);
        });
    }

    function nudgeSelection(dx, dy)
    {
        if (!editable || !supportsSelection("transformShape") || busy || fullscreen || canvasEditing || !selection.valid || transformOverlay.dragging) return false;
        editRequested("transformShape", {x: selection.x + dx, y: selection.y + dy});
        return true;
    }

    function beginTextEditing()
    {
        if (root.editable && root.supportsSelection("updateText") && !root.busy && !root.fullscreen)
        {
            canvasEditor.begin();
        }
    }

    onCurrentSlideChanged:
    {
        if (fullscreen && observedSlide >= 0 && observedSlide !== currentSlide)
            lastViewedSlide = observedSlide;
        observedSlide = currentSlide;
        canvasEditor.finish();
        transformOverlay.cancel();
        Qt.callLater(scheduleAutoAdvance);
    }

    onDocumentChanged:
    {
        formatBrushSourceId = "";
        if (presentationTools)
            presentationTools.refreshSearch();
    }

    function supportsSelection(action)
    {
        return selection.valid === true && selection.editable !== false
            && (!selection.actions || selection.actions.indexOf(action) >= 0);
    }
    onBusyChanged: Qt.callLater(scheduleAutoAdvance)
    onDualScreenPresenterChanged: Qt.callLater(scheduleAutoAdvance)
    onExternalPlaybackChanged: Qt.callLater(scheduleAutoAdvance)
    onZoomChanged:
    {
        if (zoom === 0)
        {
            Qt.callLater(function()
            {
                slideViewport.contentX = 0;
                slideViewport.contentY = 0;
            });
        }
    }
    onFullscreenChanged:
    {
        if (!fullscreen)
        {
            presenterMode = false;
            dualScreenPresenter = false;
        }
        transformOverlay.cancel();
        endCanvasPan();
        if (fullscreen)
        {
            observedSlide = currentSlide;
            lastViewedSlide = -1;
        }
        if (fullscreen && hiddenSlides[currentSlide])
        {
            for (let index = 0; index < slideCount; ++index)
            {
                if (!hiddenSlides[index])
                {
                    slideRequested(index);
                    break;
                }
            }
        }
        Qt.callLater(scheduleAutoAdvance);
    }
    onSelectedShapeChanged: canvasEditor.finish()
    onVisibleChanged:
    {
        if (visible)
        {
            Qt.callLater(focusCanvas);
        }
        else
        {
            slideAdvanceTimer.stop();
            canvasEditor.finish();
            presentationTools.activeGroup = "";
        }
    }

    Timer
    {
        id: slideAdvanceTimer

        repeat: false
        onTriggered:
        {
            if (root.fullscreen && root.nextPage >= 0) root.slideRequested(root.nextPage);
        }
    }

    Timer
    {
        id: transformPreviewTimer

        interval: 7
        repeat: false
        onTriggered: root.renderedTransformPreview = transformOverlay.dragging
            ? Object.assign({}, transformOverlay.preview) : ({})
    }

    Rectangle
    {
        anchors.fill: parent
        color: root.theme.backgroundColor
    }

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: root.fullscreen ? 15 : 24
        anchors.topMargin: root.fullscreen ? 15 : 24 + root.chromeInset
        spacing: 12

        RowLayout
        {
            Layout.fillWidth: true
            visible: !root.fullscreen
            spacing: 12

            Rectangle
            {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 42
                radius: root.theme.radius * 0.72
                color: root.theme.slidesSurface

                UiIcon
                {
                    anchors.centerIn: parent
                    width: 25
                    height: 25
                    symbol: "slides"
                    iconColor: root.theme.slidesAccent
                }
            }

            ColumnLayout
            {
                Layout.fillWidth: true
                spacing: 3

                Text
                {
                    Layout.fillWidth: true
                    text: root.documentName + (root.modified ? " *" : "")
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize + 5
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Text
                {
                    Layout.fillWidth: true
                    text: root.documentPath
                    color: root.theme.textSecondary
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.fontSize - 3
                    elide: Text.ElideMiddle
                }
            }

            Text
            {
                text: root.busy ? "正在处理…"
                    : root.syncing ? "正在同步 " + root.pendingEdits + " 项编辑…"
                    : (root.editable ? "基础编辑" : "只读预览")
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            visible: !root.fullscreen
            spacing: 7
            enabled: !root.busy

            ActionButton
            {
                theme: root.theme
                text: "返回首页"
                iconName: "home"
                compact: true
                onClicked: root.homeRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "打开"
                iconName: "folder"
                compact: true
                onClicked: root.openRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "新建"
                iconName: "plus"
                compact: true
                onClicked: root.newRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "保存"
                iconName: "check"
                compact: true
                primary: root.modified
                enabled: root.editable && (root.modified || root.documentPath.length === 0)
                onClicked: root.saveRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "另存为"
                iconName: "check"
                compact: true
                enabled: root.editable
                onClicked: root.saveAsRequested()
            }
            ActionButton { theme: root.theme; text: "导出 PDF"; compact: true; enabled: !root.busy; onClicked: root.pdfRequested() }
            ActionButton { objectName: "presentationExportImagesTopAction"; theme: root.theme; text: "导出图片"; compact: true; enabled: !root.busy && !root.imageExportBusy; onClicked: root.imageExportRequested() }

            ActionButton
            {
                theme: root.theme
                text: "撤销"
                iconName: "arrow"
                compact: true
                enabled: root.editable && root.canUndo
                onClicked: root.undoRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "重做"
                iconName: "arrow"
                compact: true
                enabled: root.editable && root.canRedo
                onClicked: root.redoRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "创建可编辑副本"
                iconName: "plus"
                compact: true
                visible: !root.editable
                onClicked: editableCopyDialog.open()
            }

            Item
            {
                Layout.fillWidth: true
            }

            Text
            {
                visible: root.slideWidth > 0 && root.slideHeight > 0
                text: Math.round(root.slideWidth) + " × " + Math.round(root.slideHeight) + " pt"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 3
            }

            ActionButton
            {
                theme: root.theme
                text: "放映"
                enabled: root.hasVisibleSlides
                iconName: "play"
                compact: true
                primary: true
                onClicked: root.fullscreenRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "演讲排练"
                enabled: root.hasVisibleSlides
                iconName: "play"
                compact: true
                onClicked:
                {
                    root.presenterMode = true;
                    root.fullscreenRequested();
                }
            }

            ActionButton
            {
                theme: root.theme
                text: "双屏演讲"
                iconName: "play"
                compact: true
                visible: root.secondaryScreenAvailable
                enabled: root.hasVisibleSlides
                onClicked: root.dualPresenterRequested()
            }
        }

        PresentationTools
        {
            id: presentationTools
            templatePreviews: root.templatePreviews
            templateOptions: root.templateOptions
            chineseFontFamilies: root.chineseFontFamilies
            systemFontFamilies: root.systemFontFamilies
            searchFunction: root.searchFunction
            paragraphInfoFunction: root.paragraphInfoFunction
            guideSettings: root.guideSettings
            slideTransition: root.slideTransition
            slideWidth: root.slideWidth
            slideHeight: root.slideHeight
            syncing: root.syncing

            objectName: "presentationToolsPanel"
            Layout.fillWidth: true
            visible: !root.fullscreen && root.editable
            theme: root.theme
            editable: root.editable
            themeEditable: root.themeEditable
            themeState: root.themeState
            actionsEnabled: !root.busy && !root.syncing
            exportBusy: root.imageExportBusy
            slideCount: root.slideCount
            currentSlide: root.currentSlide
            slideSections: root.slideSections
            sectionsEditable: root.sectionsEditable
            selectedShape: root.selectedShape
            selection: root.selection
            formatBrushSourceId: root.formatBrushSourceId
            currentSlideHidden: Boolean(root.hiddenSlides[root.currentSlide])
            onEditRequested: function(action, options)
            {
                root.finishTextEditing();
                root.editRequested(action, options);
            }
            onFormatBrushArmed: function(sourceId) { root.formatBrushSourceId = sourceId; }
            onFormatBrushApplyRequested:
            {
                root.editRequested("applyFormat", {sourceId: root.formatBrushSourceId});
                root.formatBrushSourceId = "";
            }
            onFormatBrushCancelled: root.formatBrushSourceId = ""
            onGuideSettingsRequested: function(patch) { root.guideSettingsRequested(patch); }
            onImageExportRequested: root.imageExportRequested()
            onPdfRequested: root.pdfRequested()
            onSearchNavigateRequested: function(slideIndex, shapeIndex)
            {
                root.slideRequested(slideIndex);
                root.shapeRequested(shapeIndex);
            }
            onCanvasTextRequested: root.beginTextEditing()
            onImageRequested: root.imageRequested()
            onImageReplaceRequested: root.imageReplaceRequested()
        }

        RowLayout
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Rectangle
            {
                objectName: "presentationThumbnails"
                Layout.preferredWidth: 148
                Layout.fillHeight: true
                visible: !root.fullscreen
                radius: root.theme.radius
                color: root.theme.surfaceColor
                border.width: 1
                border.color: root.theme.borderColor

                ListView
                {
                    id: thumbnails

                    anchors.fill: parent
                    anchors.margins: 11
                    model: root.slideCount
                    currentIndex: root.currentSlide
                    spacing: 10
                    clip: true
                    cacheBuffer: 0
                    reuseItems: true
                    boundsBehavior: Flickable.StopAtBounds
                    enabled: !root.busy
                    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

                    ScrollBar.vertical: ScrollBar
                    {
                        id: thumbnailScrollbar

                        width: 6
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle
                        {
                            implicitWidth: 4
                            radius: 2
                            color: thumbnailScrollbar.pressed ? root.theme.accent : root.theme.mutedColor
                        }
                        background: Item
                        {
                        }
                    }

                    delegate: AbstractButton
                    {
                        id: thumbnailButton

                        required property int index
                        readonly property var section: root.sectionStartingAt(index)
                        readonly property int sectionHeaderHeight: section ? 24 : 0

                        width: thumbnails.width - 7
                        height: thumbnailSlide.height + 30 + sectionHeaderHeight
                        hoverEnabled: true
                        activeFocusOnTab: true
                        Accessible.name: (section ? "节 " + section.name + "，" : "")
                            + "第 " + (index + 1) + " 页"
                        Keys.onPressed: function(event)
                        {
                            if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier))
                            {
                                root.openSlideMenu(index, thumbnailButton, width / 2, height / 2);
                                event.accepted = true;
                            }
                        }
                        onClicked:
                        {
                            root.finishTextEditing();
                            root.slideRequested(index);
                            root.focusCanvas();
                        }

                        HoverHandler
                        {
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler
                        {
                            acceptedButtons: Qt.RightButton
                            onTapped: function(eventPoint)
                            {
                                root.openSlideMenu(thumbnailButton.index, thumbnailButton, eventPoint.position.x, eventPoint.position.y);
                            }
                        }

                        background: Rectangle
                        {
                            radius: root.theme.radius * 0.45
                            color: root.currentSlide === thumbnailButton.index
                                ? root.theme.accentSoft
                                : (thumbnailButton.hovered ? root.theme.hoverColor : root.theme.transparentColor)
                            border.width: root.currentSlide === thumbnailButton.index || thumbnailButton.activeFocus ? 2 : 1
                            border.color: root.currentSlide === thumbnailButton.index || thumbnailButton.activeFocus
                                ? root.theme.accent : root.theme.borderColor
                        }

                        SlideThumbnail
                        {
                            id: thumbnailSlide

                            x: 5
                            y: 5 + thumbnailButton.sectionHeaderHeight
                            width: thumbnailButton.width - 10
                            height: thumbnailSlide.width * thumbnailSlide.implicitHeight
                                / Math.max(1, thumbnailSlide.implicitWidth)
                            document: root.document
                            theme: root.theme
                            slideIndex: thumbnailButton.index
                            opacity: root.hiddenSlides[thumbnailButton.index] ? 0.5 : 1
                        }

                        Text
                        {
                            objectName: "presentationSectionHeader"
                            x: 7
                            y: 5
                            width: parent.width - 14
                            visible: thumbnailButton.section !== null
                            text: thumbnailButton.section ? thumbnailButton.section.name : ""
                            color: root.theme.accent
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.fontSize - 2
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text
                        {
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 6
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: (thumbnailButton.index + 1) + (root.hiddenSlides[thumbnailButton.index] ? " · 已隐藏" : "")
                            color: root.currentSlide === thumbnailButton.index
                                ? root.theme.accent : root.theme.textSecondary
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.fontSize - 3
                        }
                    }
                }
            }

            FocusScope
            {
                id: canvasFocus

                Layout.fillWidth: true
                Layout.fillHeight: true
                focus: !root.fullscreen

                Keys.onPressed: function(event)
                {
                    if (root.busy)
                    {
                        return;
                    }
                    if (event.key === Qt.Key_Escape && transformOverlay.dragging)
                    {
                        transformOverlay.cancel();
                        event.accepted = true;
                    }
                    else if (!root.fullscreen && !root.canvasEditing &&
                        (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up || event.key === Qt.Key_Down))
                    {
                        const step = event.modifiers & Qt.ShiftModifier ? 10 : 1;
                        const dx = event.key === Qt.Key_Left ? -step : (event.key === Qt.Key_Right ? step : 0);
                        const dy = event.key === Qt.Key_Up ? -step : (event.key === Qt.Key_Down ? step : 0);
                        event.accepted = root.nudgeSelection(dx, dy);
                    }
                    else if (event.key === Qt.Key_PageUp && root.previousPage >= 0)
                    {
                        root.slideRequested(root.previousPage);
                        event.accepted = true;
                    }
                    else if (event.key === Qt.Key_PageDown)
                    {
                        event.accepted = root.advancePresentation();
                    }
                    else if (root.fullscreen && event.key === Qt.Key_Left && root.previousPage >= 0)
                    {
                        root.slideRequested(root.previousPage);
                        event.accepted = true;
                    }
                    else if (root.fullscreen && (event.key === Qt.Key_Right || event.key === Qt.Key_Space || event.key === Qt.Key_Down))
                    {
                        event.accepted = root.advancePresentation();
                    }
                }

                Rectangle
                {
                    objectName: "presentationCanvasViewport"
                    anchors.fill: parent
                    radius: root.theme.radius
                    color: root.theme.slidesCanvas
                    border.width: 1
                    border.color: root.theme.borderColor

                    Flickable
                    {
                        id: slideViewport
                        objectName: "presentationSlideViewport"

                        anchors.fill: parent
                        anchors.margins: 2
                        interactive: !root.fullscreen && !transformOverlay.dragging && !root.canvasPanning
                        contentWidth: Math.max(width, fullSlide.displayWidth + 48)
                        contentHeight: Math.max(height, fullSlide.displayHeight + 48)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        enabled: !root.busy

                        SlideView
                        {
                            id: fullSlide
                            objectName: "presentationFullSlide"

                            readonly property real fitScale: Math.max(0.01, Math.min(
                                (slideViewport.width - 48) / Math.max(1, fullSlide.implicitWidth),
                                (slideViewport.height - 48) / Math.max(1, fullSlide.implicitHeight)))
                            readonly property real viewScale: root.fullscreen ? fitScale
                                : (root.zoom > 0 ? root.zoom : fitScale)
                            readonly property real displayWidth: width * viewScale
                            readonly property real displayHeight: height * viewScale

                            x: (slideViewport.contentWidth - displayWidth) / 2
                            y: (slideViewport.contentHeight - displayHeight) / 2
                            width: fullSlide.implicitWidth
                            height: fullSlide.implicitHeight
                            scale: viewScale
                            transformOrigin: Item.TopLeft
                            visible: !root.dualScreenPresenter
                            document: root.document
                            theme: root.theme
                            slideIndex: root.currentSlide
                            mediaEnabled: root.visible && !root.dualScreenPresenter && !root.busy
                            animationEnabled: root.visible && root.fullscreen && !root.dualScreenPresenter && !root.busy
                            transitionsEnabled: root.visible && root.fullscreen && !root.dualScreenPresenter && !root.busy
                                && root.theme.motionEnabled !== false
                            deferredFrames: root.visible && !root.fullscreen && !root.dualScreenPresenter && !root.busy
                            selectedShape: root.fullscreen ? -1 : root.selectedShape
                            editingShape: canvasEditor.editing ? root.selectedShape : -1
                            transformPreview: root.renderedTransformPreview
                            clip: false
                            onFrameReady: root.firstFrameReady()

                            PresentationMediaControls
                            {
                                anchors.fill: parent
                                z: 4
                                theme: root.theme
                                items: fullSlide.mediaItems
                                states: fullSlide.mediaStates
                                sceneScale: fullSlide.width / Math.max(1, root.slideWidth)
                                fullscreen: root.fullscreen
                                enabled: fullSlide.mediaEnabled
                                onCommandRequested: function(shape, action, value) { fullSlide.mediaCommand(shape, action, value); }
                            }

                            PresentationTransformOverlay
                            {
                                id: transformOverlay
                                anchors.fill: parent
                                z: 3
                                theme: root.theme
                                selection: root.selection
                                sceneScale: fullSlide.width / Math.max(1, root.slideWidth)
                                viewScale: fullSlide.viewScale
                                slideWidthPt: root.slideWidth
                                slideHeightPt: root.slideHeight
                                guideSettings: root.guideSettings
                                enabled: root.editable && root.supportsSelection("transformShape") && !root.busy && !root.fullscreen && !canvasEditor.editing
                                onTransformCommitted: function(options) { root.editRequested("transformShape", options); }
                                onPreviewChanged:
                                {
                                    if (dragging)
                                    {
                                        if (!transformPreviewTimer.running) transformPreviewTimer.start();
                                    }
                                    else
                                    {
                                        transformPreviewTimer.stop();
                                        root.renderedTransformPreview = {};
                                    }
                                }
                                onDraggingChanged:
                                {
                                    if (!dragging)
                                    {
                                        transformPreviewTimer.stop();
                                        root.renderedTransformPreview = {};
                                    }
                                }
                            }

                            PresentationCanvasEditor
                            {
                                id: canvasEditor

                                z: 2
                                theme: root.theme
                                editorData: fullSlide.textEditorState
                                enabled: root.editable && root.selection.editable !== false && !root.busy && !root.fullscreen
                                onTextEdited: function(value) { root.editRequested("updateText", {text: value}); }
                                onUndoRequested: root.undoRequested()
                                onRedoRequested: root.redoRequested()
                                onInputCommitRequested: fullSlide.commitTextInput()
                            }
                        }

                        MouseArea
                        {
                            id: canvasMouseArea

                            parent: fullSlide
                            anchors.fill: parent
                            z: 1
                            enabled: !root.busy
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            hoverEnabled: true
                            preventStealing: transformOverlay.dragging
                            onDoubleClicked: { transformOverlay.cancel(); root.beginTextEditing(); }
                            onClicked: function(mouse)
                            {
                                if (root.fullscreen && mouse.button === Qt.LeftButton)
                                {
                                    if (fullSlide.transitionState.active) fullSlide.seekTransition(1);
                                    else if (root.followClickAction(mouse.x, mouse.y)) return;
                                    else if (fullSlide.triggerAnimationAt(mouse.x, mouse.y)) return;
                                    else if (fullSlide.transitionState.advanceOnClick !== false)
                                        root.advancePresentation();
                                }
                            }
                            onPressed: function(mouse)
                            {
                                canvasFocus.forceActiveFocus();
                                if (!root.fullscreen)
                                {
                                    root.finishTextEditing();
                                    const hit = fullSlide.hitTest(mouse.x, mouse.y);
                                    root.shapeRequested(hit);
                                    if (mouse.button === Qt.LeftButton && hit >= 0 &&
                                        root.formatBrushSourceId.length > 0)
                                    {
                                        if (root.selection.id !== root.formatBrushSourceId &&
                                            root.supportsSelection("applyFormat"))
                                            root.editRequested("applyFormat",
                                                {sourceId: root.formatBrushSourceId});
                                        root.formatBrushSourceId = "";
                                        return;
                                    }
                                    if (mouse.button === Qt.RightButton) objectMenu.popup();
                                    else if (hit >= 0) transformOverlay.beginGesture("move", mouse.x, mouse.y);
                                    else root.beginCanvasPan(canvasMouseArea, mouse.x, mouse.y);
                                }
                            }
                            onPositionChanged: function(mouse)
                            {
                                if (root.canvasPanning) root.updateCanvasPan(canvasMouseArea, mouse.x, mouse.y);
                                else transformOverlay.updateGesture(mouse.x, mouse.y, false);
                            }
                            onReleased:
                            {
                                if (root.canvasPanning) root.endCanvasPan();
                                else transformOverlay.endGesture();
                            }
                            onCanceled:
                            {
                                root.endCanvasPan();
                                transformOverlay.cancel();
                            }
                        }

                        MouseArea
                        {
                            id: canvasPanArea
                            objectName: "presentationCanvasPanArea"

                            anchors.fill: parent
                            z: 20
                            enabled: !root.busy && !root.fullscreen
                            acceptedButtons: Qt.MiddleButton
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                            onPressed: function(mouse)
                            {
                                mouse.accepted = root.beginCanvasPan(canvasPanArea, mouse.x, mouse.y);
                            }
                            onPositionChanged: function(mouse)
                            {
                                root.updateCanvasPan(canvasPanArea, mouse.x, mouse.y);
                            }
                            onReleased: root.endCanvasPan()
                            onCanceled: root.endCanvasPan()
                            onWheel: function(wheel)
                            {
                                if (!(wheel.modifiers & Qt.ControlModifier))
                                {
                                    wheel.accepted = false;
                                    return;
                                }
                                const current = root.zoom > 0 ? root.zoom : fullSlide.fitScale;
                                const direction = wheel.angleDelta.y >= 0 ? 1 : -1;
                                const next = Math.max(0.25, Math.min(3, current * Math.pow(1.15, direction)));
                                root.requestCanvasZoom(next, wheel.x, wheel.y);
                                wheel.accepted = true;
                            }
                        }

                        SlideThumbnail
                        {
                            objectName: "presentationPresenterCurrentSlide"
                            visible: root.dualScreenPresenter
                            width: Math.max(1, Math.min(slideViewport.width - 48,
                                (slideViewport.height - 48) * implicitWidth / Math.max(1, implicitHeight)))
                            height: width * implicitHeight / Math.max(1, implicitWidth)
                            x: (slideViewport.contentWidth - width) / 2
                            y: (slideViewport.contentHeight - height) / 2
                            document: root.document
                            theme: root.theme
                            slideIndex: root.currentSlide
                        }

                        ScrollBar.vertical: ScrollBar
                        {
                            id: slideVerticalScrollbar

                            width: 8
                            policy: ScrollBar.AsNeeded
                            contentItem: Rectangle
                            {
                                implicitWidth: 5
                                radius: 3
                                color: slideVerticalScrollbar.pressed ? root.theme.accent : root.theme.mutedColor
                            }
                            background: Item
                            {
                            }
                        }

                        ScrollBar.horizontal: ScrollBar
                        {
                            id: slideHorizontalScrollbar

                            height: 8
                            policy: ScrollBar.AsNeeded
                            contentItem: Rectangle
                            {
                                implicitHeight: 5
                                radius: 3
                                color: slideHorizontalScrollbar.pressed ? root.theme.accent : root.theme.mutedColor
                            }
                            background: Item
                            {
                            }
                        }
                    }

                    PresentationCanvasAids
                    {
                        objectName: "presentationCanvasAids"
                        anchors.fill: parent
                        z: 25
                        theme: root.theme
                        settings: root.guideSettings
                        slideX: fullSlide.x - slideViewport.contentX + 2
                        slideY: fullSlide.y - slideViewport.contentY + 2
                        displayWidth: fullSlide.displayWidth
                        displayHeight: fullSlide.displayHeight
                        slideWidthPt: root.slideWidth
                        slideHeightPt: root.slideHeight
                        visible: !root.fullscreen && !root.busy &&
                            (settings.showRulers === true || settings.showGrid === true ||
                                settings.showGuides === true)
                    }
                }
            }

            PresentationPresenterPanel
            {
                id: presenterPanel
                Layout.preferredWidth: root.theme.slidesPresenterPanelWidth
                Layout.fillHeight: true
                visible: root.fullscreen && root.presenterMode
                dualScreen: root.dualScreenPresenter
                theme: root.theme
                document: root.document
                speakerNotes: root.speakerNotes
                nextSlide: root.nextPage
                currentSlide: root.currentSlide
                slideCount: root.slideCount
                canPrevious: !root.busy && root.previousPage >= 0
                canNext: !root.busy && (root.nextPage >= 0 ||
                    root.automationAnimation.click < root.automationAnimation.clicks)
                onPreviousRequested: root.slideRequested(root.previousPage)
                onNextRequested: root.advancePresentation()
                onCloseRequested: root.fullscreenRequested()
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            visible: !root.fullscreen || !root.presenterMode
            spacing: 8

            Text
            {
                id: fontSummaryLabel

                Layout.fillWidth: true
                text: root.editable ? (root.selection.editable === false
                    ? "此对象保持锁定，保存时保留原始内容"
                    : root.selection.isTableCell ? "双击编辑单元格文字 · 格式在文字工具中修改 · 表格结构保持不变"
                    : "拖动对象 · 空白处或中键拖动画布 · Ctrl+滚轮缩放 · Ctrl+Z 撤销")
                    : "只读预览 · 编辑前创建副本"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 3
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            ActionButton
            {
                theme: root.theme
                text: "字体与兼容"
                iconName: ""
                compact: true
                onClicked: compatibilityDialog.open()
            }

            ActionButton
            {
                theme: root.theme
                text: "上一页"
                iconName: ""
                compact: true
                enabled: !root.busy && root.previousPage >= 0
                onClicked: root.slideRequested(root.previousPage)
            }

            Text
            {
                Layout.minimumWidth: 58
                text: (root.slideCount > 0 ? root.currentSlide + 1 : 0) + " / " + root.slideCount
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
                horizontalAlignment: Text.AlignHCenter
            }

            ActionButton
            {
                theme: root.theme
                text: "下一页"
                iconName: ""
                compact: true
                enabled: !root.busy && (root.nextPage >= 0 || (root.fullscreen && root.automationAnimation.click < root.automationAnimation.clicks))
                onClicked: root.advancePresentation()
            }

            ActionButton
            {
                theme: root.theme
                text: "−"
                iconName: ""
                compact: true
                enabled: !root.busy && (root.zoom === 0 || root.zoom > 0.25)
                onClicked: root.requestCanvasZoom(Math.max(0.25,
                    (root.zoom > 0 ? root.zoom : fullSlide.fitScale) - 0.25),
                    slideViewport.width / 2, slideViewport.height / 2)
            }

            Text
            {
                Layout.minimumWidth: 47
                text: Math.round((root.zoom > 0 ? root.zoom : fullSlide.fitScale) * 100) + "%"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
                horizontalAlignment: Text.AlignHCenter
            }

            ActionButton
            {
                theme: root.theme
                text: "+"
                iconName: ""
                compact: true
                enabled: !root.busy && root.zoom < 3
                onClicked: root.requestCanvasZoom(Math.min(3,
                    (root.zoom > 0 ? root.zoom : fullSlide.fitScale) + 0.25),
                    slideViewport.width / 2, slideViewport.height / 2)
            }

            ActionButton
            {
                theme: root.theme
                text: "适应窗口"
                iconName: ""
                compact: true
                primary: root.zoom === 0
                enabled: !root.busy
                onClicked: root.requestCanvasZoom(0, slideViewport.width / 2, slideViewport.height / 2)
            }

            ActionButton
            {
                theme: root.theme
                text: "退出全屏"
                iconName: "play"
                compact: true
                visible: root.fullscreen
                onClicked: root.fullscreenRequested()
            }
        }
    }

    Rectangle
    {
        id: noticeBanner

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.fullscreen ? 28 : 68
        width: Math.min(parent.width - 100, 680)
        height: errorText.implicitHeight + 28
        radius: root.theme.radius * 0.7
        color: root.theme.noticeBackground
        visible: displayMessage.length > 0

        property string displayMessage: root.error.length > 0 ? root.error : root.message

        RowLayout
        {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 16

            Text
            {
                id: errorText

                Layout.fillWidth: true
                text: noticeBanner.displayMessage
                color: root.theme.noticeForeground
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
            }

            AbstractButton
            {
                id: dismissButton

                implicitWidth: 25
                implicitHeight: 25
                enabled: !root.busy
                hoverEnabled: true
                activeFocusOnTab: true
                padding: 2
                Accessible.name: "关闭提示"
                onClicked: root.dismissError()

                contentItem: UiIcon
                {
                    symbol: "close"
                    iconColor: root.theme.noticeForeground
                }

                background: Rectangle
                {
                    color: root.theme.transparentColor
                    border.width: dismissButton.activeFocus || dismissButton.hovered ? 1 : 0
                    border.color: root.theme.noticeForeground
                    radius: 4
                }
            }
        }
    }

    RoundedDialog
    {
        id: compatibilityDialog

        theme: root.theme
        overlayRadius: root.theme.windowRadius
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(640, root.width - 80)
        title: "字体与兼容说明"
        modal: true
        standardButtons: Dialog.Close

        contentItem: ScrollView
        {
            implicitHeight: Math.min(300, summaryText.implicitHeight + 12)
            contentWidth: availableWidth
            clip: true

            Text
            {
                id: summaryText

                width: parent.width
                text: root.fontSummary || "使用此设备的系统字体。"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize
                wrapMode: Text.Wrap
            }
        }
    }

    PresentationSlideMenu
    {
        id: slideMenu

        objectName: "presentationSlideMenu"
        parent: root
        theme: root.theme
        slideCount: root.slideCount
        editable: root.editable
        busy: root.busy
        slideHidden: Boolean(root.hiddenSlides[targetSlide])
        onActionRequested: function(action, options)
        {
            if (!root.busy && targetSlide >= 0 && targetSlide < root.slideCount)
            {
                root.finishTextEditing();
                root.slideRequested(targetSlide);
                root.editRequested(action, options);
            }
        }
        onEditableCopyRequested: editableCopyDialog.open()
    }

    RoundedDialog
    {
        id: editableCopyDialog

        theme: root.theme
        overlayRadius: root.theme.windowRadius
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(560, root.width - 80)
        title: "创建可编辑副本"
        modal: true
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout
        {
            spacing: 18

            Text
            {
                Layout.fillWidth: true
                text: "创建新副本并保留原包中的媒体、动画、备注和复杂对象。已支持的页面对象、普通组合内部对象及表格单元格可按可用能力编辑；母版与未支持的结构保持保护。原文件不会被覆盖。"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize
                wrapMode: Text.Wrap
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 9

                ActionButton
                {
                    theme: root.theme
                    text: "继续"
                    iconName: "check"
                    primary: true
                    onClicked:
                    {
                        editableCopyDialog.close();
                        root.editableCopyRequested();
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
                    onClicked: editableCopyDialog.close()
                }
            }
        }
    }
    Menu
    {
        id: objectMenu
        MenuItem { text: "撤销"; enabled: root.editable && root.canUndo; onTriggered: root.undoRequested() }
        MenuItem { text: "重做"; enabled: root.editable && root.canRedo; onTriggered: root.redoRequested() }
        MenuSeparator {}
        MenuItem { text: "编辑文字"; enabled: root.editable && root.supportsSelection("updateText"); onTriggered: root.beginTextEditing() }
        MenuItem { text: "复制对象"; enabled: root.editable && root.supportsSelection("duplicateShape"); onTriggered: root.editRequested("duplicateShape", {}) }
        MenuItem { text: "上移一层"; enabled: root.editable && root.supportsSelection("moveShape"); onTriggered: root.editRequested("moveShape", {offset: 1}) }
        MenuItem { text: "下移一层"; enabled: root.editable && root.supportsSelection("moveShape"); onTriggered: root.editRequested("moveShape", {offset: -1}) }
        MenuItem { text: "删除对象"; enabled: root.editable && root.supportsSelection("deleteShape"); onTriggered: root.editRequested("deleteShape", {}) }
        MenuSeparator {}
        MenuItem { text: "插入图片…"; enabled: root.editable; onTriggered: root.imageRequested() }
        MenuItem { text: "导出 PDF…"; onTriggered: root.pdfRequested() }
    }

}
