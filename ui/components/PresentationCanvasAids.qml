pragma ComponentBehavior: Bound

import QtQuick

Canvas
{
    id: root

    property var theme
    property var settings: ({})
    property real slideX: 0
    property real slideY: 0
    property real displayWidth: 0
    property real displayHeight: 0
    property real slideWidthPt: 0
    property real slideHeightPt: 0

    readonly property real scaleX: displayWidth / Math.max(1, slideWidthPt)
    readonly property real scaleY: displayHeight / Math.max(1, slideHeightPt)

    enabled: false
    visible: settings.showRulers === true || settings.showGrid === true || settings.showGuides === true

    onSettingsChanged: requestPaint()
    onSlideXChanged: requestPaint()
    onSlideYChanged: requestPaint()
    onDisplayWidthChanged: requestPaint()
    onDisplayHeightChanged: requestPaint()
    onSlideWidthPtChanged: requestPaint()
    onSlideHeightPtChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onVisibleChanged: requestPaint()

    onPaint:
    {
        const context = getContext("2d");
        context.clearRect(0, 0, width, height);
        if (!visible || slideWidthPt <= 0 || slideHeightPt <= 0) return;
        context.save();
        context.beginPath();
        context.rect(slideX, slideY, displayWidth, displayHeight);
        context.clip();

        if (settings.showGrid === true)
        {
            const spacing = Math.max(2, Number(settings.gridSpacingPt || 12));
            const xStep = spacing * Math.max(1, Math.ceil(8 / (spacing * scaleX)));
            const yStep = spacing * Math.max(1, Math.ceil(8 / (spacing * scaleY)));
            context.globalAlpha = 0.35;
            context.strokeStyle = theme.borderColor;
            context.lineWidth = 1;
            context.beginPath();
            for (let point = xStep; point < slideWidthPt; point += xStep)
            {
                const x = slideX + point * scaleX;
                context.moveTo(x, slideY);
                context.lineTo(x, slideY + displayHeight);
            }
            for (let point = yStep; point < slideHeightPt; point += yStep)
            {
                const y = slideY + point * scaleY;
                context.moveTo(slideX, y);
                context.lineTo(slideX + displayWidth, y);
            }
            context.stroke();
        }

        if (settings.showGuides === true)
        {
            context.globalAlpha = 0.85;
            context.strokeStyle = theme.accent;
            context.lineWidth = 1.5;
            context.beginPath();
            for (const point of settings.verticalGuidesPt || [])
            {
                const x = slideX + point * scaleX;
                context.moveTo(x, slideY);
                context.lineTo(x, slideY + displayHeight);
            }
            for (const point of settings.horizontalGuidesPt || [])
            {
                const y = slideY + point * scaleY;
                context.moveTo(slideX, y);
                context.lineTo(slideX + displayWidth, y);
            }
            context.stroke();
        }
        context.restore();

        if (settings.showRulers !== true) return;
        const rulerSize = 20;
        const fontSize = Math.max(9, theme.fontSize - 3);
        context.save();
        context.fillStyle = theme.surfaceColor;
        context.globalAlpha = 0.94;
        context.fillRect(Math.max(0, slideX), 0,
            Math.max(0, Math.min(width, slideX + displayWidth) - Math.max(0, slideX)), rulerSize);
        context.fillRect(0, Math.max(0, slideY), rulerSize,
            Math.max(0, Math.min(height, slideY + displayHeight) - Math.max(0, slideY)));
        context.globalAlpha = 1;
        context.strokeStyle = theme.borderColor;
        context.fillStyle = theme.textSecondary;
        context.lineWidth = 1;
        context.font = fontSize + "px '" + theme.fontFamily + "'";
        const xMajor = 72 * Math.max(1, Math.ceil(55 / (72 * scaleX)));
        const yMajor = 72 * Math.max(1, Math.ceil(55 / (72 * scaleY)));
        context.beginPath();
        for (let point = 0; point <= slideWidthPt; point += xMajor)
        {
            const x = slideX + point * scaleX;
            if (x < rulerSize || x > width) continue;
            context.moveTo(x, rulerSize);
            context.lineTo(x, rulerSize - 6);
            context.fillText(String(Math.round(point)), x + 2, 11);
        }
        for (let point = 0; point <= slideHeightPt; point += yMajor)
        {
            const y = slideY + point * scaleY;
            if (y < rulerSize || y > height) continue;
            context.moveTo(rulerSize, y);
            context.lineTo(rulerSize - 6, y);
            context.fillText(String(Math.round(point)), 2, y + 11);
        }
        context.stroke();
        context.restore();
    }
}
