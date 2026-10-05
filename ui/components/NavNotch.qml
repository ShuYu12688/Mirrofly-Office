import QtQuick

Canvas
{
    id: root

    property color fillColor
    property real cornerRadius: 24
    property real shoulderRadius: 20
    property real leadingRadius: 24
    property bool cutoutEnabled: false
    property real cutoutX: 0
    property real cutoutY: 0
    property real cutoutWidth: 0
    property real cutoutHeight: 0

    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onFillColorChanged: requestPaint()
    onCornerRadiusChanged: requestPaint()
    onShoulderRadiusChanged: requestPaint()
    onLeadingRadiusChanged: requestPaint()
    onCutoutEnabledChanged: requestPaint()
    onCutoutXChanged: requestPaint()
    onCutoutYChanged: requestPaint()
    onCutoutWidthChanged: requestPaint()
    onCutoutHeightChanged: requestPaint()
    onVisibleChanged:
    {
        if (visible)
        {
            requestPaint();
        }
    }

    onPaint:
    {
        const context = getContext("2d");
        const corner = Math.min(root.cornerRadius, root.width / 2, root.height / 2);
        const arc = 0.55228475;

        context.clearRect(0, 0, root.width, root.height);
        context.globalCompositeOperation = "source-over";
        context.fillStyle = root.fillColor;
        context.beginPath();
        context.moveTo(corner, 0);
        context.lineTo(root.width - corner, 0);
        context.bezierCurveTo(root.width - corner * (1 - arc), 0,
            root.width, corner * (1 - arc), root.width, corner);
        context.lineTo(root.width, root.height - corner);
        context.bezierCurveTo(root.width, root.height - corner * (1 - arc),
            root.width - corner * (1 - arc), root.height, root.width - corner, root.height);
        context.lineTo(corner, root.height);
        context.bezierCurveTo(corner * (1 - arc), root.height,
            0, root.height - corner * (1 - arc), 0, root.height - corner);
        context.lineTo(0, corner);
        context.bezierCurveTo(0, corner * (1 - arc), corner * (1 - arc), 0, corner, 0);
        context.closePath();
        context.fill();

        if (!root.cutoutEnabled || root.cutoutWidth <= 0 || root.cutoutHeight <= 0)
        {
            return;
        }

        const shoulder = Math.min(root.shoulderRadius, root.cutoutWidth / 3, root.cutoutHeight / 4);
        const leading = Math.min(root.leadingRadius, (root.cutoutHeight - shoulder * 2) / 2);
        const bottom = root.cutoutHeight - shoulder;

        context.save();
        context.translate(root.cutoutX, root.cutoutY);
        context.globalCompositeOperation = "destination-out";
        context.fillStyle = Qt.rgba(root.fillColor.r, root.fillColor.g, root.fillColor.b, 1);
        context.beginPath();
        context.moveTo(root.cutoutWidth, 0);
        context.bezierCurveTo(root.cutoutWidth, shoulder * arc,
            root.cutoutWidth - shoulder * (1 - arc), shoulder, root.cutoutWidth - shoulder, shoulder);
        context.lineTo(leading, shoulder);
        context.bezierCurveTo(leading * (1 - arc), shoulder,
            0, shoulder + leading * (1 - arc), 0, shoulder + leading);
        context.lineTo(0, bottom - leading);
        context.bezierCurveTo(0, bottom - leading * (1 - arc),
            leading * (1 - arc), bottom, leading, bottom);
        context.lineTo(root.cutoutWidth - shoulder, bottom);
        context.bezierCurveTo(root.cutoutWidth - shoulder * (1 - arc), bottom,
            root.cutoutWidth, root.cutoutHeight - shoulder * arc, root.cutoutWidth, root.cutoutHeight);
        context.closePath();
        context.fill();
        context.restore();
    }
}
