import QtQuick
import QtGraphs

GraphsView {
    id: view

    property real xMin: 2
    property real xMax: 3
    property real yMin: 0
    property real yMax: 1
    property string yTitle: ""

    readonly property color fg: "#e6e6e6"
    readonly property color dim: "#5a5a64"

    theme: GraphsTheme {
        colorScheme: GraphsTheme.ColorScheme.Dark
        backgroundColor: "#121217"
        plotAreaBackgroundColor: "#16161d"
        labelTextColor: view.fg
        grid.mainColor: "#32323c"
        grid.subColor: "#24242c"
    }

    axisX: ValueAxis {
        min: view.xMin
        max: view.xMax
        titleText: "Multipole l"
        titleColor: view.fg
        color: view.dim
        subColor: view.dim
    }

    axisY: ValueAxis {
        min: view.yMin
        max: view.yMax
        titleText: view.yTitle
        titleColor: view.fg
        color: view.dim
        subColor: view.dim
    }

    LineSeries {
        id: series
        color: "#7fd1ff"
        width: 2
    }

    // Called by the dialog whenever the selected spectrum changes
    function setData(ell, values, title) {
        series.clear()
        if (ell.length === 0)
            return
        let lo = Infinity, hi = -Infinity
        for (let i = 0; i < ell.length; ++i) {
            series.append(ell[i], values[i])
            lo = Math.min(lo, values[i])
            hi = Math.max(hi, values[i])
        }
        // Fit both axes to the data, with a little headroom
        const pad = (hi - lo > 0 ? hi - lo : Math.abs(hi) || 1) * 0.05
        xMin = ell[0]
        xMax = ell[ell.length - 1]
        yMin = lo - pad
        yMax = hi + pad
        yTitle = title
    }
}
