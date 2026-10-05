pragma ComponentBehavior: Bound

import QtQuick

import Mirrorfly.Native 1.0
import "components"

Window
{
    id: root
    objectName: "presentationAudienceWindow"

    required property var theme
    required property var document
    required property int currentSlide
    required property real slideWidth
    required property bool playing
    required property bool busy
    readonly property var playbackView: audienceSlide

    signal advanceRequested()
    signal previousRequested()
    signal slideClicked(real x, real y)
    signal exitRequested()

    visible: false
    color: theme.slidesCanvas
    flags: Qt.Window | Qt.FramelessWindowHint
    onClosing:
    {
        if (playing) exitRequested();
    }

    SlideView
    {
        id: audienceSlide
        objectName: "presentationAudienceSlide"
        readonly property real fitScale: Math.max(0.01, Math.min(
            root.width / Math.max(1, implicitWidth),
            root.height / Math.max(1, implicitHeight)))

        x: (root.width - width * fitScale) / 2
        y: (root.height - height * fitScale) / 2
        width: implicitWidth
        height: implicitHeight
        scale: fitScale
        transformOrigin: Item.TopLeft
        document: root.document
        theme: root.theme
        slideIndex: root.currentSlide
        selectedShape: -1
        mediaEnabled: root.visible && root.playing && !root.busy
        animationEnabled: root.visible && root.playing && !root.busy
        transitionsEnabled: root.visible && root.playing && !root.busy
            && root.theme.motionEnabled !== false
        deferredFrames: false

        PresentationMediaControls
        {
            anchors.fill: parent
            z: 2
            theme: root.theme
            items: audienceSlide.mediaItems
            states: audienceSlide.mediaStates
            sceneScale: audienceSlide.width / Math.max(1, root.slideWidth)
            fullscreen: true
            enabled: audienceSlide.mediaEnabled
            onCommandRequested: function(shape, action, value)
            {
                audienceSlide.mediaCommand(shape, action, value);
            }
        }

        MouseArea
        {
            anchors.fill: parent
            z: 1
            acceptedButtons: Qt.LeftButton
            onClicked: function(mouse) { root.slideClicked(mouse.x, mouse.y); }
        }
    }

    Item
    {
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event)
        {
            if (event.key === Qt.Key_Escape)
            {
                root.exitRequested();
                event.accepted = true;
            }
            else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_Left)
            {
                root.previousRequested();
                event.accepted = true;
            }
            else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_Right ||
                event.key === Qt.Key_Down || event.key === Qt.Key_Space)
            {
                root.advanceRequested();
                event.accepted = true;
            }
        }
    }
}
