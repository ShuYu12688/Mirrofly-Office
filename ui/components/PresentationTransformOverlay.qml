pragma ComponentBehavior: Bound

import QtQuick

Item
{
    id: root
    objectName: "presentationTransformOverlay"

    property var theme
    property var selection: ({})
    property real sceneScale: 1
    property real viewScale: 1
    property real slideWidthPt: 0
    property real slideHeightPt: 0
    property var guideSettings: ({})
    property bool dragging: false
    property var preview: ({})
    property var origin: ({})
    property point start: Qt.point(0, 0)
    property string handle: "move"
    readonly property var geometry: dragging ? preview : selection
    signal transformCommitted(var options)

    function snapTranslation(value)
    {
        if (guideSettings.snapToGrid !== true && guideSettings.snapToGuides !== true)
            return value;
        const tolerance = 6 / Math.max(0.001, sceneScale * viewScale);
        const corners = [[0, 0], [value.width, 0], [0, value.height],
            [value.width, value.height]];
        const horizontal = [];
        const vertical = [];
        for (const corner of corners)
        {
            horizontal.push(value.x + value.a * corner[0] + value.c * corner[1]);
            vertical.push(value.y + value.b * corner[0] + value.d * corner[1]);
        }
        const candidatesX = [Math.min(...horizontal),
            (Math.min(...horizontal) + Math.max(...horizontal)) / 2, Math.max(...horizontal)];
        const candidatesY = [Math.min(...vertical),
            (Math.min(...vertical) + Math.max(...vertical)) / 2, Math.max(...vertical)];
        const nearestDelta = function(values, guides, extent)
        {
            let best = tolerance + 1;
            let delta = 0;
            const spacing = Math.max(2, Number(root.guideSettings.gridSpacingPt || 12));
            for (const position of values)
            {
                if (root.guideSettings.snapToGrid === true)
                {
                    const target = Math.round(position / spacing) * spacing;
                    if (target >= 0 && target <= extent && Math.abs(target - position) < best)
                    {
                        best = Math.abs(target - position);
                        delta = target - position;
                    }
                }
                if (root.guideSettings.snapToGuides === true)
                    for (const target of guides)
                        if (Math.abs(target - position) < best)
                        {
                            best = Math.abs(target - position);
                            delta = target - position;
                        }
            }
            return best <= tolerance ? delta : 0;
        };
        value.x += nearestDelta(candidatesX, guideSettings.verticalGuidesPt || [], slideWidthPt);
        value.y += nearestDelta(candidatesY, guideSettings.horizontalGuidesPt || [], slideHeightPt);
        return value;
    }

    function cancel()
    {
        dragging = false;
        preview = {};
        origin = {};
    }

    function beginGesture(kind, x, y)
    {
        if (!enabled || !selection.valid || sceneScale <= 0) return false;
        const determinant = selection.a * selection.d - selection.b * selection.c;
        if (!isFinite(determinant) || Math.abs(determinant) < 0.000001) return false;
        origin = Object.assign({}, selection);
        preview = Object.assign({}, selection);
        start = Qt.point(x, y);
        handle = kind;
        dragging = true;
        return true;
    }

    function updateGesture(x, y, proportional)
    {
        if (!dragging) return;
        const dx = (x - start.x) / sceneScale;
        const dy = (y - start.y) / sceneScale;
        const next = Object.assign({}, origin);
        if (handle === "move")
        {
            if (Math.abs(x - start.x) + Math.abs(y - start.y) < 4) return;
            next.x += dx;
            next.y += dy;
            snapTranslation(next);
        }
        else
        {
            const determinant = origin.a * origin.d - origin.b * origin.c;
            const localX = (origin.d * dx - origin.c * dy) / determinant;
            const localY = (origin.a * dy - origin.b * dx) / determinant;
            const left = handle.indexOf("w") >= 0;
            const right = handle.indexOf("e") >= 0;
            const top = handle.indexOf("n") >= 0;
            const bottom = handle.indexOf("s") >= 0;
            if (left || right) next.width = Math.max(1, Math.min(20000, origin.width + (left ? -localX : localX)));
            if (top || bottom) next.height = Math.max(1, Math.min(20000, origin.height + (top ? -localY : localY)));
            if (proportional && (left || right) && (top || bottom))
            {
                let ratio = Math.abs(next.width / origin.width - 1) > Math.abs(next.height / origin.height - 1)
                    ? next.width / origin.width : next.height / origin.height;
                ratio = Math.max(1 / Math.min(origin.width, origin.height), Math.min(ratio, 20000 / Math.max(origin.width, origin.height)));
                next.width = origin.width * ratio;
                next.height = origin.height * ratio;
            }
            const offsetX = left ? origin.width - next.width : 0;
            const offsetY = top ? origin.height - next.height : 0;
            next.x += origin.a * offsetX + origin.c * offsetY;
            next.y += origin.b * offsetX + origin.d * offsetY;
        }
        if (Math.abs(next.x) <= 20000 && Math.abs(next.y) <= 20000) preview = next;
    }

    function endGesture()
    {
        if (!dragging) return;
        const next = preview;
        const changed = Math.abs(next.x - origin.x) + Math.abs(next.y - origin.y)
            + Math.abs(next.width - origin.width) + Math.abs(next.height - origin.height) > 0.001;
        cancel();
        if (changed) transformCommitted({x: next.x, y: next.y, width: next.width, height: next.height});
    }

    onSelectionChanged: cancel()
    onSceneScaleChanged: cancel()
    onEnabledChanged: { if (!enabled) cancel(); }

    Canvas
    {
        id: selectionOutline

        objectName: "presentationSelectionOutline"
        anchors.fill: parent
        visible: root.geometry.valid === true

        onPaint:
        {
            const context = getContext("2d");
            context.clearRect(0, 0, width, height);
            if (!visible) return;
            const value = root.geometry;
            const point = function(u, v)
            {
                return Qt.point((value.x + value.a * value.width * u + value.c * value.height * v)
                        * root.sceneScale,
                    (value.y + value.b * value.width * u + value.d * value.height * v)
                        * root.sceneScale);
            };
            const topLeft = point(0, 0);
            const topRight = point(1, 0);
            const bottomRight = point(1, 1);
            const bottomLeft = point(0, 1);
            context.beginPath();
            context.moveTo(topLeft.x, topLeft.y);
            context.lineTo(topRight.x, topRight.y);
            context.lineTo(bottomRight.x, bottomRight.y);
            context.lineTo(bottomLeft.x, bottomLeft.y);
            context.closePath();
            context.lineWidth = 1.5;
            context.strokeStyle = root.theme.accent;
            context.stroke();
        }

        Connections
        {
            target: root
            function onGeometryChanged() { selectionOutline.requestPaint(); }
            function onSceneScaleChanged() { selectionOutline.requestPaint(); }
            function onThemeChanged() { selectionOutline.requestPaint(); }
        }
    }

    Repeater
    {
        model: [{key: "nw", u: 0, v: 0}, {key: "n", u: 0.5, v: 0}, {key: "ne", u: 1, v: 0},
            {key: "e", u: 1, v: 0.5}, {key: "se", u: 1, v: 1}, {key: "s", u: 0.5, v: 1},
            {key: "sw", u: 0, v: 1}, {key: "w", u: 0, v: 0.5}]
        delegate: Rectangle
        {
            id: grip
            required property var modelData
            visible: root.enabled && root.geometry.valid === true
            width: 10
            height: 10
            radius: 3
            x: visible ? (root.geometry.x + root.geometry.a * root.geometry.width * modelData.u
                + root.geometry.c * root.geometry.height * modelData.v) * root.sceneScale - width / 2 : 0
            y: visible ? (root.geometry.y + root.geometry.b * root.geometry.width * modelData.u
                + root.geometry.d * root.geometry.height * modelData.v) * root.sceneScale - height / 2 : 0
            color: root.theme.surfaceColor
            border.color: root.theme.accent
            border.width: 1.5

            MouseArea
            {
                anchors.fill: parent
                anchors.margins: -4
                preventStealing: true
                cursorShape: grip.modelData.key === "n" || grip.modelData.key === "s" ? Qt.SizeVerCursor
                    : (grip.modelData.key === "e" || grip.modelData.key === "w" ? Qt.SizeHorCursor
                    : (grip.modelData.key === "nw" || grip.modelData.key === "se" ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor))
                onPressed: function(mouse)
                {
                    const point = mapToItem(root, mouse.x, mouse.y);
                    mouse.accepted = root.beginGesture(grip.modelData.key, point.x, point.y);
                    if (mouse.accepted) root.forceActiveFocus();
                }
                onPositionChanged: function(mouse)
                {
                    const point = mapToItem(root, mouse.x, mouse.y);
                    root.updateGesture(point.x, point.y, (mouse.modifiers & Qt.ShiftModifier) !== 0);
                }
                onReleased: root.endGesture()
                onCanceled: root.cancel()
            }
        }
    }
}
