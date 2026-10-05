import QtQuick

Canvas
{
    id: root
    property string kind: "word"
    property color ink: "white"
    implicitWidth: 64
    implicitHeight: 64
    onKindChanged: requestPaint()
    onInkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint:
    {
        const c = getContext("2d");
        c.reset();
        c.scale(width / 64, height / 64);
        c.strokeStyle = root.ink;
        c.lineWidth = 2.8;
        c.lineCap = "round";
        c.lineJoin = "round";
        c.beginPath();
        if (root.kind === "slides")
        {
            c.roundedRect(5, 10, 54, 37, 7, 7);
            c.moveTo(32, 47); c.lineTo(20, 57);
            c.moveTo(32, 47); c.lineTo(44, 57);
            c.moveTo(32, 47); c.lineTo(32, 54);
            c.moveTo(19, 32); c.arc(19, 29, 8, Math.PI / 2, Math.PI * 2);
            c.lineTo(19, 29); c.lineTo(27, 29);
            c.moveTo(37, 24); c.lineTo(49, 24);
            c.moveTo(37, 33); c.lineTo(46, 33);
        }
        else if (root.kind === "sheets")
        {
            c.roundedRect(7, 8, 50, 48, 8, 8);
            c.moveTo(7, 23); c.lineTo(57, 23);
            c.moveTo(7, 39); c.lineTo(57, 39);
            c.moveTo(24, 8); c.lineTo(24, 56);
            c.moveTo(41, 23); c.lineTo(41, 56);
        }
        else if (root.kind === "mindmap")
        {
            c.roundedRect(4, 24, 18, 16, 6, 6);
            c.roundedRect(42, 7, 18, 14, 5, 5);
            c.roundedRect(42, 43, 18, 14, 5, 5);
            c.moveTo(22, 32); c.bezierCurveTo(34, 32, 29, 14, 42, 14);
            c.moveTo(22, 32); c.bezierCurveTo(34, 32, 29, 50, 42, 50);
        }
        else if (root.kind === "markdown")
        {
            c.roundedRect(4, 12, 56, 40, 8, 8);
            c.moveTo(14, 41); c.lineTo(14, 24); c.lineTo(23, 33);
            c.lineTo(32, 24); c.lineTo(32, 41);
            c.moveTo(46, 23); c.lineTo(46, 41);
            c.moveTo(40, 35); c.lineTo(46, 41); c.lineTo(52, 35);
        }
        else
        {
            c.moveTo(39, 5); c.lineTo(19, 5);
            c.bezierCurveTo(12, 5, 10, 9, 10, 15);
            c.lineTo(10, 49); c.bezierCurveTo(10, 56, 14, 59, 20, 59);
            c.lineTo(44, 59); c.bezierCurveTo(51, 59, 54, 55, 54, 49);
            c.lineTo(54, 20); c.lineTo(39, 5);
            c.moveTo(39, 5); c.lineTo(39, 17);
            c.bezierCurveTo(39, 20, 41, 20, 44, 20); c.lineTo(54, 20);
            if (root.kind === "pdf")
            {
                c.moveTo(22, 46); c.bezierCurveTo(39, 22, 26, 22, 29, 32);
                c.bezierCurveTo(32, 48, 49, 38, 40, 38);
                c.bezierCurveTo(28, 38, 15, 47, 22, 46);
            }
            else
            {
                c.moveTo(21, 34); c.lineTo(42, 34);
                c.moveTo(21, 44); c.lineTo(root.kind === "word" ? 35 : 42, 44);
            }
        }
        c.stroke();
    }
}
