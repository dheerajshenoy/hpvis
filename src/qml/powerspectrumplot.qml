import QtQuick
import QtGraphs

// Power spectrum plot: one or more curves with linear / logarithmic axes, a
// legend, a hover readout, wheel zoom, drag pan and double-click to reset.
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

    // The curves: [{name, color, ell: [...], values: [...]}, ...]. Each has
    // one point per multipole, in steps of one, so a point is found by its l.
    property var curves: []
    property var seriesObjects: []
    property var hoverPoints: []

    // Set from the dock (the user's choice); the rest is derived from them
    property color bgColor: "#121217"
    property color fgColor: "#e6e6e6"
    property color lineColor: "#7fd1ff"

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

    // GraphsView does not repaint when only a color changes; the dock sends
    // the curves again after a color change, which rebuilds the series
    onBgColorChanged: redraw()
    onFgColorChanged: redraw()

    // The axes are created afresh whenever the scale changes: reusing an axis
    // object that was taken off the graph leaves the series drawn against the
    // old scale (the curves vanish)
    Component {
        id: linXComp
        ValueAxis {
            min: view.xMin
            max: view.xMax
            titleText: "Multipole ℓ"
            titleColor: view.fg
            color: view.dim
            subColor: view.dim
        }
    }
    Component {
        id: logXComp
        LogValueAxis {
            base: 10
            min: Math.max(view.xMin, 1e-9)
            max: view.xMax
            titleText: "Multipole ℓ"
            titleColor: view.fg
            color: view.dim
            subColor: view.dim
        }
    }
    Component {
        id: linYComp
        ValueAxis {
            min: view.yMin
            max: view.yMax
            titleText: view.yTitle
            titleColor: view.fg
            color: view.dim
            subColor: view.dim
        }
    }
    Component {
        id: logYComp
        LogValueAxis {
            base: 10
            min: Math.max(view.yMin, 1e-300)
            max: view.yMax
            titleText: view.yTitle
            titleColor: view.fg
            color: view.dim
            subColor: view.dim
        }
    }

    // GraphsView can keep painting with the previous axis ranges after the
    // axes or the series change, until it is resized: ask for a redraw now
    // and once more after the scene graph has caught up
    function redraw() {
        if (graph)
            graph.update()
        redrawTimer.restart()
    }
    Timer {
        id: redrawTimer
        interval: 30
        onTriggered: {
            if (view.graph)
                view.graph.update()
        }
    }

    Component {
        id: lineComponent
        LineSeries {
            width: 2
        }
    }

    // The graph is rebuilt (with the right axis types from the start)
    // whenever a log setting changes: switching the axes of a live GraphsView
    // leaves its series drawn against the old scale
    property var graph: graphLoader.item

    Component {
        id: graphComp
        GraphsView {
            id: g
            theme: GraphsTheme {
                colorScheme: view.dark ? GraphsTheme.ColorScheme.Dark
                                       : GraphsTheme.ColorScheme.Light
                backgroundColor: view.bgColor
                plotAreaBackgroundColor: view.plotBg
                labelTextColor: view.fg
                grid.mainColor: view.gridMain
                grid.subColor: view.gridSub
            }
            Component.onCompleted: {
                axisX = (view.logX ? logXComp : linXComp).createObject(g)
                axisY = (view.logY ? logYComp : linYComp).createObject(g)
            }
        }
    }

    Loader {
        id: graphLoader
        anchors.fill: parent
        sourceComponent: graphComp
    }

    function recreateGraph() {
        seriesObjects = []   // they go with the old graph
        graphLoader.active = false
        graphLoader.active = true
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

    // Called by the dock whenever the curves, the quantity or the mode change
    function setCurves(curveList, title, keepRange) {
        curves = curveList
        yTitle = title
        rebuild()
        if (!keepRange)
            fitAll()
        redraw()
    }

    // Called when the user switches between linear and logarithmic axes
    function setScales(lx, ly) {
        if (lx === logX && ly === logY)
            return
        logX = lx
        logY = ly
        recreateGraph()
        rebuild()
        fitAll()
        redraw()
    }

    // A point can only be drawn when it is a number, and on a log axis when
    // it is positive
    function plottable(curve, i) {
        const v = curve.values[i]
        return isFinite(v) && (!logY || v > 0)
    }

    function rebuild() {
        if (!graph)
            return
        for (let k = 0; k < seriesObjects.length; ++k) {
            graph.removeSeries(seriesObjects[k])
            seriesObjects[k].destroy()
        }
        seriesObjects = []

        for (let c = 0; c < curves.length; ++c) {
            const curve = curves[c]
            const s = lineComponent.createObject(graph, {color: curve.color})
            graph.addSeries(s)
            for (let i = 0; i < curve.ell.length; ++i)
                if (plottable(curve, i))
                    s.append(curve.ell[i], curve.values[i])
            seriesObjects.push(s)
        }
    }

    function fitAll() {
        hoverPoints = []
        if (curves.length === 0)
            return
        let lo = Infinity, hi = -Infinity
        let l0 = Infinity, l1 = -Infinity
        for (let c = 0; c < curves.length; ++c) {
            const curve = curves[c]
            for (let i = 0; i < curve.ell.length; ++i) {
                if (!plottable(curve, i))
                    continue
                lo = Math.min(lo, curve.values[i])
                hi = Math.max(hi, curve.values[i])
                l0 = Math.min(l0, curve.ell[i])
                l1 = Math.max(l1, curve.ell[i])
            }
        }
        if (!isFinite(lo))
            return

        xMin = l0
        xMax = l1
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

    // ---- legend

    Column {
        id: legend
        visible: view.curves.length > 1
        spacing: 3
        z: 4
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 14
        anchors.topMargin: 26

        Repeater {
            model: view.curves
            Row {
                spacing: 6
                layoutDirection: Qt.RightToLeft
                Text {
                    text: modelData.name
                    color: view.fg
                    font.pixelSize: 12
                }
                Rectangle {
                    width: 18
                    height: 3
                    y: 6
                    color: modelData.color
                }
            }
        }
    }

    // ---- hover readout

    Repeater {
        model: view.hoverPoints
        Rectangle {
            x: modelData.x - 4
            y: modelData.y - 4
            width: 9
            height: 9
            radius: 5
            color: modelData.color
            border.color: view.bgColor
            border.width: 1
            z: 5
        }
    }

    Rectangle {
        id: readout
        visible: view.hoverPoints.length > 0
        color: view.mix(view.bgColor, view.fgColor, 0.08)
        border.color: view.dim
        radius: 4
        z: 6
        width: readoutText.implicitWidth + 14
        height: readoutText.implicitHeight + 10
        property real anchorX: 0
        property real anchorY: 0
        // Next to the points, kept inside the plot
        x: Math.min(anchorX + 12, view.width - width - 4)
        y: Math.max(anchorY - height - 8, 4)

        Text {
            id: readoutText
            anchors.centerIn: parent
            color: view.fg
            font.pixelSize: 12
            textFormat: Text.StyledText
            text: ""
        }
    }

    function showReadout(px, py) {
        const pa = graph.plotArea
        if (curves.length === 0 || pa.width <= 0
                || px < pa.x || px > pa.x + pa.width
                || py < pa.y || py > pa.y + pa.height) {
            hoverPoints = []
            return
        }
        // The nearest multipole
        const l = Math.round(dataX(px))
        const points = []
        let text = "ℓ = " + l
        let anchorX = 0, anchorY = Infinity
        for (let c = 0; c < curves.length; ++c) {
            const curve = curves[c]
            const i = l - curve.ell[0]
            if (i < 0 || i >= curve.ell.length || !plottable(curve, i))
                continue
            const cx = pixelX(curve.ell[i]), cy = pixelY(curve.values[i])
            if (cx < pa.x || cx > pa.x + pa.width
                    || cy < pa.y || cy > pa.y + pa.height)
                continue
            points.push({x: cx, y: cy, color: curve.color})
            text += "<br><font color='" + curve.color + "'>●</font> "
                    + curve.name + " = " + curve.values[i].toPrecision(6)
            anchorX = cx
            anchorY = Math.min(anchorY, cy)
        }
        if (points.length === 0) {
            hoverPoints = []
            return
        }
        readout.anchorX = anchorX
        readout.anchorY = anchorY
        readoutText.text = text
        hoverPoints = points
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
            if (curves.length > 0 && nhi - nlo > 0) {
                let l0 = Infinity, l1 = -Infinity
                for (let k = 0; k < curves.length; ++k) {
                    l0 = Math.min(l0, curves[k].ell[0])
                    l1 = Math.max(l1, curves[k].ell[curves[k].ell.length - 1])
                }
                xMin = Math.max(ui(nlo, logX), l0 - 1)
                xMax = Math.min(ui(nhi, logX), l1 + 1)
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
                    view.hoverPoints = []
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
                    view.hoverPoints = []
            }
            onTranslationChanged: {
                view.panBy(translation.x - lastX, translation.y - lastY)
                lastX = translation.x
                lastY = translation.y
            }
        }

        TapHandler {
            onDoubleTapped: {
                view.fitAll()
                view.redraw()
            }
        }
    }
}
