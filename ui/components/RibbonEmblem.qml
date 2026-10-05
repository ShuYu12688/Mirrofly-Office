import QtQuick

Canvas
{
    id: root
    objectName: "ribbonEmblem"
    required property var theme
    property int design: 1
    readonly property real shapeScale: Math.min(width / 160, height / 120)
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onThemeChanged: requestPaint()
    onDesignChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()
    onPaint:
    {
        if (width <= 0 || height <= 0) return;
        const c = getContext("2d");
        c.reset();
        // A square-coordinate emblem keeps its silhouette at every window width.
        c.translate((width - 160 * shapeScale) / 2, (height - 120 * shapeScale) / 2);
        c.scale(shapeScale, shapeScale);
        const gradient = c.createLinearGradient(12, 20, 148, 94);
        const colors = [theme.spectrumBlue, theme.spectrumViolet, theme.spectrumCoral,
            theme.spectrumGold, theme.spectrumSage];
        for (let i = 0; i < colors.length; ++i)
            gradient.addColorStop(i / (colors.length - 1), colors[i]);
        c.fillStyle = gradient;
        c.strokeStyle = gradient;
        c.lineCap = "round";
        c.lineJoin = "round";
        if (design === 1)
        {
            // Separate loops, two forked tails and an opaque centre make a bow.
            c.beginPath();
            c.moveTo(73, 58); c.lineTo(45, 104); c.lineTo(61, 96);
            c.lineTo(68, 112); c.lineTo(86, 63); c.closePath(); c.fill();
            c.beginPath();
            c.moveTo(87, 58); c.lineTo(115, 104); c.lineTo(99, 96);
            c.lineTo(92, 112); c.lineTo(74, 63); c.closePath(); c.fill();
            c.beginPath();
            c.moveTo(76, 51);
            c.bezierCurveTo(55, 35, 19, 7, 12, 29);
            c.bezierCurveTo(3, 58, 14, 78, 36, 72);
            c.bezierCurveTo(52, 68, 65, 58, 76, 51);
            c.closePath(); c.fill();
            c.beginPath();
            c.moveTo(84, 51);
            c.bezierCurveTo(105, 35, 141, 7, 148, 29);
            c.bezierCurveTo(157, 58, 146, 78, 124, 72);
            c.bezierCurveTo(108, 68, 95, 58, 84, 51);
            c.closePath(); c.fill();
            c.strokeStyle = theme.whiteColor;
            c.globalAlpha = 0.6;
            c.lineWidth = 2;
            c.beginPath(); c.moveTo(23, 34); c.quadraticCurveTo(42, 36, 69, 51); c.stroke();
            c.beginPath(); c.moveTo(137, 34); c.quadraticCurveTo(118, 36, 91, 51); c.stroke();
            c.globalAlpha = 1;
            c.fillStyle = theme.spectrumBlue;
            c.beginPath(); c.roundedRect(71, 40, 18, 25, 5, 5); c.fill();
        }
        else if (design === 2)
        {
            // Diamond weave, side ears and twin tassels identify a Chinese knot.
            c.lineWidth = 6;
            for (const p of [[80, 10], [43, 45], [117, 45]])
            {
                c.beginPath(); c.arc(p[0], p[1], 8, 0, Math.PI * 2); c.stroke();
            }
            c.lineWidth = 9;
            c.beginPath();
            c.moveTo(80, 17); c.lineTo(108, 45); c.lineTo(80, 73);
            c.lineTo(52, 45); c.closePath(); c.stroke();
            c.lineWidth = 5;
            c.beginPath();
            c.moveTo(80, 29); c.lineTo(96, 45); c.lineTo(80, 61);
            c.lineTo(64, 45); c.closePath(); c.stroke();
            c.beginPath(); c.moveTo(65, 45); c.lineTo(95, 45); c.stroke();
            c.strokeStyle = theme.whiteColor;
            c.lineWidth = 8;
            c.beginPath(); c.moveTo(80, 37); c.lineTo(80, 53); c.stroke();
            c.strokeStyle = gradient;
            c.lineWidth = 4;
            c.beginPath(); c.moveTo(80, 29); c.lineTo(80, 61); c.stroke();
            c.beginPath(); c.moveTo(80, 73); c.lineTo(72, 87); c.lineTo(72, 111); c.stroke();
            c.beginPath(); c.moveTo(80, 73); c.lineTo(88, 87); c.lineTo(88, 111); c.stroke();
            c.lineWidth = 2;
            for (const x of [68, 76, 84, 92])
            {
                c.beginPath(); c.moveTo(x, 92); c.lineTo(x, 114); c.stroke();
            }
        }
        else if (design === 3)
        {
            c.lineWidth = 12;
            c.beginPath(); c.arc(80, 44, 30, 0, Math.PI * 2); c.stroke();
            c.beginPath(); c.moveTo(68, 71); c.quadraticCurveTo(54, 93, 42, 102); c.stroke();
            c.beginPath(); c.moveTo(90, 70); c.quadraticCurveTo(101, 91, 117, 103); c.stroke();
            c.fillStyle = theme.spectrumBlue;
            c.beginPath(); c.roundedRect(71, 64, 18, 16, 4, 4); c.fill();
        }
        else if (design === 4)
        {
            c.beginPath();
            c.moveTo(13, 87); c.lineTo(53, 19); c.lineTo(90, 79);
            c.lineTo(130, 19); c.lineTo(145, 29); c.lineTo(90, 109);
            c.lineTo(53, 49); c.lineTo(28, 97); c.closePath(); c.fill();
            c.strokeStyle = theme.whiteColor;
            c.globalAlpha = 0.65;
            c.lineWidth = 1.5;
            c.beginPath(); c.moveTo(53, 24); c.lineTo(90, 88); c.lineTo(137, 25); c.stroke();
        }
    }
}
