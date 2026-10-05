import QtQuick
import QtQuick.Window

FocusScope
{
    id: root

    property var theme
    property string kind: "slides"
    property bool running: false
    property bool contentReady: true
    property bool elapsed: false
    property real progress: 0
    property bool loadingMode: false
    property bool loadingComplete: true
    property real loadingProgress: 0
    property string loadingStatus: ""
    readonly property int duration: Math.max(600, Math.min(5000, theme.featureIntroDuration))
    readonly property bool exposed: Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized && Window.window.visibility !== Window.Hidden
    readonly property bool motionActive: running && theme.motionEnabled && exposed
    readonly property real arrival: motionActive
        ? Math.min(1, progress * duration / theme.featureLoadingArrivalDuration) : 1

    signal finished()

    visible: running
    enabled: running
    focus: running

    function play(feature)
    {
        running = false;
        kind = feature;
        loadingMode = false;
        loadingComplete = true;
        elapsed = false;
        progress = 0;
        running = true;
        hold.restart();
    }

    function beginLoading(feature)
    {
        play(feature);
        loadingMode = true;
        loadingComplete = false;
    }

    function finishLoading()
    {
        if (!loadingMode) return;
        loadingComplete = true;
        completeIfReady();
    }

    function cancelLoading()
    {
        hold.stop();
        running = false;
        loadingMode = false;
        loadingComplete = true;
        elapsed = false;
        progress = 0;
    }

    function completeIfReady()
    {
        if (running && elapsed && contentReady && (!loadingMode || loadingComplete))
        {
            running = false;
            finished();
        }
    }

    onContentReadyChanged: completeIfReady()
    Keys.onPressed: function(event) { event.accepted = true; }
    Keys.onReleased: function(event) { event.accepted = true; }

    Timer
    {
        id: hold

        interval: root.duration
        onTriggered:
        {
            root.elapsed = true;
            root.completeIfReady();
        }
    }

    NumberAnimation
    {
        target: root
        property: "progress"
        running: root.motionActive
        from: 0
        to: 1
        duration: root.duration
        easing.type: Easing.Linear
    }

    MouseArea
    {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true; }
    }

    FeatureLoadingCard
    {
        objectName: "featureIntroCard"
        anchors.centerIn: parent
        width: Math.min(root.theme.featureLoadingWidth, parent.width - 64)
        height: root.theme.featureLoadingHeight
        opacity: root.arrival
        scale: 0.96 + 0.04 * (1 - Math.pow(1 - root.arrival, 3))
        theme: root.theme
        kind: root.kind
        loading: root.loadingMode
        actualProgress: root.loadingProgress
        status: root.loadingStatus
        animating: root.motionActive
    }
}
