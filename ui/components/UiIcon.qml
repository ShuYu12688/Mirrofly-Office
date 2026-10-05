import QtQuick

Item
{
    id: root

    property string symbol: "home"
    property color iconColor
    property real lineWidth: 1.7

    implicitWidth: 24
    implicitHeight: 24

    onSymbolChanged: drawing.requestPaint()
    onIconColorChanged: drawing.requestPaint()

    Canvas
    {
        id: drawing

        anchors.fill: parent
        onPaint:
        {
            const context = getContext("2d");
            context.clearRect(0, 0, width, height);
            context.save();
            context.scale(width / 24, height / 24);
            context.strokeStyle = root.iconColor;
            context.fillStyle = root.iconColor;
            context.lineWidth = root.lineWidth;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.beginPath();

            if (root.symbol === "mindmap")
            {
                context.rect(2, 9, 7, 6); context.rect(16, 3, 6, 5); context.rect(16, 16, 6, 5);
                context.moveTo(9,12); context.lineTo(12,12); context.lineTo(12,5.5); context.lineTo(16,5.5);
                context.moveTo(12,12); context.lineTo(12,18.5); context.lineTo(16,18.5);
            }
            else if (root.symbol === "pdf")
            {
                context.rect(4, 2, 15, 20);
                context.moveTo(7,8); context.lineTo(15,8); context.moveTo(7,12); context.lineTo(13,12);
                context.moveTo(11,18); context.lineTo(20,9); context.lineTo(22,11); context.lineTo(13,20); context.closePath();
            }
            else if (root.symbol === "home")
            {
                context.moveTo(3.5, 10.5);
                context.lineTo(12, 3.5);
                context.lineTo(20.5, 10.5);
                context.moveTo(5.5, 9.5);
                context.lineTo(5.5, 20);
                context.lineTo(10, 20);
                context.lineTo(10, 14);
                context.lineTo(14, 14);
                context.lineTo(14, 20);
                context.lineTo(18.5, 20);
                context.lineTo(18.5, 9.5);
            }
            else if (root.symbol === "clock")
            {
                context.arc(12, 12, 8.5, 0, Math.PI * 2);
                context.moveTo(12, 7);
                context.lineTo(12, 12);
                context.lineTo(15.5, 14);
            }
            else if (root.symbol === "star" || root.symbol === "starFilled")
            {
                for (let point = 0; point < 10; ++point)
                {
                    const angle = -Math.PI / 2 + point * Math.PI / 5;
                    const radius = point % 2 === 0 ? 9 : 4.4;
                    const x = 12 + Math.cos(angle) * radius;
                    const y = 12 + Math.sin(angle) * radius;
                    if (point === 0)
                    {
                        context.moveTo(x, y);
                    }
                    else
                    {
                        context.lineTo(x, y);
                    }
                }
                context.closePath();
                if (root.symbol === "starFilled")
                {
                    context.fill();
                }
            }
            else if (root.symbol === "folder")
            {
                context.moveTo(3.5, 8);
                context.lineTo(3.5, 5.5);
                context.lineTo(9, 5.5);
                context.lineTo(11.5, 8);
                context.lineTo(20.5, 8);
                context.lineTo(20.5, 18.5);
                context.lineTo(3.5, 18.5);
                context.closePath();
            }
            else if (root.symbol === "search")
            {
                context.arc(10.5, 10.5, 6, 0, Math.PI * 2);
                context.moveTo(15, 15);
                context.lineTo(20, 20);
            }
            else if (root.symbol === "arrow")
            {
                context.moveTo(5, 12);
                context.lineTo(19, 12);
                context.moveTo(14, 7);
                context.lineTo(19, 12);
                context.lineTo(14, 17);
            }
            else if (root.symbol === "plus")
            {
                context.moveTo(5, 12);
                context.lineTo(19, 12);
                context.moveTo(12, 5);
                context.lineTo(12, 19);
            }
            else if (root.symbol === "close")
            {
                context.moveTo(6, 6);
                context.lineTo(18, 18);
                context.moveTo(18, 6);
                context.lineTo(6, 18);
            }
            else if (root.symbol === "play")
            {
                context.moveTo(8, 5);
                context.lineTo(19, 12);
                context.lineTo(8, 19);
                context.closePath();
            }
            else if (root.symbol === "ai")
            {
                context.arc(9, 12, 3.2, 0, Math.PI * 2);
                context.moveTo(12.2, 12);
                context.lineTo(17.5, 12);
                context.moveTo(6.5, 9.8);
                context.lineTo(4, 6.5);
                context.moveTo(6.5, 14.2);
                context.lineTo(4, 17.5);
                context.moveTo(18, 3.5);
                context.lineTo(18, 8.5);
                context.moveTo(15.5, 6);
                context.lineTo(20.5, 6);
                context.moveTo(18, 15.5);
                context.lineTo(18, 20.5);
                context.moveTo(15.5, 18);
                context.lineTo(20.5, 18);
            }
            else if (root.symbol === "check")
            {
                context.moveTo(5, 12);
                context.lineTo(10, 17);
                context.lineTo(19, 7);
            }
            else if (root.symbol === "grid")
            {
                context.rect(4, 4, 16, 16);
                context.moveTo(4, 9);
                context.lineTo(20, 9);
                context.moveTo(4, 14.5);
                context.lineTo(20, 14.5);
                context.moveTo(10, 4);
                context.lineTo(10, 20);
            }
            else if (root.symbol === "slides")
            {
                context.rect(3.5, 4.5, 17, 12);
                context.moveTo(12, 16.5);
                context.lineTo(12, 21);
                context.moveTo(8, 21);
                context.lineTo(16, 21);
                context.moveTo(8, 12);
                context.lineTo(8, 9);
                context.moveTo(12, 12);
                context.lineTo(12, 7.5);
                context.moveTo(16, 12);
                context.lineTo(16, 10);
            }
            else
            {
                context.moveTo(6, 3);
                context.lineTo(14, 3);
                context.lineTo(19, 8);
                context.lineTo(19, 21);
                context.lineTo(6, 21);
                context.closePath();
                context.moveTo(14, 3);
                context.lineTo(14, 8);
                context.lineTo(19, 8);
                context.moveTo(9, 12);
                context.lineTo(16, 12);
                context.moveTo(9, 16);
                context.lineTo(14, 16);
            }

            context.stroke();
            context.restore();
        }
    }
}
