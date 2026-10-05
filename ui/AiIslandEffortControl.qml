pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window

FocusScope
{
    id: root
    required property var theme
    property string effort: "none"
    property bool effortPending: false
    property real motionPhase: 0
    property real auroraPhase: 0
    property int draggedIndex: 0
    readonly property var values: ["none", "low", "high", "max"]
    readonly property var labels: ["关闭", "轻", "深", "极深"]
    readonly property int selectedIndex: Math.max(0, values.indexOf(effort))
    readonly property int previewIndex: Math.round(slider.value)
    property real displayPosition: slider.visualPosition
    Behavior on displayPosition
    {
        enabled: root.motionActive && !slider.pressed
        NumberAnimation { duration: root.theme.islandEffortSlideDuration; easing.type: Easing.OutCubic }
    }
    readonly property bool motionActive: visible && theme.motionEnabled
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized
    onPreviewIndexChanged: if (motionActive) stepPulse.restart()
    onSelectedIndexChanged: if (!slider.pressed) slider.value = selectedIndex
    onEffortPendingChanged: if (!effortPending && !slider.pressed) slider.value = selectedIndex
    Component.onCompleted: slider.value = selectedIndex
    onMotionActiveChanged:
    {
        if (!motionActive)
        {
            stepPulse.stop();
            motionPhase = 0;
        }
    }
    signal effortRequested(string value)
    implicitWidth: theme.islandEffortWidth
    implicitHeight: theme.islandEffortHeight
    Accessible.name: "AI 思考强度"
    function requestIndex(index)
    {
        slider.value = index;
        draggedIndex = index;
        effortRequested(values[index]);
    }
    Keys.onLeftPressed: requestIndex(Math.max(0, previewIndex - 1))
    Keys.onRightPressed: requestIndex(Math.min(3, previewIndex + 1))

    Text
    {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        text: root.labels[root.previewIndex]
        color: root.theme.islandMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 11
    }
    Slider
    {
        id: slider
        objectName: "effortSlider"
        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.right: parent.right
        height: parent.height
        from: 0
        to: 3
        stepSize: 1
        snapMode: Slider.SnapAlways
        live: true
        leftPadding: root.theme.islandEffortHeight / 2
        rightPadding: leftPadding
        Accessible.name: "思考强度，关闭、轻、深、极深"
        Keys.onLeftPressed: root.requestIndex(Math.max(0, root.previewIndex - 1))
        Keys.onRightPressed: root.requestIndex(Math.min(3, root.previewIndex + 1))
        onPressedChanged:
        {
            if (pressed) root.draggedIndex = Math.round(value);
            else root.effortRequested(root.values[root.draggedIndex]);
        }
        onMoved:
        {
            root.draggedIndex = Math.round(value);
            if (!pressed) root.effortRequested(root.values[root.draggedIndex]);
        }
        background: Rectangle
        {
            objectName: "effortTrack"
            x: 0
            y: (slider.height - height) / 2
            width: slider.width
            height: root.theme.islandEffortHeight
            radius: root.theme.islandEffortRadius
            color: root.theme.islandEffortTrack
            border.width: root.previewIndex === 3 ? 0 : 1
            border.color: root.theme.islandEffortOutline
            gradient: root.previewIndex === 3 ? rainbowGradient : null
            Gradient
            {
                id: rainbowGradient
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: root.theme.islandBlue }
                GradientStop { position: 0.25; color: root.theme.spectrumSage }
                GradientStop { position: 0.5; color: root.theme.spectrumGold }
                GradientStop { position: 0.75; color: root.theme.islandPink }
                GradientStop { position: 1; color: root.theme.islandPurple }
            }
            Rectangle
            {
                anchors.fill: parent
                anchors.margins: root.theme.islandEffortMaxBorderWidth
                radius: parent.radius - anchors.margins
                color: root.theme.islandEffortTrack
                visible: root.previewIndex === 3
            }
            Rectangle
            {
                x: 3
                y: 3
                width: Math.min(parent.width - 6,
                    slider.leftPadding + root.displayPosition * slider.availableWidth + 8)
                height: parent.height - 6
                radius: height / 2
                color: root.theme.islandEffortInk
                opacity: root.previewIndex === 3 ? 0.95 : 0.12 + root.previewIndex * 0.07
                gradient: root.previewIndex === 3 ? purpleGradient : null
                Gradient
                {
                    id: purpleGradient
                    orientation: Gradient.Horizontal
                    GradientStop
                    {
                        position: 0
                        color: Qt.lighter(root.theme.islandEffortMaxStart, 1 + root.theme.islandEffortGradientLift
                            * (0.5 + 0.5 * Math.sin(root.auroraPhase * Math.PI * 2)))
                    }
                    GradientStop
                    {
                        position: 0.5 + 0.2 * Math.sin(root.auroraPhase * Math.PI * 2)
                        color: root.theme.islandEffortMaxMiddle
                    }
                    GradientStop
                    {
                        position: 1
                        color: Qt.lighter(root.theme.islandEffortMaxEnd, 1 + root.theme.islandEffortGradientLift
                            * (0.5 - 0.5 * Math.sin(root.auroraPhase * Math.PI * 2)))
                    }
                }
            }
            Item
            {
                id: particleField
                objectName: "effortParticles"
                anchors.fill: parent
                clip: true
                visible: root.previewIndex > 0 && root.motionActive
                Repeater
                {
                    model: root.previewIndex === 3 ? root.theme.islandEffortMaxParticles
                        : root.previewIndex === 2 ? root.theme.islandEffortHighParticles
                        : root.theme.islandEffortLowParticles
                    Item
                    {
                        id: particle
                        objectName: "effortParticle"
                        required property int index
                        property real phase: 0
                        readonly property bool animating: particleAnimation.running
                        readonly property real age: (phase + index * 0.618034) % 1
                        x: slider.leftPadding + root.displayPosition * slider.availableWidth
                            - age * root.theme.islandEffortTrailLength
                            * root.previewIndex / 3
                        y: particleField.height / 2 + ((index % 5) - 2)
                            * root.theme.islandEffortLaneSpacing
                        opacity: (1 - age) * Math.sin(age * Math.PI)
                            * root.theme.islandEffortParticleOpacity
                        Rectangle
                        {
                            width: root.theme.islandEffortParticleSize
                            height: width
                            radius: width / 2
                            color: root.theme.islandEffortParticleColor
                        }
                        Rectangle
                        {
                            anchors.left: parent.left
                            y: root.theme.islandEffortParticleSize / 4
                            width: root.theme.islandEffortParticleTail * root.previewIndex / 3
                            height: root.theme.islandEffortParticleSize / 2
                            radius: height / 2
                            gradient: Gradient
                            {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: root.theme.islandEffortParticleColor }
                                GradientStop { position: 1; color: "transparent" }
                            }
                            opacity: 0.45 * (1 - particle.age)
                        }
                        NumberAnimation
                        {
                            id: particleAnimation
                            objectName: "effortParticleAnimation"
                            target: particle
                            property: "phase"
                            running: root.motionActive && root.previewIndex > 0
                            from: 0
                            to: 1
                            loops: Animation.Infinite
                            duration: (root.previewIndex === 3 ? root.theme.islandEffortMaxPeriod
                                : root.previewIndex === 2 ? root.theme.islandEffortHighPeriod
                                : root.theme.islandEffortLowPeriod) + particle.index * 17
                        }
                    }
                }
            }
            Repeater
            {
                model: 4
                Rectangle
                {
                    required property int index
                    x: slider.leftPadding + index * slider.availableWidth / 3 - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: 3
                    radius: 1.5
                    color: root.theme.islandEffortInk
                    opacity: 0.65
                }
            }
            Rectangle
            {
                anchors.fill: parent
                radius: root.theme.islandEffortRadius
                color: "transparent"
                border.width: 1 + root.previewIndex * 0.35
                border.color: root.theme.islandEffortInk
                opacity: root.motionActive ? Math.sin(root.motionPhase * Math.PI)
                    * (0.18 + root.previewIndex * 0.16) : 0
            }
        }
        handle: Rectangle
        {
            objectName: "effortThumb"
            x: slider.leftPadding + root.displayPosition * slider.availableWidth - width / 2
            y: (slider.height - height) / 2
            width: root.theme.islandEffortThumbSize
            height: width
            radius: width / 2
            color: root.theme.islandText
            border.width: slider.visualFocus ? 2 : 1
            border.color: root.theme.islandEffortOutline
            scale: 1 + (root.motionActive ? Math.sin(root.motionPhase * Math.PI)
                * (0.025 + root.previewIndex * 0.025) : 0)
        }
    }
    NumberAnimation
    {
        target: root
        property: "auroraPhase"
        running: root.motionActive && root.previewIndex === 3
        from: 0
        to: 1
        duration: root.theme.islandEffortGradientPeriod
        loops: Animation.Infinite
    }
    NumberAnimation
    {
        id: stepPulse
        target: root
        property: "motionPhase"
        from: 0
        to: 1
        duration: root.theme.islandEffortStepDuration + root.previewIndex * root.theme.islandEffortStepIncrement
        easing.type: Easing.OutCubic
    }
}
