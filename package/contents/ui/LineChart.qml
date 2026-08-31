import QtQuick 2.15

Item {
    id: root

    property var points: []
    property color strokeColor: "#4ade80"
    property color fillColor: "#334ade80"
    property color gridColor: "#33888888"
    property color labelColor: "#d8dee9"
    property real minValue: 0
    property real maxValue: 0

    function valueAt(index) {
        return Number(points[index] && points[index].value ? points[index].value : 0)
    }

    function xAt(index) {
        if (points.length <= 1)
            return width / 2
        return (index / (points.length - 1)) * width
    }

    function yAt(index) {
        const value = valueAt(index)
        const range = Math.max(1, maxValue - minValue)
        return height - ((value - minValue) / range) * height
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            ctx.strokeStyle = gridColor
            ctx.lineWidth = 1
            for (let i = 1; i < 4; ++i) {
                const y = (height / 4) * i
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(width, y)
                ctx.stroke()
            }

            if (!points || points.length === 0)
                return

            ctx.beginPath()
            ctx.moveTo(xAt(0), yAt(0))
            for (let index = 1; index < points.length; ++index)
                ctx.lineTo(xAt(index), yAt(index))

            ctx.strokeStyle = strokeColor
            ctx.lineWidth = 2
            ctx.stroke()

            ctx.lineTo(width, height)
            ctx.lineTo(0, height)
            ctx.closePath()
            ctx.fillStyle = fillColor
            ctx.fill()
        }
    }

    onPointsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onMinValueChanged: canvas.requestPaint()
    onMaxValueChanged: canvas.requestPaint()
}
