import QtQuick

Canvas
{
    id: root

    property var theme
    property real radius: theme.radius
    property real lineWidth: 1.6

    onThemeChanged: requestPaint()
    onRadiusChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()

    onPaint:
    {
        if (!theme || width < 4 || height < 4)
        {
            return;
        }
        const ctx = getContext("2d");
        ctx.reset();
        const gradient = ctx.createLinearGradient(0, height, width, 0);
        gradient.addColorStop(0, theme.spectrumBlue);
        gradient.addColorStop(0.28, theme.spectrumSage);
        gradient.addColorStop(0.53, theme.spectrumGold);
        gradient.addColorStop(0.76, theme.spectrumCoral);
        gradient.addColorStop(1, theme.spectrumViolet);
        ctx.strokeStyle = gradient;
        ctx.lineWidth = lineWidth;
        const inset = lineWidth + 1;
        const r = Math.min(radius, (height - inset * 2) / 2);
        ctx.beginPath();
        ctx.roundedRect(inset, inset, width - inset * 2, height - inset * 2, r, r);
        ctx.stroke();
        ctx.globalAlpha = 0.32;
        ctx.lineWidth = lineWidth * 0.6;
        ctx.beginPath();
        ctx.moveTo(width * 0.18, height - inset - 4);
        ctx.bezierCurveTo(width * 0.44, height - 8, width * 0.73, height - 1, width * 0.88, height - 8);
        ctx.stroke();
    }
}
