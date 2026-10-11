import QtQuick
import QtGraphs

// Pixel value histogram: one or more step curves over shared bins, with an
// optional logarithmic count axis, a legend, a hover readout, wheel zoom,
// drag pan and double-click to reset.
//
//   wheel            zoom both axes around the cursor
//   Shift + wheel    zoom x only        Ctrl + wheel    zoom y only
//   drag             pan
//   double-click     show all of the data
Item {
    id: view

    // Visible range (what the axes show)
    property real xMin: 0
    property real xMax: 1
    property real yMin: 0
    property real yMax: 1

    property bool logY: false
    property string xTitle: "Pixel value"
    property string yTitle: ""

    // The bins: bins equal bins of width binWidth starting at lo. The curves:
    // [{name, color, values: [one per bin]}, ...]
    property real lo: 0
    property real binWidth: 1
    property int bins: 1
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

    onBgColorChanged: redraw()
    onFgColorChanged: redraw()

    // A value on a log axis must be positive: empty bins sit on a floor a bit
    // below the smallest count
    property real floorValue: 0.5

    // The axes are created with the graph (see recreateGraph)
    Component {
        id: linXComp
        ValueAxis {
            min: view.xMin
            max: view.xMax
            titleText: view.xTitle
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

    Component {
        id: lineComponent
        LineSeries {
            width: 2
        }
    }

    // The graph is rebuilt (with the right axis types from the start)
    // whenever the y scale changes: switching the axes of a live GraphsView
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
                axisX = linXComp.createObject(g)
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

    // GraphsView can keep painting with the previous axis ranges until it is
    // resized: ask for a redraw now and once more after the scene graph has
    // caught up
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

    // ---- axis <-> pixel mapping (in the axis' own space: log or linear)

    function v(y) { return logY ? Math.log(y) : y }
    function vi(y) { return logY ? Math.exp(y) : y }

    function dataX(px) {
        const pa = graph.plotArea
        return xMin + (px - pa.x) / pa.width * (xMax - xMin)
    }
    function dataY(py) {
        const pa = graph.plotArea
        const f = (pa.y + pa.height - py) / pa.height
        return vi(v(yMin) + f * (v(yMax) - v(yMin)))
    }
    function pixelX(x) {
        const pa = graph.plotArea
        return pa.x + (x - xMin) / (xMax - xMin) * pa.width
    }
    function pixelY(y) {
        const pa = graph.plotArea
        return pa.y + pa.height
               - (v(y) - v(yMin)) / (v(yMax) - v(yMin)) * pa.height
    }

    // ---- data

    // Called by the dock whenever the histogram changes. keepRange keeps the
    // zoom (e.g. after a color change).
    function setHistogram(low, width, binCount, curveList, xAxisTitle,
                          yAxisTitle, log, keepRange) {
        lo = low
        binWidth = width
        bins = binCount
        curves = curveList
        xTitle = xAxisTitle
        yTitle = yAxisTitle
        const scaleChanged = (log !== logY)
        logY = log
        if (scaleChanged)
            recreateGraph()
        computeFloor()
        rebuild()
        if (!keepRange || scaleChanged)
            fitAll()
        redraw()
    }

    function computeFloor() {
        let smallest = Infinity
        for (let c = 0; c < curves.length; ++c)
            for (let i = 0; i < bins; ++i) {
                const y = curves[c].values[i]
                if (isFinite(y) && y > 0)
                    smallest = Math.min(smallest, y)
            }
        floorValue = isFinite(smallest) ? smallest / 3 : 0.5
    }

    // What is drawn for a bin: its value, or the floor for an empty one on a
    // log axis. Not a number: nothing.
    function drawn(y) {
        if (!isFinite(y))
            return NaN
        return (logY && y <= 0) ? floorValue : y
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
            for (let i = 0; i < bins; ++i) {
                const y = drawn(curve.values[i])
                if (isNaN(y))
                    continue
                s.append(lo + i * binWidth, y)
                s.append(lo + (i + 1) * binWidth, y)
            }
            seriesObjects.push(s)
        }
    }

    function fitAll() {
        hoverPoints = []
        let ymin = Infinity, ymax = -Infinity
        for (let c = 0; c < curves.length; ++c)
            for (let i = 0; i < bins; ++i) {
                const y = drawn(curves[c].values[i])
                if (isNaN(y))
                    continue
                ymin = Math.min(ymin, y)
                ymax = Math.max(ymax, y)
            }
        xMin = lo
        xMax = lo + bins * binWidth
        if (!isFinite(ymin))
            return
        if (logY) {
            yMin = ymin / 1.5
            yMax = ymax * 1.5
        } else {
            const low = Math.min(0, ymin)
            const pad = (ymax - low > 0 ? ymax - low : Math.abs(ymax) || 1) * 0.05
            yMin = low < 0 ? low - pad : 0
            yMax = ymax + pad
        }
        if (xMax <= xMin)
            xMax = xMin + 1
    }

    // ---- legend

    Column {
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
        const pa = graph ? graph.plotArea : null
        if (!pa || curves.length === 0 || pa.width <= 0
                || px < pa.x || px > pa.x + pa.width
                || py < pa.y || py > pa.y + pa.height) {
            hoverPoints = []
            return
        }
        // The bin under the pointer
        const i = Math.floor((dataX(px) - lo) / binWidth)
        if (i < 0 || i >= bins) {
            hoverPoints = []
            return
        }
        const a = lo + i * binWidth, b = a + binWidth
        const centre = (a + b) / 2
        const points = []
        let text = "[" + a.toPrecision(5) + ", " + b.toPrecision(5) + ")"
        let anchorY = Infinity
        let any = false
        for (let c = 0; c < curves.length; ++c) {
            const curve = curves[c]
            const y = curve.values[i]
            const yd = drawn(y)
            if (isNaN(yd))
                continue
            const cy = pixelY(yd)
            if (cy >= pa.y && cy <= pa.y + pa.height)
                points.push({x: pixelX(centre), y: cy, color: curve.color})
            text += "<br><font color='" + curve.color + "'>●</font> "
                    + curve.name + " = " + y.toPrecision(5)
            anchorY = Math.min(anchorY, cy)
            any = true
        }
        if (!any) {
            hoverPoints = []
            return
        }
        readout.anchorX = pixelX(centre)
        readout.anchorY = isFinite(anchorY) ? anchorY : py
        readoutText.text = text
        // The readout shows together with the dots: keep one when every
        // value is off the screen
        hoverPoints = points.length > 0
                      ? points
                      : [{x: pixelX(centre), y: py, color: view.fg}]
    }

    // ---- zoom and pan

    function zoomAround(px, py, factor, zoomX, zoomY) {
        const pa = graph.plotArea
        if (pa.width <= 0)
            return
        if (zoomX) {
            const c = dataX(px)
            const nlo = c - (c - xMin) * factor, nhi = c + (xMax - c) * factor
            if (nhi - nlo > 0) {
                xMin = Math.max(nlo, lo - binWidth)
                xMax = Math.min(nhi, lo + (bins + 1) * binWidth)
            }
        }
        if (zoomY) {
            const ylo = v(yMin), yhi = v(yMax)
            const c = v(dataY(py))
            yMin = vi(c - (c - ylo) * factor)
            yMax = vi(c + (yhi - c) * factor)
        }
    }

    function panBy(dx, dy) {
        const pa = graph.plotArea
        if (pa.width <= 0)
            return
        const dxu = -dx / pa.width * (xMax - xMin)
        xMin += dxu
        xMax += dxu
        const ylo = v(yMin), yhi = v(yMax)
        const dyu = dy / pa.height * (yhi - ylo)
        yMin = vi(ylo + dyu)
        yMax = vi(yhi + dyu)
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
