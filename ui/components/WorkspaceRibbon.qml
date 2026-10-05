import QtQuick
import QtQuick.Window

Item
{
    id: root
    required property var theme
    property bool cycling: true
    property int design: 0
    property int cycleIndex: 0
    property real reveal: 1
    readonly property bool motionActive: cycling && visible && theme.motionEnabled
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized
    readonly property var designs: ["飘带", "蝴蝶结", "中国结", "环结", "折带"]
    readonly property var sequence: [0, 1, 0, 2, 0, 3, 0, 4]
    Accessible.name: designs[design]
    onMotionActiveChanged:
    {
        if (!motionActive)
        {
            design = 0;
            cycleIndex = 0;
            reveal = 1;
        }
    }
    Item
    {
        anchors.fill: parent
        opacity: root.reveal * root.theme.workspaceRibbonOpacity
        Canvas
        {
            id: tails
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections
            {
                target: root
                function onDesignChanged() { tails.requestPaint(); }
                function onThemeChanged() { tails.requestPaint(); }
            }
            onPaint:
            {
                if (width <= 0 || height <= 0) return;
                const c = getContext("2d");
                c.reset();
                const gradient = c.createLinearGradient(0, 0, width, height);
                const colors = [root.theme.spectrumBlue, root.theme.spectrumSage,
                    root.theme.spectrumGold, root.theme.spectrumCoral, root.theme.spectrumViolet];
                for (let i = 0; i < colors.length; ++i)
                    gradient.addColorStop(i / (colors.length - 1), colors[i]);
                c.strokeStyle = gradient;
                c.lineCap = "round";
                c.lineWidth = root.theme.workspaceRibbonWidth;
                c.beginPath();
                if (root.design === 0)
                {
                    c.moveTo(0, height * 0.5);
                    c.lineTo(width, height * 0.5);
                }
                else
                {
                    const centre = emblem.x + emblem.width / 2;
                    const unit = emblem.shapeScale;
                    const gap = unit * (root.design === 1 ? 70 : root.design === 2 ? 48
                        : root.design === 3 ? 36 : 66);
                    const top = (height - 120 * unit) / 2;
                    const leftY = top + unit * (root.design === 2 ? 45
                        : root.design === 3 ? 44 : root.design === 4 ? 88 : 51);
                    const rightY = root.design === 4 ? top + unit * 30 : leftY;
                    c.moveTo(0, height * 0.5);
                    c.quadraticCurveTo(centre * 0.45, height * 0.5, centre - gap, leftY);
                    c.moveTo(centre + gap, rightY);
                    c.quadraticCurveTo(width * 0.94, height * 0.5, width, height * 0.5);
                }
                c.stroke();
                c.strokeStyle = root.theme.whiteColor;
                c.lineWidth = 1;
                c.globalAlpha = 0.45;
                c.stroke();
                c.globalAlpha = 1;
                c.globalCompositeOperation = "destination-in";
                const fade = c.createLinearGradient(0, 0, width, 0);
                fade.addColorStop(0, "rgba(255,255,255,0)");
                fade.addColorStop(root.theme.ribbonEndFade, "rgba(255,255,255,1)");
                fade.addColorStop(1 - root.theme.ribbonEndFade, "rgba(255,255,255,1)");
                fade.addColorStop(1, "rgba(255,255,255,0)");
                c.fillStyle = fade;
                c.fillRect(0, 0, width, height);
                c.globalCompositeOperation = "source-over";
            }
        }
        RibbonEmblem
        {
            id: emblem
            x: parent.width * 0.62 - width / 2
            width: Math.min(parent.width * 0.48, parent.height * root.theme.ribbonMotifAspect)
            height: parent.height
            visible: root.design !== 0
            theme: root.theme
            design: root.design
        }
    }
    SequentialAnimation
    {
        running: root.motionActive
        loops: Animation.Infinite
        PauseAnimation { duration: root.theme.workspaceRibbonHold }
        NumberAnimation { target: root; property: "reveal"; to: 0; duration: root.theme.workspaceRibbonFade; easing.type: Easing.InOutSine }
        ScriptAction
        {
            script:
            {
                root.cycleIndex = (root.cycleIndex + 1) % root.sequence.length;
                root.design = root.sequence[root.cycleIndex];
            }
        }
        NumberAnimation { target: root; property: "reveal"; to: 1; duration: root.theme.workspaceRibbonFade; easing.type: Easing.InOutSine }
    }
}
