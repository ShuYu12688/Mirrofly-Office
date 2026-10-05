import QtQuick

Canvas
{
    id: root
    property var theme
    property bool flowing: false
    onThemeChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()
    onPaint:
    {
        if (!theme || width <= 0 || height <= 0) return;
        const c = getContext("2d");
        c.reset();
        // Broad ribbons cross on a diagonal; the second pass supplies their lit folds.
        const colors = [theme.spectrumBlue, theme.spectrumViolet,
            theme.spectrumCoral, theme.spectrumGold, theme.spectrumSage];
        for (let band = 0; band < 2; ++band)
        {
            const start = band === 0 ? 0.24 : 0.74;
            const gradient = c.createLinearGradient(0, height, width, 0);
            for (let index = 0; index < colors.length; ++index)
                gradient.addColorStop(index / (colors.length - 1), colors[index]);
            c.strokeStyle = gradient;
            c.lineWidth = height * 0.23;
            c.globalAlpha = band === 0 ? 0.36 : 0.28;
            c.beginPath();
            c.moveTo(0, height * start);
            c.bezierCurveTo(width * 0.32, height * (band === 0 ? 0.92 : 0.04),
                width * 0.68, height * (band === 0 ? 0.08 : 0.90),
                width, height * (band === 0 ? 0.76 : 0.22));
            c.stroke();
            c.lineWidth = 1.4;
            c.globalAlpha = 0.75;
            c.strokeStyle = theme.whiteColor;
            c.stroke();
        }
        c.globalAlpha = 1;
        c.globalCompositeOperation = "destination-in";
        const fade = c.createLinearGradient(0, 0, width, 0);
        const end = theme.ribbonEndFade;
        fade.addColorStop(0, "rgba(255,255,255,0)");
        fade.addColorStop(end * 0.4, "rgba(255,255,255,0.3)");
        fade.addColorStop(end, "rgba(255,255,255,1)");
        fade.addColorStop(1 - end, "rgba(255,255,255,1)");
        fade.addColorStop(1 - end * 0.4, "rgba(255,255,255,0.3)");
        fade.addColorStop(1, "rgba(255,255,255,0)");
        c.fillStyle = fade;
        c.fillRect(0, 0, width, height);
        c.globalCompositeOperation = "source-over";
    }
}