import QtQuick 2.15

Item {
    id: root

    property var points: []
    property color gridColor: "#33888888"
    property color accountColor: "#4ade80"
    property color investedColor: "#fbbf24"
    property color profitFillColor: "#2034d399"
    property color lossFillColor: "#20ef4444"
    property color positiveBarColor: "#22c55e"
    property color negativeBarColor: "#ef4444"
    property color drawdownColor: "#fb7185"
    property color drawdownFillColor: "#1ffb7185"
    property color topBackgroundColor: "#10161f"
    property color lowerBackgroundColor: "#0c1118"
    property color axisLabelColor: "#d8dee9"
    property color axisMutedColor: "#88d8dee9"
    property int axisWidth: 74
    property real chartLeft: axisWidth
    property real chartRight: width - 10
    property real chartTop: 6
    property real chartBottom: height - 6
    property real topAreaHeight: Math.max(90, height * 0.58)
    property real barsAreaHeight: Math.max(34, height * 0.18)
    property real drawdownAreaHeight: Math.max(34, height * 0.16)
    property real topAreaBottom: Math.min(chartBottom - barsAreaHeight - drawdownAreaHeight - 26, chartTop + topAreaHeight)
    property real barsAreaTop: topAreaBottom + 12
    property real barsAreaBottom: barsAreaTop + barsAreaHeight
    property real drawdownAreaTop: barsAreaBottom + 10
    property real drawdownAreaBottom: Math.min(chartBottom, drawdownAreaTop + drawdownAreaHeight)
    property var moneyBounds: computeMoneyBounds()

    function pointNumber(point, key) {
        if (!point || point[key] === undefined)
            return 0
        return Number(point[key])
    }

    function computeMoneyBounds() {
        let min = Number.POSITIVE_INFINITY
        let max = Number.NEGATIVE_INFINITY
        for (let i = 0; i < points.length; ++i) {
            min = Math.min(min, pointNumber(points[i], "accountValue"), pointNumber(points[i], "netInvested"))
            max = Math.max(max, pointNumber(points[i], "accountValue"), pointNumber(points[i], "netInvested"))
        }
        if (!isFinite(min) || !isFinite(max))
            return { min: 0, max: 1 }
        if (Math.abs(max - min) < 1)
            return { min: min - 1, max: max + 1 }
        const padding = (max - min) * 0.08
        return { min: min - padding, max: max + padding }
    }

    function moneyLabel(value) {
        const amount = Math.abs(Number(value))
        const sign = Number(value) < 0 ? "-" : ""
        if (amount >= 1000000)
            return sign + "$" + (amount / 1000000).toLocaleString(Qt.locale(), "f", 2) + "M"
        if (amount >= 1000)
            return sign + "$" + (amount / 1000).toLocaleString(Qt.locale(), "f", 1) + "K"
        return sign + "$" + amount.toLocaleString(Qt.locale(), "f", 0)
    }

    function xAt(index, left, chartWidth) {
        if (points.length <= 1)
            return left + chartWidth / 2
        return left + (index / (points.length - 1)) * chartWidth
    }

    Rectangle {
        x: root.chartLeft - 1
        y: root.chartTop
        width: 1
        height: Math.max(1, root.topAreaBottom - root.chartTop)
        color: root.gridColor
        opacity: 0.8
    }

    Text {
        x: 0
        y: root.chartTop - 2
        width: root.axisWidth - 10
        horizontalAlignment: Text.AlignRight
        text: root.moneyLabel(root.moneyBounds.max)
        color: root.axisLabelColor
        font.pixelSize: 12
        visible: root.points.length > 0
    }

    Text {
        x: 0
        y: ((root.chartTop + root.topAreaBottom) / 2) - (height / 2)
        width: root.axisWidth - 10
        horizontalAlignment: Text.AlignRight
        text: root.moneyLabel((root.moneyBounds.max + root.moneyBounds.min) / 2)
        color: root.axisMutedColor
        font.pixelSize: 12
        visible: root.points.length > 0
    }

    Text {
        x: 0
        y: root.topAreaBottom - height + 2
        width: root.axisWidth - 10
        horizontalAlignment: Text.AlignRight
        text: root.moneyLabel(root.moneyBounds.min)
        color: root.axisLabelColor
        font.pixelSize: 12
        visible: root.points.length > 0
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            if (!root.points || root.points.length === 0)
                return

            const left = root.chartLeft
            const right = root.chartRight
            const top = root.chartTop
            const bottom = root.chartBottom
            const chartWidth = Math.max(1, right - left)
            const topBottom = root.topAreaBottom
            const barsTop = root.barsAreaTop
            const barsBottom = root.barsAreaBottom
            const drawdownTop = root.drawdownAreaTop
            const drawdownBottom = root.drawdownAreaBottom

            ctx.fillStyle = root.topBackgroundColor
            ctx.fillRect(left, top, chartWidth, Math.max(1, topBottom - top))
            ctx.fillStyle = root.lowerBackgroundColor
            ctx.fillRect(left, barsTop, chartWidth, Math.max(1, barsBottom - barsTop))
            ctx.fillRect(left, drawdownTop, chartWidth, Math.max(1, drawdownBottom - drawdownTop))

            function drawGrid(areaTop, areaBottom, lines) {
                ctx.strokeStyle = root.gridColor
                ctx.lineWidth = 1
                for (let i = 0; i <= lines; ++i) {
                    const y = areaTop + ((areaBottom - areaTop) / lines) * i
                    ctx.beginPath()
                    ctx.moveTo(left, y)
                    ctx.lineTo(right, y)
                    ctx.stroke()
                }
            }

            function yForTop(value, bounds) {
                const range = Math.max(1, bounds.max - bounds.min)
                return topBottom - ((value - bounds.min) / range) * (topBottom - top)
            }

            function yForBar(value, limit, zeroY) {
                return zeroY - (value / limit) * ((barsBottom - barsTop) / 2 - 2)
            }

            function yForDrawdown(value, minDrawdown) {
                return drawdownTop + (value / minDrawdown) * (drawdownBottom - drawdownTop)
            }

            drawGrid(top, topBottom, 4)
            drawGrid(barsTop, barsBottom, 2)
            drawGrid(drawdownTop, drawdownBottom, 2)

            const topBounds = root.moneyBounds
            let maxAbsDaily = 1
            let minDrawdown = 0
            for (let i = 0; i < root.points.length; ++i) {
                maxAbsDaily = Math.max(maxAbsDaily, Math.abs(pointNumber(root.points[i], "dailyPerformance")))
                minDrawdown = Math.min(minDrawdown, pointNumber(root.points[i], "drawdownPct"))
            }

            const bandColor = pointNumber(root.points[root.points.length - 1], "profitLoss") >= 0
                ? root.profitFillColor
                : root.lossFillColor

            ctx.beginPath()
            ctx.moveTo(xAt(0, left, chartWidth), yForTop(pointNumber(root.points[0], "accountValue"), topBounds))
            for (let i = 1; i < root.points.length; ++i)
                ctx.lineTo(xAt(i, left, chartWidth), yForTop(pointNumber(root.points[i], "accountValue"), topBounds))
            for (let i = root.points.length - 1; i >= 0; --i)
                ctx.lineTo(xAt(i, left, chartWidth), yForTop(pointNumber(root.points[i], "netInvested"), topBounds))
            ctx.closePath()
            ctx.fillStyle = bandColor
            ctx.fill()

            ctx.beginPath()
            ctx.moveTo(xAt(0, left, chartWidth), yForTop(pointNumber(root.points[0], "netInvested"), topBounds))
            for (let i = 1; i < root.points.length; ++i)
                ctx.lineTo(xAt(i, left, chartWidth), yForTop(pointNumber(root.points[i], "netInvested"), topBounds))
            if (ctx.setLineDash)
                ctx.setLineDash([6, 4])
            ctx.strokeStyle = root.investedColor
            ctx.lineWidth = 2
            ctx.stroke()
            if (ctx.setLineDash)
                ctx.setLineDash([])

            ctx.beginPath()
            ctx.moveTo(xAt(0, left, chartWidth), yForTop(pointNumber(root.points[0], "accountValue"), topBounds))
            for (let i = 1; i < root.points.length; ++i)
                ctx.lineTo(xAt(i, left, chartWidth), yForTop(pointNumber(root.points[i], "accountValue"), topBounds))
            ctx.strokeStyle = root.accountColor
            ctx.lineWidth = 3
            ctx.stroke()

            const markerRadius = root.points.length <= 10 ? 4 : 2.5
            for (let i = 0; i < root.points.length; ++i) {
                const accountX = xAt(i, left, chartWidth)
                const accountY = yForTop(pointNumber(root.points[i], "accountValue"), topBounds)
                ctx.beginPath()
                ctx.arc(accountX, accountY, markerRadius, 0, Math.PI * 2)
                ctx.fillStyle = root.accountColor
                ctx.fill()
            }

            if (root.points.length === 1) {
                const investedX = xAt(0, left, chartWidth)
                const investedY = yForTop(pointNumber(root.points[0], "netInvested"), topBounds)
                ctx.beginPath()
                ctx.arc(investedX, investedY, markerRadius + 3, 0, Math.PI * 2)
                ctx.strokeStyle = root.investedColor
                ctx.lineWidth = 2
                ctx.stroke()
            }

            const zeroY = (barsTop + barsBottom) / 2
            ctx.strokeStyle = root.gridColor
            ctx.beginPath()
            ctx.moveTo(left, zeroY)
            ctx.lineTo(right, zeroY)
            ctx.stroke()

            const barWidth = Math.max(3, Math.min(12, chartWidth / Math.max(4, root.points.length * 1.4)))
            for (let i = 0; i < root.points.length; ++i) {
                const value = pointNumber(root.points[i], "dailyPerformance")
                const x = xAt(i, left, chartWidth) - barWidth / 2
                const y = yForBar(value, maxAbsDaily, zeroY)
                const topY = Math.min(y, zeroY)
                const rectHeight = Math.max(1, Math.abs(zeroY - y))
                ctx.fillStyle = value >= 0 ? root.positiveBarColor : root.negativeBarColor
                ctx.fillRect(x, topY, barWidth, rectHeight)
            }

            ctx.beginPath()
            ctx.moveTo(xAt(0, left, chartWidth), yForDrawdown(pointNumber(root.points[0], "drawdownPct"), Math.min(-1, minDrawdown)))
            for (let i = 1; i < root.points.length; ++i)
                ctx.lineTo(xAt(i, left, chartWidth), yForDrawdown(pointNumber(root.points[i], "drawdownPct"), Math.min(-1, minDrawdown)))
            ctx.lineTo(right, drawdownBottom)
            ctx.lineTo(left, drawdownBottom)
            ctx.closePath()
            ctx.fillStyle = root.drawdownFillColor
            ctx.fill()

            ctx.beginPath()
            ctx.moveTo(xAt(0, left, chartWidth), yForDrawdown(pointNumber(root.points[0], "drawdownPct"), Math.min(-1, minDrawdown)))
            for (let i = 1; i < root.points.length; ++i)
                ctx.lineTo(xAt(i, left, chartWidth), yForDrawdown(pointNumber(root.points[i], "drawdownPct"), Math.min(-1, minDrawdown)))
            ctx.strokeStyle = root.drawdownColor
            ctx.lineWidth = 2
            ctx.stroke()
        }
    }

    onPointsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
