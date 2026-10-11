import QtQuick
import QtGraphs

// Power spectrum plot with linear / logarithmic axes, a hover readout, wheel
// zoom, drag pan and double-click to reset the view.
//
//   wheel            zoom both axes around the cursor
//   Shift + wheel    zoom x only        Ctrl + wheel    zoom y only
//   drag             pan
//   double-click     show all of the data
Item {
    id: view

    // Visible range (what the axes show)
    property real xMin: 2
    property real xMax: 3
    property real yMin: 0
    property real yMax: 1

    property bool logX: false
    property bool logY: false
    property string yTitle: ""
    property string yName: ""   // short name for the readout, e.g. "D_ℓ (TT)"

    // The data, kept for the readout and to rebuild the line when the scales
    // change
    property var ell: []
    property var values: []

    // Set from the dock (the user's choice); the rest is derived from them
    property color bgColor: "#121217"
    property color fgColor: "#e6e6e6"
    property color lineColor: "#7fd1ff"

    // GraphsView does not repaint when only a series' color changes, so
    // refill the series (and ask for a redraw) after any color change
    function refreshColors() {
        series.color = lineColor
        rebuild()
        graph.update()
    }
    onLineColorChanged: refreshColors()
    onBgColorChanged: graph.update()
    onFgColorChanged: graph.update()

    function mix(a, b, t) {   // t = 0 gives a, t = 1 gives b
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t, 1)
    }
    readonly property color fg: fgColor
    readonly property color dim: mix(fgColor, bgColor, 0.62)
    readonly property color gridMain: mix(fgColor, bgColor, 0.84)
    readonly property color gridSub: mix(fgColor, bgColor, 0.92)
    readonly property color plotBg: mix(bgColor, fgColor, 0.04)
    readonly property bool dark: (0.299 * bgColor.r + 0.587 * bgColor.g
                                  + 0.114 * bgColor.b) < 0.5

    property var linX: ValueAxis {
        min: view.xMin
        max: view.xMax
        titleText: "Multipole ℓ"
        titleColor: view.fg
        color: view.dim
        subColor: view.dim
    }
    property var logXAxis: LogValueAxis {
        base: 10
        min: Math.max(view.xMin, 1e-9)
        max: view.xMax
        titleText: "Multipole ℓ"
        titleColor: view.fg
        color: view.dim
        subColor: view.dim
    }
    property var linY: ValueAxis {
        min: view.yMin
        max: view.yMax
        titleText: view.yTitle
        titleColor: view.fg
        color: view.dim
        subColor: view.dim
    }
    property var logYAxis: LogValueAxis {
        base: 10
        min: Math.max(view.yMin, 1e-300)
        max: view.yMax
        titleText: view.yTitle
        titleColor: view.fg
        color: view.dim
        subColor: view.dim
    }

    GraphsView {
        id: graph
        anchors.fill: parent

        axisX: view.logX ? view.logXAxis : view.linX
        axisY: view.logY ? view.logYAxis : view.linY

        theme: GraphsTheme {
            colorScheme: view.dark ? GraphsTheme.ColorScheme.Dark
                                   : GraphsTheme.ColorScheme.Light
            backgroundColor: view.bgColor
            plotAreaBackgroundColor: view.plotBg
            labelTextColor: view.fg
            grid.mainColor: view.gridMain
            grid.subColor: view.gridSub
        }

        LineSeries {
            id: series
            color: view.lineColor
            width: 2
        }

    }

    // ---- axis <-> pixel mapping (in the axis' own space: log10 or linear)

    function u(v, log) { return log ? Math.log(v) : v }
    function ui(v, log) { return log ? Math.exp(v) : v }

    function dataX(px) {
        const pa = graph.plotArea
        const f = (px - pa.x) / pa.width
        const lo = u(xMin, logX), hi = u(xMax, logX)
        return ui(lo + f * (hi - lo), logX)
    }
    function dataY(py) {
        const pa = graph.plotArea
        const f = (pa.y + pa.height - py) / pa.height
        const lo = u(yMin, logY), hi = u(yMax, logY)
        return ui(lo + f * (hi - lo), logY)
    }
    function pixelX(x) {
        const pa = graph.plotArea
        const lo = u(xMin, logX), hi = u(xMax, logX)
        return pa.x + (u(x, logX) - lo) / (hi - lo) * pa.width
    }
    function pixelY(y) {
        const pa = graph.plotArea
        const lo = u(yMin, logY), hi = u(yMax, logY)
        return pa.y + pa.height - (u(y, logY) - lo) / (hi - lo) * pa.height
    }

    // ---- data

    // Called by the dock whenever the selected spectrum or the quantity changes
    function setData(ellList, valueList, title, name) {
        ell = ellList
        values = valueList
        yTitle = title
        yName = name
        rebuild()
        fitAll()
    }

    // Called when the user switches between linear and logarithmic axes
    function setScales(lx, ly) {
        if (lx === logX && ly === logY)
            return
        logX = lx
        logY = ly
        rebuild()
        fitAll()
    }

    // A point can only be drawn on a log axis when it is positive
    function plottable(i) {
        return !logY || values[i] > 0
    }

    function rebuild() {
        series.clear()
        for (let i = 0; i < ell.length; ++i)
            if (plottable(i))
                series.append(ell[i], values[i])
    }

    function fitAll() {
        hoverMarker.visible = false
        if (ell.length === 0)
            return
        let lo = Infinity, hi = -Infinity
        for (let i = 0; i < ell.length; ++i) {
            if (!plottable(i))
                continue
            lo = Math.min(lo, values[i])
            hi = Math.max(hi, values[i])
        }
        if (!isFinite(lo))
            return

        xMin = ell[0]
        xMax = ell[ell.length - 1]
        if (logY) {
            yMin = lo / 1.4
            yMax = hi * 1.4
        } else {
            const pad = (hi - lo > 0 ? hi - lo : Math.abs(hi) || 1) * 0.05
            yMin = lo - pad
            yMax = hi + pad
        }
        if (xMax <= xMin)
            xMax = xMin + 1
    }

    // ---- hover readout

    Rectangle {
        id: hoverMarker
        width: 9
        height: 9
        radius: 5
        color: "#ffd166"
        border.color: view.bgColor
        border.width: 1
        visible: false
        z: 5
    }

    Rectangle {
        id: readout
        visible: hoverMarker.visible
        color: view.mix(view.bgColor, view.fgColor, 0.08)
        border.color: view.dim
        radius: 4
        z: 6
        width: readoutText.implicitWidth + 14
        height: readoutText.implicitHeight + 10
        // Next to the marker, kept inside the plot
        x: Math.min(hoverMarker.x + 12, view.width - width - 4)
        y: Math.max(hoverMarker.y - height - 8, 4)

        Text {
            id: readoutText
            anchors.centerIn: parent
            color: view.fg
            font.pixelSize: 12
            text: ""
        }
    }

    function showReadout(px, py) {
        const pa = graph.plotArea
        if (ell.length === 0 || pa.width <= 0
                || px < pa.x || px > pa.x + pa.width
                || py < pa.y || py > pa.y + pa.height) {
            hoverMarker.visible = false
            return
        }
        // The nearest multipole (the data are one point per l)
        const l = dataX(px)
        let i = Math.round(l - ell[0])
        i = Math.max(0, Math.min(ell.length - 1, i))
        if (!plottable(i)) {
            hoverMarker.visible = false
            return
        }
        const cx = pixelX(ell[i]), cy = pixelY(values[i])
        if (cx < pa.x || cx > pa.x + pa.width
                || cy < pa.y || cy > pa.y + pa.height) {
            hoverMarker.visible = false
            return
        }
        hoverMarker.x = cx - hoverMarker.width / 2
        hoverMarker.y = cy - hoverMarker.height / 2
        readoutText.text = "ℓ = " + ell[i] + "\n" + yName + " = "
                           + values[i].toPrecision(6)
        hoverMarker.visible = true
    }

    // ---- zoom and pan

    function zoomAround(px, py, factor, zoomX, zoomY) {
        const pa = graph.plotArea
        if (pa.width <= 0)
            return
        if (zoomX) {
            const lo = u(xMin, logX), hi = u(xMax, logX)
            const c = u(dataX(px), logX)
            const nlo = c - (c - lo) * factor, nhi = c + (hi - c) * factor
            // Keep the multipole range inside the data
            if (ell.length > 0 && nhi - nlo > 0) {
                xMin = Math.max(ui(nlo, logX), ell[0] - 1)
                xMax = Math.min(ui(nhi, logX), ell[ell.length - 1] + 1)
            }
        }
        if (zoomY) {
            const lo = u(yMin, logY), hi = u(yMax, logY)
            const c = u(dataY(py), logY)
            yMin = ui(c - (c - lo) * factor, logY)
            yMax = ui(c + (hi - c) * factor, logY)
        }
    }

    function panBy(dx, dy) {
        const pa = graph.plotArea
        if (pa.width <= 0)
            return
        const xlo = u(xMin, logX), xhi = u(xMax, logX)
        const dxu = -dx / pa.width * (xhi - xlo)
        xMin = ui(xlo + dxu, logX)
        xMax = ui(xhi + dxu, logX)
        const ylo = u(yMin, logY), yhi = u(yMax, logY)
        const dyu = dy / pa.height * (yhi - ylo)
        yMin = ui(ylo + dyu, logY)
        yMax = ui(yhi + dyu, logY)
    }

    Item {
        id: interaction
        anchors.fill: parent
        z: 10

        HoverHandler {
            id: hover
            cursorShape: Qt.CrossCursor
            onPointChanged: {
                if (!drag.active)
                    view.showReadout(point.position.x, point.position.y)
            }
            onHoveredChanged: {
                if (!hovered)
                    hoverMarker.visible = false
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                const steps = event.angleDelta.y / 120
                const factor = Math.pow(1 / 1.18, steps)
                const shift = event.modifiers & Qt.ShiftModifier
                const ctrl = event.modifiers & Qt.ControlModifier
                view.zoomAround(point.position.x, point.position.y, factor,
                                !ctrl, !shift)
                view.showReadout(point.position.x, point.position.y)
            }
        }

        DragHandler {
            id: drag
            target: null
            property real lastX: 0
            property real lastY: 0
            onActiveChanged: {
                lastX = 0
                lastY = 0
                if (active)
                    hoverMarker.visible = false
            }
            onTranslationChanged: {
                view.panBy(translation.x - lastX, translation.y - lastY)
                lastX = translation.x
                lastY = translation.y
            }
        }

        TapHandler {
            onDoubleTapped: view.fitAll()
        }
    }
}
